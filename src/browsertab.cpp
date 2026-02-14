#include "browsertab.h"

#include "trashview.h"
#include "openwithdialog.h"

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

  // Detailed list (QTreeView)
  listView_ = new QTreeView(this);
  listView_->setModel(fsModel_);
  listView_->setSortingEnabled(true);
  listView_->sortByColumn(0, Qt::AscendingOrder);
  listView_->setSelectionMode(QAbstractItemView::ExtendedSelection);
  listView_->setSelectionBehavior(QAbstractItemView::SelectRows);
  listView_->setEditTriggers(QAbstractItemView::NoEditTriggers);
  listView_->setUniformRowHeights(true);
  listView_->setContextMenuPolicy(Qt::CustomContextMenu);

  listView_->header()->setSectionResizeMode(0, QHeaderView::Stretch);
  listView_->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
  listView_->header()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
  listView_->header()->setSectionResizeMode(3, QHeaderView::ResizeToContents);

  connect(listView_, &QTreeView::doubleClicked, this, &BrowserTab::onActivated);
  connect(listView_, &QTreeView::activated,     this, &BrowserTab::onActivated);
  connect(listView_, &QWidget::customContextMenuRequested, this, &BrowserTab::onContextMenu);

  listView_->viewport()->installEventFilter(this);

  // Grid icons (QListView in IconMode)
  iconView_ = new QListView(this);
  iconView_->setModel(fsModel_);
  iconView_->setViewMode(QListView::IconMode);
  iconView_->setResizeMode(QListView::Adjust);
  iconView_->setSelectionMode(QAbstractItemView::ExtendedSelection);
  iconView_->setSelectionBehavior(QAbstractItemView::SelectItems);
  iconView_->setEditTriggers(QAbstractItemView::NoEditTriggers);
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
  compactView_->setModel(fsModel_);
  compactView_->setViewMode(QListView::ListMode);
  compactView_->setSelectionMode(QAbstractItemView::ExtendedSelection);
  compactView_->setSelectionBehavior(QAbstractItemView::SelectItems);
  compactView_->setEditTriggers(QAbstractItemView::NoEditTriggers);
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

  navigateTo(QDir::homePath(), true);
}

QAbstractItemView* BrowserTab::currentFileView() const
{
  switch (viewMode_) {
    case ViewMode::GridIcons: return iconView_;
    case ViewMode::List:      return listView_;
    case ViewMode::Compact:   return compactView_;
  }
  return listView_;
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

void BrowserTab::showFilePane() { stack_->setCurrentWidget(fileStack_); }

void BrowserTab::showTrashPane() {
  stack_->setCurrentWidget(trashView_);
  trashView_->refresh();
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

  QModelIndex root = fsModel_->index(location_);
  if (!root.isValid()) {
    fsModel_->setRootPath(location_);
    root = fsModel_->index(location_);
  }
  listView_->setRootIndex(root);
  iconView_->setRootIndex(root);
  compactView_->setRootIndex(root);

  showFilePane();
  emit locationChanged(location_);
  setTabTitleFromLocation();
}

void BrowserTab::setViewMode(ViewMode m) {
  viewMode_ = m;
  if (!fileStack_) return;
  switch (m) {
    case ViewMode::GridIcons: fileStack_->setCurrentWidget(iconView_); break;
    case ViewMode::List:      fileStack_->setCurrentWidget(listView_); break;
    case ViewMode::Compact:   fileStack_->setCurrentWidget(compactView_); break;
  }
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
  if (inTrash()) { trashView_->refresh(); return; }
  navigateTo(location_, false);
}

QStringList BrowserTab::selectedPaths() const {
  QStringList out;
  if (inTrash()) return out;
  auto *v = currentFileView();
  if (!v || !v->selectionModel()) return out;

  // For QTreeView we prefer selectedRows(0). For QListView, selectedIndexes() is fine.
  QModelIndexList idxs;
  if (auto *tv = qobject_cast<QTreeView*>(v)) idxs = tv->selectionModel()->selectedRows(0);
  else idxs = v->selectionModel()->selectedIndexes();

  out.reserve(idxs.size());
  for (const QModelIndex &i : idxs) out << fsModel_->filePath(i.sibling(i.row(), 0));
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

void BrowserTab::onActivated(const QModelIndex &idx) {
  if (!idx.isValid()) return;

  const QString path = fsModel_->filePath(idx);
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
    const QString p = fsModel_->filePath(cur.sibling(cur.row(), 0));
    if (QFileInfo(p).isDir()) return p;
  }

  QModelIndexList idxs;
  if (auto *tv = qobject_cast<QTreeView*>(v)) idxs = tv->selectionModel()->selectedRows(0);
  else idxs = v->selectionModel()->selectedIndexes();

  for (const QModelIndex &i : idxs) {
    const QString p = fsModel_->filePath(i.sibling(i.row(), 0));
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
  QString clickedPath = hasIndex ? fsModel_->filePath(idx) : QString();

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
          const QString p = fsModel_->filePath(idx);
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
