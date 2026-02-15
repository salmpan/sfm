#include "browsertab.h"

#include "trashview.h"
#include "openwithdialog.h"
#include "filesortproxy.h"

#include <QFileSystemModel>
#include <QTreeView>
#include <QListView>
#include <QStackedWidget>
#include <QVBoxLayout>
#include <QHeaderView>
#include <QDir>
#include <QFileInfo>
#include <QDesktopServices>
#include <QUrl>
#include <QItemSelectionModel>
#include <QMenu>
#include <QMouseEvent>
#include <QKeyEvent>

BrowserTab::BrowserTab(QFileSystemModel *sharedModel, QWidget *parent)
  : QWidget(parent), fsModel_(sharedModel) {

  fsProxy_ = new FileSortProxyModel(this);
  fsProxy_->setSourceModel(fsModel_);
  fsProxy_->setFoldersFirst(true);
  fsProxy_->sort(0, Qt::AscendingOrder);

  // Detailed list (QTreeView)
  listView_ = new QTreeView(this);
  listView_->setModel(fsProxy_);
  listView_->setSortingEnabled(true);
  listView_->sortByColumn(0, Qt::AscendingOrder);
  listView_->setSelectionMode(QAbstractItemView::ExtendedSelection);
  listView_->setSelectionBehavior(QAbstractItemView::SelectRows);
  listView_->setEditTriggers(QAbstractItemView::EditKeyPressed);
  listView_->setUniformRowHeights(true);
  listView_->setContextMenuPolicy(Qt::CustomContextMenu);

  listView_->header()->setSectionResizeMode(0, QHeaderView::Stretch);
  listView_->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
  listView_->header()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
  listView_->header()->setSectionResizeMode(3, QHeaderView::ResizeToContents);

  connect(listView_, &QTreeView::doubleClicked, this, &BrowserTab::onActivated);
  connect(listView_, &QTreeView::activated,     this, &BrowserTab::onActivated);
  connect(listView_, &QWidget::customContextMenuRequested, this, &BrowserTab::onContextMenu);
  connect(listView_->header(), &QHeaderView::sortIndicatorChanged, this, &BrowserTab::onHeaderSortChanged);

  listView_->viewport()->installEventFilter(this);

  // Grid icons (QListView in IconMode)
  iconView_ = new QListView(this);
  iconView_->setModel(fsProxy_);
  iconView_->setViewMode(QListView::IconMode);
  iconView_->setResizeMode(QListView::Adjust);
  iconView_->setSelectionMode(QAbstractItemView::ExtendedSelection);
  iconView_->setSelectionBehavior(QAbstractItemView::SelectItems);
  iconView_->setEditTriggers(QAbstractItemView::EditKeyPressed);
  iconView_->setContextMenuPolicy(Qt::CustomContextMenu);
  iconView_->setUniformItemSizes(true);
  iconView_->setWordWrap(true);
  iconView_->setIconSize(QSize(64, 64));
  iconView_->setGridSize(QSize(110, 100));
  iconView_->setMovement(QListView::Static);

  connect(iconView_, &QListView::doubleClicked, this, &BrowserTab::onActivated);
  connect(iconView_, &QListView::activated,     this, &BrowserTab::onActivated);
  connect(iconView_, &QWidget::customContextMenuRequested, this, &BrowserTab::onContextMenu);

  iconView_->viewport()->installEventFilter(this);

  // Compact list (QListView in ListMode)
  compactView_ = new QListView(this);
  compactView_->setModel(fsProxy_);
  compactView_->setViewMode(QListView::ListMode);
  compactView_->setSelectionMode(QAbstractItemView::ExtendedSelection);
  compactView_->setSelectionBehavior(QAbstractItemView::SelectItems);
  compactView_->setEditTriggers(QAbstractItemView::EditKeyPressed);
  compactView_->setContextMenuPolicy(Qt::CustomContextMenu);
  compactView_->setUniformItemSizes(true);
  compactView_->setIconSize(QSize(16, 16));
  compactView_->setWrapping(false);
  compactView_->setSpacing(0);

  connect(compactView_, &QListView::doubleClicked, this, &BrowserTab::onActivated);
  connect(compactView_, &QListView::activated,     this, &BrowserTab::onActivated);
  connect(compactView_, &QWidget::customContextMenuRequested, this, &BrowserTab::onContextMenu);

  compactView_->viewport()->installEventFilter(this);

  trashView_ = new TrashView(this);
  connect(trashView_, &TrashView::requestNavigate, this, [this](const QString &p){
    emit requestNavigate(p);
  });

  fileStack_ = new QStackedWidget(this);
  fileStack_->addWidget(iconView_);
  fileStack_->addWidget(listView_);
  fileStack_->addWidget(compactView_);
  fileStack_->setCurrentWidget(listView_);

  stack_ = new QStackedWidget(this);
  stack_->addWidget(fileStack_);
  stack_->addWidget(trashView_);

  auto *layout = new QVBoxLayout(this);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->addWidget(stack_);

  // Default per-pane state
  fileViewMode_ = ViewMode::List;
  trashViewMode_ = ViewMode::List;

  fileSort_.key = SortKey::Name;
  fileSort_.order = Qt::AscendingOrder;
  fileSort_.foldersFirst = true;

  trashSort_.key = SortKey::Name;
  trashSort_.order = Qt::AscendingOrder;
  trashSort_.foldersFirst = true;

  setViewMode(fileViewMode_);
  setSort(fileSort_.key, fileSort_.order);
  setFoldersFirst(true);

  navigateTo(QDir::homePath(), true);
}

QAbstractItemView* BrowserTab::currentFileView() const {
  switch (fileViewMode_) {
    case ViewMode::GridIcons: return iconView_;
    case ViewMode::List:      return listView_;
    case ViewMode::Compact:   return compactView_;
  }
  return listView_;
}

QModelIndex BrowserTab::toSourceIndex(const QModelIndex &proxyIdx) const {
  if (!proxyIdx.isValid()) return {};
  if (!fsProxy_) return proxyIdx;
  return fsProxy_->mapToSource(proxyIdx);
}

QString BrowserTab::pathForIndex(const QModelIndex &proxyIdx) const {
  const QModelIndex src = toSourceIndex(proxyIdx.sibling(proxyIdx.row(), 0));
  if (!src.isValid()) return {};
  return fsModel_->filePath(src);
}

void BrowserTab::setTabTitleFromLocation() {
  QString title;
  if (inTrash()) title = "Trash";
  else {
    QFileInfo fi(location_);
    title = fi.fileName();
    if (title.isEmpty()) title = location_;
  }
  emit titleChanged(title);
}

void BrowserTab::showFilePane() {
  stack_->setCurrentWidget(fileStack_);
  // Restore per-pane view mode
  switch (fileViewMode_) {
    case ViewMode::GridIcons: fileStack_->setCurrentWidget(iconView_); break;
    case ViewMode::List:      fileStack_->setCurrentWidget(listView_); break;
    case ViewMode::Compact:   fileStack_->setCurrentWidget(compactView_); break;
  }
  // Restore per-pane sort
  setSort(fileSort_.key, fileSort_.order);
  setFoldersFirst(fileSort_.foldersFirst);
}

void BrowserTab::showTrashPane() {
  stack_->setCurrentWidget(trashView_);
  trashView_->refresh();
  // Restore per-pane state
  trashView_->setFoldersFirst(trashSort_.foldersFirst);
  trashView_->setSort((int)trashSort_.key, trashSort_.order);
  switch (trashViewMode_) {
    case ViewMode::GridIcons: trashView_->setViewMode(TrashView::ViewMode::GridIcons); break;
    case ViewMode::List:      trashView_->setViewMode(TrashView::ViewMode::List); break;
    case ViewMode::Compact:   trashView_->setViewMode(TrashView::ViewMode::Compact); break;
  }
}

void BrowserTab::navigateTo(const QString &loc, bool pushHistory) {
  const QString input = loc.trimmed().isEmpty() ? QString("/") : loc.trimmed();

  if (pushHistory) {
    if (historyIndex_ + 1 < (int)history_.size()) {
      history_.erase(history_.begin() + historyIndex_ + 1, history_.end());
    }
    if (history_.empty() || history_.back() != input) {
      history_.push_back(input);
      historyIndex_ = (int)history_.size() - 1;
    } else {
      historyIndex_ = (int)history_.size() - 1;
    }
  }

  if (isTrashUrl(input)) {
    location_ = "trash:///";
    showTrashPane();
    emit locationChanged(location_);
    setTabTitleFromLocation();
    return;
  }

  const QString normalized = QDir(input).absolutePath();
  location_ = normalized;

  QModelIndex srcRoot = fsModel_->index(location_);
  if (!srcRoot.isValid()) {
    fsModel_->setRootPath(location_);
    srcRoot = fsModel_->index(location_);
  }

  const QModelIndex proxyRoot = fsProxy_->mapFromSource(srcRoot);
  listView_->setRootIndex(proxyRoot);
  iconView_->setRootIndex(proxyRoot);
  compactView_->setRootIndex(proxyRoot);

  showFilePane();
  emit locationChanged(location_);
  setTabTitleFromLocation();
}

void BrowserTab::setViewMode(ViewMode m) {
  if (inTrash()) {
    trashViewMode_ = m;
    if (!trashView_) return;
    switch (m) {
      case ViewMode::GridIcons: trashView_->setViewMode(TrashView::ViewMode::GridIcons); break;
      case ViewMode::List:      trashView_->setViewMode(TrashView::ViewMode::List); break;
      case ViewMode::Compact:   trashView_->setViewMode(TrashView::ViewMode::Compact); break;
    }
    return;
  }

  fileViewMode_ = m;
  if (!fileStack_) return;
  switch (m) {
    case ViewMode::GridIcons: fileStack_->setCurrentWidget(iconView_); break;
    case ViewMode::List:      fileStack_->setCurrentWidget(listView_); break;
    case ViewMode::Compact:   fileStack_->setCurrentWidget(compactView_); break;
  }
}

void BrowserTab::setSort(SortKey key, Qt::SortOrder order) {
  if (inTrash()) {
    trashSort_.key = key;
    trashSort_.order = order;
    if (trashView_) trashView_->setSort((int)key, order);
    return;
  }

  fileSort_.key = key;
  fileSort_.order = order;

  const int col = (int)key;
  if (fsProxy_) fsProxy_->sort(col, order);
  if (listView_) listView_->header()->setSortIndicator(col, order);
}

void BrowserTab::setFoldersFirst(bool on) {
  if (inTrash()) {
    trashSort_.foldersFirst = on;
    if (trashView_) trashView_->setFoldersFirst(on);
    return;
  }

  fileSort_.foldersFirst = on;
  if (fsProxy_) fsProxy_->setFoldersFirst(on);
  if (fsProxy_) fsProxy_->sort((int)fileSort_.key, fileSort_.order);
}

void BrowserTab::onHeaderSortChanged(int logicalIndex, Qt::SortOrder order) {
  // Header-driven sorting applies only to filesystem list view.
  if (inTrash()) return;
  if (logicalIndex < 0 || logicalIndex > 3) return;
  setSort((SortKey)logicalIndex, order);
}

void BrowserTab::goBack() {
  if (!canGoBack()) return;
  historyIndex_--;
  navigateTo(history_[historyIndex_], false);
}

void BrowserTab::goForward() {
  if (!canGoForward()) return;
  historyIndex_++;
  navigateTo(history_[historyIndex_], false);
}

void BrowserTab::goUp() {
  if (inTrash()) return;
  QDir d(location_);
  if (!d.cdUp()) return;
  navigateTo(d.absolutePath(), true);
}

void BrowserTab::refresh() {
  if (inTrash()) { if (trashView_) trashView_->refresh(); return; }
  navigateTo(location_, false);
}

QStringList BrowserTab::selectedPaths() const {
  QStringList out;
  if (inTrash()) return out;
  auto *v = currentFileView();
  if (!v || !v->selectionModel()) return out;

  QModelIndexList idxs;
  if (auto *tv = qobject_cast<QTreeView*>(v)) idxs = tv->selectionModel()->selectedRows(0);
  else idxs = v->selectionModel()->selectedIndexes();

  out.reserve(idxs.size());
  for (const QModelIndex &pi : idxs) {
    const QString p = pathForIndex(pi);
    if (!p.isEmpty()) out << p;
  }
  return out;
}

QStringList BrowserTab::selectedTrashedPaths() const {
  if (!inTrash() || !trashView_) return {};
  return trashView_->selectedTrashedPaths();
}

bool BrowserTab::trashDeleteSelected(QString *errorOut) {
  if (!inTrash() || !trashView_) return true;
  return trashView_->deleteSelectedPermanently(errorOut);
}

bool BrowserTab::trashRestoreSelected(QString *errorOut) {
  if (!inTrash() || !trashView_) return true;
  return trashView_->restoreSelected(errorOut);
}

void BrowserTab::onActivated(const QModelIndex &proxyIdx) {
  if (!proxyIdx.isValid()) return;

  const QString path = pathForIndex(proxyIdx);
  if (path.isEmpty()) return;

  QFileInfo info(path);
  if (info.isDir()) {
    navigateTo(path, true);
    return;
  }
  QDesktopServices::openUrl(QUrl::fromLocalFile(path));
}

QString BrowserTab::currentSelectedDirOrEmpty() const {
  if (inTrash()) return {};

  auto *v = currentFileView();
  if (!v || !v->selectionModel()) return {};

  QModelIndex cur = v->currentIndex();
  if (cur.isValid()) {
    const QString p = pathForIndex(cur);
    if (QFileInfo(p).isDir()) return p;
  }

  QModelIndexList idxs;
  if (auto *tv = qobject_cast<QTreeView*>(v)) idxs = tv->selectionModel()->selectedRows(0);
  else idxs = v->selectionModel()->selectedIndexes();

  for (const QModelIndex &pi : idxs) {
    const QString p = pathForIndex(pi);
    if (QFileInfo(p).isDir()) return p;
  }
  return {};
}

void BrowserTab::openInNewTabIfDir(const QString &path) {
  if (path.isEmpty()) return;
  QFileInfo fi(path);
  if (!fi.exists() || !fi.isDir()) return;
  emit openFolderInNewTabRequested(QDir(path).absolutePath());
}

void BrowserTab::onCtrlEnter() {
  openInNewTabIfDir(currentSelectedDirOrEmpty());
}

void BrowserTab::onContextMenu(const QPoint &pos) {
  if (inTrash()) return; // TrashView has its own context menu

  auto *v = currentFileView();
  if (!v) return;

  const QModelIndex idx = v->indexAt(pos);
  const bool hasIndex = idx.isValid();
  QString clickedPath = hasIndex ? pathForIndex(idx) : QString();

  QMenu menu(this);

  QAction *aOpen = menu.addAction("Open");
  aOpen->setEnabled(hasIndex);

  QAction *aOpenNewTab = menu.addAction("Open in New Tab");
  aOpenNewTab->setEnabled(hasIndex && QFileInfo(clickedPath).isDir());
  aOpenNewTab->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_Return));

  // Open With submenu (files only)
  QMenu *openWithMenu = nullptr;
  QAction *aOpenWithDialog = nullptr;
  if (hasIndex && QFileInfo(clickedPath).isFile()) {
    openWithMenu = menu.addMenu("Open With");
    const QString mime = OpenWithDialog::detectMime(clickedPath);
    const QString def = OpenWithDialog::queryDefaultDesktopId(mime);
    const auto apps = OpenWithDialog::queryAppsForMime(mime, 6);

    for (const auto &app : apps) {
      QString text = app.name;
      if (!def.isEmpty() && app.desktopId == def) text += "  (default)";
      QAction *a = openWithMenu->addAction(QIcon::fromTheme(app.iconName), text);
      a->setData(app.desktopId);
    }
    if (!apps.isEmpty()) openWithMenu->addSeparator();
    aOpenWithDialog = openWithMenu->addAction("Other Application…");
  }

  menu.addSeparator();
  QAction *aNewFolder = menu.addAction("New Folder…");
  QAction *aNewDoc = menu.addAction("New Document…");

  QAction *aRename = menu.addAction("Rename");
  aRename->setEnabled(hasIndex);
  aRename->setShortcut(Qt::Key_F2);

  QAction *aProps = menu.addAction("Properties");
  aProps->setEnabled(hasIndex);
  aProps->setShortcut(QKeySequence(Qt::ALT | Qt::Key_Return));

  QAction *chosen = menu.exec(v->viewport()->mapToGlobal(pos));
  if (!chosen) return;

  if (chosen == aOpen) {
    onActivated(idx);
    return;
  }
  if (chosen == aOpenNewTab) {
    openInNewTabIfDir(clickedPath);
    return;
  }
  if (chosen == aNewFolder) {
    emit createNewFolderRequested();
    return;
  }
  if (chosen == aNewDoc) {
    emit createNewDocumentRequested();
    return;
  }

  if (chosen == aRename) {
    if (hasIndex) {
      auto *view = currentFileView();
      if (view) {
        const QModelIndex nameIdx = idx.sibling(idx.row(), 0);
        view->setCurrentIndex(nameIdx);
        if (view->selectionModel()) {
          view->selectionModel()->select(nameIdx, QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
        }
      }
    }
    beginInlineRename();
    return;
  }

  if (chosen == aProps) {
    emit propertiesRequested(clickedPath);
    return;
  }

  if (openWithMenu) {
    if (aOpenWithDialog && chosen == aOpenWithDialog) {
      emit openWithDialogRequested(clickedPath);
      return;
    }
    const QString desktopId = chosen->data().toString();
    if (!desktopId.isEmpty()) {
      emit openWithAppRequested(desktopId, clickedPath);
      return;
    }
  }
}

bool BrowserTab::eventFilter(QObject *obj, QEvent *event) {
  auto handleFor = [&](QAbstractItemView *v) -> bool {
    if (!v) return false;
    if (event->type() == QEvent::KeyPress) {
      auto *ke = static_cast<QKeyEvent*>(event);
      if ((ke->modifiers() & Qt::ControlModifier) &&
          (ke->key() == Qt::Key_Return || ke->key() == Qt::Key_Enter)) {
        onCtrlEnter();
        return true;
      }
    }

    if (event->type() == QEvent::MouseButtonRelease) {
      auto *me = static_cast<QMouseEvent*>(event);
      if (me->button() == Qt::MiddleButton) {
        const QModelIndex idx = v->indexAt(me->position().toPoint());
        if (idx.isValid()) {
          const QString p = pathForIndex(idx);
          if (QFileInfo(p).isDir()) {
            openInNewTabIfDir(p);
            return true;
          }
        }
      }
    }
    return false;
  };

  if (obj == listView_->viewport()) return handleFor(listView_);
  if (obj == iconView_->viewport()) return handleFor(iconView_);
  if (obj == compactView_->viewport()) return handleFor(compactView_);
  return QWidget::eventFilter(obj, event);
}

void BrowserTab::beginInlineRename() {
  if (inTrash()) return;
  auto *v = currentFileView();
  if (!v) return;

  QModelIndex idx = v->currentIndex();
  if (!idx.isValid() && v->selectionModel()) {
    const auto rows = v->selectionModel()->selectedRows(0);
    if (!rows.isEmpty()) idx = rows.first();
  }
  if (!idx.isValid()) return;

  idx = idx.sibling(idx.row(), 0);
  v->setCurrentIndex(idx);
  v->edit(idx);
}
