#include "browsertab.h"

#include "trashview.h"
#include "openwithdialog.h"

#include <QFileSystemModel>
#include <QTreeView>
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

  view_ = new QTreeView(this);
  view_->setModel(fsModel_);
  view_->setSortingEnabled(true);
  view_->sortByColumn(0, Qt::AscendingOrder);
  view_->setSelectionMode(QAbstractItemView::ExtendedSelection);
  view_->setSelectionBehavior(QAbstractItemView::SelectRows);
  view_->setEditTriggers(QAbstractItemView::NoEditTriggers);
  view_->setUniformRowHeights(true);
  view_->setContextMenuPolicy(Qt::CustomContextMenu);

  view_->header()->setSectionResizeMode(0, QHeaderView::Stretch);
  view_->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
  view_->header()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
  view_->header()->setSectionResizeMode(3, QHeaderView::ResizeToContents);

  connect(view_, &QTreeView::doubleClicked, this, &BrowserTab::onActivated);
  connect(view_, &QTreeView::activated,     this, &BrowserTab::onActivated);
  connect(view_, &QWidget::customContextMenuRequested, this, &BrowserTab::onContextMenu);

  view_->viewport()->installEventFilter(this);

  trashView_ = new TrashView(this);
  connect(trashView_, &TrashView::requestNavigate, this, [this](const QString &p){
    emit requestNavigate(p);
  });

  stack_ = new QStackedWidget(this);
  stack_->addWidget(view_);
  stack_->addWidget(trashView_);

  auto *layout = new QVBoxLayout(this);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->addWidget(stack_);

  navigateTo(QDir::homePath(), true);
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

void BrowserTab::showFilePane() { stack_->setCurrentWidget(view_); }

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
  view_->setRootIndex(root);

  showFilePane();
  emit locationChanged(location_);
  setTabTitleFromLocation();
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
  if (!view_->selectionModel()) return out;
  const QModelIndexList rows = view_->selectionModel()->selectedRows(0);
  out.reserve(rows.size());
  for (const QModelIndex &r : rows) out << fsModel_->filePath(r);
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
  if (!view_->selectionModel()) return {};

  QModelIndex cur = view_->currentIndex();
  if (cur.isValid()) {
    const QString p = fsModel_->filePath(cur.sibling(cur.row(), 0));
    if (QFileInfo(p).isDir()) return p;
  }

  const QModelIndexList rows = view_->selectionModel()->selectedRows(0);
  for (const QModelIndex &r : rows) {
    const QString p = fsModel_->filePath(r);
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

  const QModelIndex idx = view_->indexAt(pos);
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

  QAction *chosen = menu.exec(view_->viewport()->mapToGlobal(pos));
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
  if (obj == view_->viewport()) {
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
        const QModelIndex idx = view_->indexAt(me->position().toPoint());
        if (idx.isValid()) {
          const QString p = fsModel_->filePath(idx);
          if (QFileInfo(p).isDir()) {
            openInNewTabIfDir(p);
            return true;
          }
        }
      }
    }
  }
  return QWidget::eventFilter(obj, event);
}
