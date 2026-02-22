#include <QMainWindow>
#include <QToolBar>
#include <QLineEdit>
#include <QTabWidget>
#include <QStackedWidget>
#include <QToolButton>
#include <QSplitter>
#include <QFileSystemModel>
#include <QAction>
#include <QMessageBox>
#include <QKeySequence>
#include <QShortcut>
#include <QGuiApplication>
#include <QClipboard>
#include <QFileInfo>
#include <QFile>
#include <QDir>
#include <QProgressDialog>
#include <QMenuBar>
#include <QMenu>
#include <QAction>
#include <QActionGroup>
#include <QKeySequence>
#include <QInputDialog>
#include <QSignalBlocker>
#include <QLabel>
#include <QStatusBar>
#include <QStorageInfo>
#include <QStringList>
#include <QStyle>

#include "iconutil.h"
#include "terminal.h"
#include "places.h"
#include "browsertab.h"
#include "jobmanager.h"
#include "propertiesdialog.h"
#include "openwithdialog.h"
#include "breadcrumbbar.h"
#include "mainwindow.h"

MainWindow::MainWindow(QWidget *parent)
  : QMainWindow(parent) {
  setWindowTitle("sfm");
  resize(1250, 760);

  fsModel_ = new QFileSystemModel(this);
  fsModel_->setFilter(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::AllDirs | QDir::Hidden);
  fsModel_->setReadOnly(false);
  fsModel_->setRootPath("/");

  jobs_ = new JobManager(this);
  qRegisterMetaType<JobManager::Job>("JobManager::Job");

  progress_ = new QProgressDialog(this);
  progress_->setWindowTitle("File Operation");
  progress_->setLabelText("Working…");
  progress_->setCancelButtonText("Cancel");
  progress_->setMinimumDuration(250);
  progress_->setAutoClose(true);
  progress_->setAutoReset(true);
  progress_->hide();

  connect(progress_, &QProgressDialog::canceled, this, [this]{ jobs_->cancelAll(); });

  connect(jobs_, &JobManager::jobStarted, this, [this](const JobManager::Job &job, int){
    bytesTotal_ = -1;
    bytesDone_ = 0;
    progress_->setWindowTitle(job.displayName);
    progress_->setLabelText(job.displayName);
    progress_->setRange(0, 1000);
    progress_->setValue(0);
    progress_->show();
  });

  connect(jobs_, &JobManager::jobBytesTotal, this, [this](const JobManager::Job &, qint64 totalBytes){
    bytesTotal_ = totalBytes;
    bytesDone_ = 0;
  });

  connect(jobs_, &JobManager::jobBytesProgress, this, [this](const JobManager::Job &, const QString &cur, qint64 done, qint64 total){
    bytesDone_ = done;
    bytesTotal_ = total;

    int v = 0;
    if (total > 0) {
      double ratio = (double)done / (double)total;
      if (ratio < 0) ratio = 0;
      if (ratio > 1) ratio = 1;
      v = (int)(ratio * 1000.0);
    }

    QString label = cur;
    if (total > 0) label += "\n" + humanBytes(done) + " / " + humanBytes(total);
    progress_->setLabelText(label);
    progress_->setValue(v);
  });

  connect(jobs_, &JobManager::jobProgress, this, [this](const JobManager::Job &, const QString &cur, int doneItems, int totalItems){
    if (bytesTotal_ > 0) return;
    int v = 0;
    if (totalItems > 0) {
      double ratio = (double)doneItems / (double)totalItems;
      if (ratio < 0) ratio = 0;
      if (ratio > 1) ratio = 1;
      v = (int)(ratio * 1000.0);
    }
    progress_->setLabelText(cur);
    progress_->setValue(v);
  });

  connect(jobs_, &JobManager::jobFinished, this, [this](const JobManager::Job &job, bool ok, const QString &err){
    if (!ok && err != "Cancelled.") QMessageBox::warning(this, job.displayName + " failed", err);
    refresh();
  });

  connect(jobs_, &JobManager::idle, this, [this]{
    progress_->reset();
    if (places_) places_->refresh();
  });

  // Toolbar
  auto *tb = addToolBar("Navigation");
  tb->setMovable(false);

  actBack_ = tb->addAction("Back");
  actBack_->setIcon(IconUtil::fromTheme(QStringList{"go-previous", "back"}, {},
    QStyle::SP_ArrowBack, this));
  actBack_->setShortcut(QKeySequence::Back);
  connect(actBack_, &QAction::triggered, this, [this]{ currentTab()->goBack(); syncUiFromTab(); });

  actForward_ = tb->addAction("Forward");
  actForward_->setIcon(IconUtil::fromTheme(QStringList{"go-next","forward"}, {},
    QStyle::SP_ArrowForward, this));
  actForward_->setShortcut(QKeySequence::Forward);
  connect(actForward_, &QAction::triggered, this, [this]{ currentTab()->goForward(); syncUiFromTab(); });

  actUp_ = tb->addAction("Up");
  actUp_->setIcon(IconUtil::fromTheme(QStringList{"go-up","up"}, {},
    QStyle::SP_ArrowUp, this));
  actUp_->setShortcut(QKeySequence(Qt::ALT | Qt::Key_Up));
  connect(actUp_, &QAction::triggered, this, [this]{ currentTab()->goUp(); syncUiFromTab(); });

  actRefresh_ = tb->addAction("Refresh");
  actRefresh_->setIcon(IconUtil::fromTheme(QStringList{"view-refresh","reload"}, {},
    QStyle::SP_BrowserReload, this));
  actRefresh_->setShortcut(QKeySequence::Refresh);
  connect(actRefresh_, &QAction::triggered, this, &MainWindow::refresh);

  tb->addSeparator();

  // Path bar (breadcrumb <-> line edit)
  pathStack_ = new QStackedWidget(this);
  pathStack_->setMinimumWidth(520);

  breadcrumbs_ = new BreadcrumbBar(pathStack_);
  address_ = new QLineEdit(pathStack_);
  address_->setClearButtonEnabled(true);
  address_->setPlaceholderText("Path or trash:///");

  pathStack_->addWidget(breadcrumbs_);
  pathStack_->addWidget(address_);
  pathStack_->setCurrentWidget(breadcrumbs_);

  tb->addWidget(pathStack_);

  connect(address_, &QLineEdit::returnPressed, this, &MainWindow::onAddressEntered);
  connect(breadcrumbs_, &BreadcrumbBar::pathActivated, this, [this](const QString &p){
    currentTab()->navigateTo(p, true);
    syncUiFromTab();
  });
  connect(breadcrumbs_, &BreadcrumbBar::openInNewTabRequested, this, [this](const QString &p){
    newTab(p);
  });
  connect(breadcrumbs_, &BreadcrumbBar::requestEdit, this, [this]{
    pathStack_->setCurrentWidget(address_);
    address_->setFocus(Qt::ShortcutFocusReason);
    address_->selectAll();
  });

  // Ctrl+L focuses the address bar; Esc returns to breadcrumbs.
  auto *scAddr = new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_L), this);
  connect(scAddr, &QShortcut::activated, this, [this]{
    pathStack_->setCurrentWidget(address_);
    address_->setFocus(Qt::ShortcutFocusReason);
    address_->selectAll();
  });
  auto *scCrumbs = new QShortcut(QKeySequence(Qt::Key_Escape), address_);
  connect(scCrumbs, &QShortcut::activated, this, [this]{
    pathStack_->setCurrentWidget(breadcrumbs_);
  });

  // Central
  auto *split = new QSplitter(this);
  split->setChildrenCollapsible(false);

  places_ = new PlacesSidebar(split);
  places_->setMinimumWidth(240);
  places_->setMaximumWidth(420);

  tabs_ = new QTabWidget(split);
  tabs_->setDocumentMode(true);
  tabs_->setMovable(true);
  tabs_->setTabsClosable(true);

  split->addWidget(places_);
  split->addWidget(tabs_);
  split->setStretchFactor(0, 0);
  split->setStretchFactor(1, 1);
  setCentralWidget(split);

  initInlineStatusBar();

  connect(tabs_, &QTabWidget::currentChanged, this, [this](int){ syncUiFromTab(); });
  connect(tabs_, &QTabWidget::tabCloseRequested, this, &MainWindow::closeTab);

  connect(places_, &PlacesSidebar::placeActivated, this, [this](const QString &path){
    currentTab()->navigateTo(path, true);
    syncUiFromTab();
  });

  connect(places_, &PlacesSidebar::emptyTrashRequested, this, &MainWindow::emptyTrashFromSidebar);

  // Tabs shortcuts
  auto *scNewTab = new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_T), this);
  connect(scNewTab, &QShortcut::activated, this, [this]{ newTab(QDir::homePath()); });

  auto *scCloseTab = new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_W), this);
  connect(scCloseTab, &QShortcut::activated, this, [this]{
    if (tabs_->count() > 1) closeTab(tabs_->currentIndex());
  });

  auto *scNextTab = new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_Tab), this);
  connect(scNextTab, &QShortcut::activated, this, [this]{
    const int n = tabs_->count();
    tabs_->setCurrentIndex((tabs_->currentIndex() + 1) % n);
  });

  auto *scPrevTab = new QShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_Tab), this);
  connect(scPrevTab, &QShortcut::activated, this, [this]{
    const int n = tabs_->count();
    tabs_->setCurrentIndex((tabs_->currentIndex() - 1 + n) % n);
  });

  // General shortcuts

  auto *scProps = new QShortcut(QKeySequence(Qt::ALT | Qt::Key_Return), this);
  connect(scProps, &QShortcut::activated, this, &MainWindow::showPropertiesForSelection);

  auto *scRename = new QShortcut(QKeySequence(Qt::Key_F2), this);
  connect(scRename, &QShortcut::activated, this, &MainWindow::renameSelected);

  auto *scNewFolder = new QShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_N), this);
  connect(scNewFolder, &QShortcut::activated, this, &MainWindow::createNewFolder);

  auto *scCopy = new QShortcut(QKeySequence::Copy, this);
  connect(scCopy, &QShortcut::activated, this, &MainWindow::copySelected);

  auto *scCut = new QShortcut(QKeySequence::Cut, this);
  connect(scCut, &QShortcut::activated, this, &MainWindow::cutSelected);

  auto *scPaste = new QShortcut(QKeySequence::Paste, this);
  connect(scPaste, &QShortcut::activated, this, &MainWindow::pasteIntoCurrentDir);

  // View mode shortcuts (Ctrl+1/2/3)
  auto *scViewGrid = new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_1), this);
  connect(scViewGrid, &QShortcut::activated, this, [this]{
    if (auto *t = currentTab()) t->setViewMode(BrowserTab::ViewMode::GridIcons);
  });

  auto *scViewList = new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_2), this);
  connect(scViewList, &QShortcut::activated, this, [this]{
    if (auto *t = currentTab()) t->setViewMode(BrowserTab::ViewMode::List);
  });

  auto *scViewCompact = new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_3), this);
  connect(scViewCompact, &QShortcut::activated, this, [this]{
    if (auto *t = currentTab()) t->setViewMode(BrowserTab::ViewMode::Compact);
  });

  auto *scDel = new QShortcut(QKeySequence(Qt::Key_Delete), this);
  connect(scDel, &QShortcut::activated, this, [this]{
    if (currentTab()->inTrash()) deleteSelectedInTrashView();
    else trashSelected();
  });

  auto *scShiftDel = new QShortcut(QKeySequence(Qt::SHIFT | Qt::Key_Delete), this);
  connect(scShiftDel, &QShortcut::activated, this, [this]{
    if (currentTab()->inTrash()) deleteSelectedInTrashView();
    else deleteSelectedPermanently();
  });

  auto *scRestore = new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_R), this);
  connect(scRestore, &QShortcut::activated, this, &MainWindow::restoreSelectedInTrashView);

  auto *scTerm = new QShortcut(QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_T), this);
  connect(scTerm, &QShortcut::activated, this, &MainWindow::openCurrentDirInTerminal);

  newTab(QDir::homePath());

  createActions();
  createMenus();
}

void MainWindow::createActions()
{
  // File
  newTabAct_ = new QAction(tr("New Tab"), this);
  newTabAct_->setIcon(IconUtil::fromTheme(
    QStringList{"tab-new", "document-new"},
    QStringList{"list-add", "add"},
    QStyle::SP_FileIcon, this));
  newTabAct_->setShortcut(QKeySequence::AddTab);
  connect(newTabAct_, &QAction::triggered, this, [this]{
    const QString loc = currentTab() ? currentTab()->location() : QDir::homePath();
    newTab(loc);
  });

  closeTabAct_ = new QAction(tr("Close Tab"), this);
  closeTabAct_->setIcon(IconUtil::fromTheme(
    QStringList{"tab-close","window-close"},
    QStringList{"process-stop","close"},
    QStyle::SP_DialogCloseButton, this));
  closeTabAct_->setShortcut(QKeySequence::Close);
  connect(closeTabAct_, &QAction::triggered, this, [this]{
    const int i = tabs_ ? tabs_->currentIndex() : -1;
    if (i >= 0) closeTab(i);
  });

  newFolderAct_ = new QAction(tr("New Folder…"), this);
  newFolderAct_->setIcon(IconUtil::fromTheme(
    QStringList{"folder-new","document-new"},
    QStringList{"list-add"},
    QStyle::SP_DirIcon, this));
  newFolderAct_->setShortcut(QKeySequence("Ctrl+Shift+N"));
  connect(newFolderAct_, &QAction::triggered, this, &MainWindow::createNewFolder);

  newDocAct_ = new QAction(tr("New Document…"), this);
  newDocAct_->setIcon(IconUtil::fromTheme(
    QStringList{"document-new", "text-x-generic"},
    QStringList{"list-add"},
    QStyle::SP_FileIcon, this));
  newDocAct_->setShortcut(QKeySequence::New);
  connect(newDocAct_, &QAction::triggered, this, &MainWindow::createEmptyDocument);

  quitAct_ = new QAction(tr("Quit"), this);
  quitAct_->setIcon(IconUtil::fromTheme(
    QStringList{"application-exit", "system-log-out"},
    QStringList{"window-close"},
    QStyle::SP_DialogCloseButton, this));
  quitAct_->setShortcut(QKeySequence::Quit);
  connect(quitAct_, &QAction::triggered, this, &QWidget::close);

  // Edit
  cutAct_ = new QAction(tr("Cut"), this);
  cutAct_->setIcon(IconUtil::fromTheme(
    QStringList{"edit-cut"},
    QStringList{"cut"},
    QStyle::SP_DialogOpenButton, this));
  cutAct_->setShortcut(QKeySequence::Cut);
  connect(cutAct_, &QAction::triggered, this, &MainWindow::cutSelected);

  copyAct_ = new QAction(tr("Copy"), this);
  copyAct_->setIcon(IconUtil::fromTheme(
    QStringList{"edit-copy"},
    QStringList{"copy"},
    QStyle::SP_DialogOpenButton, this));
  copyAct_->setShortcut(QKeySequence::Copy);
  connect(copyAct_, &QAction::triggered, this, &MainWindow::copySelected);

  pasteAct_ = new QAction(tr("Paste"), this);
  pasteAct_->setIcon(IconUtil::fromTheme(
    QStringList{"edit-paste"},
    QStringList{"paste"},
    QStyle::SP_DialogOpenButton, this));
  pasteAct_->setShortcut(QKeySequence::Paste);
  connect(pasteAct_, &QAction::triggered, this, &MainWindow::pasteIntoCurrentDir);

  renameAct_ = new QAction(tr("Rename"), this);
  renameAct_->setIcon(IconUtil::fromTheme(
    QStringList{"edit-rename","document-edit"},
    QStringList{"edit"},
    QStyle::SP_FileIcon, this));
  renameAct_->setShortcut(Qt::Key_F2);
  connect(renameAct_, &QAction::triggered, this, &MainWindow::renameSelected);

  trashAct_ = new QAction(tr("Move to Trash"), this);
  trashAct_->setIcon(IconUtil::fromTheme(
    QStringList{"user-trash","edit-delete"},
    QStringList{"trash"},
    QStyle::SP_TrashIcon, this));
  trashAct_->setShortcut(QKeySequence::Delete);
  connect(trashAct_, &QAction::triggered, this, &MainWindow::trashSelected);

  deleteAct_ = new QAction(tr("Delete Permanently"), this);
  deleteAct_->setIcon(IconUtil::fromTheme(
    QStringList{"edit-delete", "user-trash"},
    QStringList{"trash"},
    QStyle::SP_TrashIcon, this));
  deleteAct_->setShortcut(QKeySequence(Qt::SHIFT | Qt::Key_Delete));
  connect(deleteAct_, &QAction::triggered, this, &MainWindow::deleteSelectedPermanently);

  propertiesAct_ = new QAction(tr("Properties…"), this);
  propertiesAct_->setIcon(IconUtil::fromTheme(
    QStringList{"document-properties", "dialog-information"},
    QStringList{"help-about"},
    QStyle::SP_FileDialogInfoView, this));
  propertiesAct_->setShortcut(QKeySequence(Qt::ALT | Qt::Key_Return));
  connect(propertiesAct_, &QAction::triggered, this, &MainWindow::showPropertiesForSelection);

  openWithAct_ = new QAction(tr("Open With…"), this);
  openWithAct_->setIcon(IconUtil::fromTheme(
    QStringList{"open-menu-symbolic", "system-run"},
    QStringList{"application-x-executable"},
    QStyle::SP_ArrowRight, this));
  connect(openWithAct_, &QAction::triggered, this, [this]{
    if (!currentTab()) return;
    const QStringList sel = currentTab()->selectedPaths();
    if (sel.isEmpty()) return;
    OpenWithDialog dlg(sel.first(), this);
    dlg.exec();
  });

  // View
  viewModeGroup_ = new QActionGroup(this);
  viewModeGroup_->setExclusive(true);

  viewGridAct_ = new QAction(tr("Icon View"), this);
  viewGridAct_->setCheckable(true);
  viewGridAct_->setIcon(IconUtil::fromTheme(
    QStringList{"view-grid","view-grid-symbolic"},
    QStringList{"view-list-icons"},
    QStyle::SP_FileDialogDetailedView, this));
  viewGridAct_->setShortcut(QKeySequence("Ctrl+1"));
  viewModeGroup_->addAction(viewGridAct_);
  connect(viewGridAct_, &QAction::triggered, this, [this]{
    if (auto t = currentTab()) t->setViewMode(BrowserTab::ViewMode::GridIcons);
  });

  viewListAct_ = new QAction(tr("List View"), this);
  viewListAct_->setCheckable(true);
  viewListAct_->setIcon(IconUtil::fromTheme(
    QStringList{"view-list-details","view-list"},
    QStringList{"format-justify-left"},
    QStyle::SP_FileDialogListView, this));
  viewListAct_->setShortcut(QKeySequence("Ctrl+2"));
  viewModeGroup_->addAction(viewListAct_);
  connect(viewListAct_, &QAction::triggered, this, [this]{
    if (auto t = currentTab()) t->setViewMode(BrowserTab::ViewMode::List);
  });

  viewCompactAct_ = new QAction(tr("Compact View"), this);
  viewCompactAct_->setCheckable(true);
  viewCompactAct_->setIcon(IconUtil::fromTheme(
    QStringList{"view-list-text","view-list"},
    QStringList{"format-justify-left"},
    QStyle::SP_FileDialogListView, this));
  viewCompactAct_->setShortcut(QKeySequence("Ctrl+3"));
  viewModeGroup_->addAction(viewCompactAct_);
  connect(viewCompactAct_, &QAction::triggered, this, [this]{
    if (auto t = currentTab()) t->setViewMode(BrowserTab::ViewMode::Compact);
  });

  auto syncViewChecks = [this]{
    auto t = currentTab();
    if (!t) return;
    switch (t->viewMode()) {
      case BrowserTab::ViewMode::GridIcons: viewGridAct_->setChecked(true); break;
      case BrowserTab::ViewMode::List:      viewListAct_->setChecked(true); break;
      case BrowserTab::ViewMode::Compact:   viewCompactAct_->setChecked(true); break;
    }
  };
  if (tabs_) connect(tabs_, &QTabWidget::currentChanged, this, syncViewChecks);
  syncViewChecks();

  toggleHiddenAct_ = new QAction(tr("Show Hidden Files"), this);
  toggleHiddenAct_->setCheckable(true);
  toggleHiddenAct_->setShortcut(QKeySequence("Ctrl+H"));
  connect(toggleHiddenAct_, &QAction::toggled, this, [this](bool on){
    if (!fsModel_) return;
    auto f = fsModel_->filter();
    if (on) f |= QDir::Hidden;
    else    f &= ~QDir::Hidden;
    fsModel_->setFilter(f);
  });

  // Sorting (per tab, applies to active pane)
  sortKeyGroup_ = new QActionGroup(this);
  sortKeyGroup_->setExclusive(true);

  sortByNameAct_ = new QAction(tr("Sort by Name"), this);
  sortByNameAct_->setCheckable(true);
  sortKeyGroup_->addAction(sortByNameAct_);
  connect(sortByNameAct_, &QAction::triggered, this, [this]{
    if (auto *t = currentTab()) t->setSort(BrowserTab::SortKey::Name, t->sortState().order);
    syncUiFromTab();
  });

  sortBySizeAct_ = new QAction(tr("Sort by Size"), this);
  sortBySizeAct_->setCheckable(true);
  sortKeyGroup_->addAction(sortBySizeAct_);
  connect(sortBySizeAct_, &QAction::triggered, this, [this]{
    if (auto *t = currentTab()) t->setSort(BrowserTab::SortKey::Size, t->sortState().order);
    syncUiFromTab();
  });

  sortByTypeAct_ = new QAction(tr("Sort by Type"), this);
  sortByTypeAct_->setCheckable(true);
  sortKeyGroup_->addAction(sortByTypeAct_);
  connect(sortByTypeAct_, &QAction::triggered, this, [this]{
    if (auto *t = currentTab()) t->setSort(BrowserTab::SortKey::Type, t->sortState().order);
    syncUiFromTab();
  });

  sortByModifiedAct_ = new QAction(tr("Sort by Modified"), this);
  sortByModifiedAct_->setCheckable(true);
  sortKeyGroup_->addAction(sortByModifiedAct_);
  connect(sortByModifiedAct_, &QAction::triggered, this, [this]{
    if (auto *t = currentTab()) t->setSort(BrowserTab::SortKey::Modified, t->sortState().order);
    syncUiFromTab();
  });

  sortOrderGroup_ = new QActionGroup(this);
  sortOrderGroup_->setExclusive(true);

  sortAscAct_ = new QAction(tr("Ascending"), this);
  sortAscAct_->setCheckable(true);
    sortAscAct_->setIcon(IconUtil::fromTheme(
    QStringList{"view-sort-ascending", "sort-ascending"},
    QStringList{"go-up"},
    QStyle::SP_ArrowUp, this));
  sortOrderGroup_->addAction(sortAscAct_);
  connect(sortAscAct_, &QAction::triggered, this, [this]{
    if (auto *t = currentTab()) t->setSort(t->sortState().key, Qt::AscendingOrder);
    syncUiFromTab();
  });

  sortDescAct_ = new QAction(tr("Descending"), this);
  sortDescAct_->setCheckable(true);
    sortDescAct_->setIcon(IconUtil::fromTheme(
    QStringList{"view-sort-descending", "sort-descending"},
    QStringList{"go-down"},
    QStyle::SP_ArrowDown, this));
  sortOrderGroup_->addAction(sortDescAct_);
  connect(sortDescAct_, &QAction::triggered, this, [this]{
    if (auto *t = currentTab()) t->setSort(t->sortState().key, Qt::DescendingOrder);
    syncUiFromTab();
  });

  foldersFirstAct_ = new QAction(tr("Folders First"), this);
  foldersFirstAct_->setCheckable(true);
  foldersFirstAct_->setIcon(IconUtil::fromTheme(
    QStringList{"folder", "folder-symbolic"},
    QStringList{"inode-directory"},
    QStyle::SP_DirIcon, this));
  connect(foldersFirstAct_, &QAction::toggled, this, [this](bool on){
    if (auto *t = currentTab()) t->setFoldersFirst(on);
    syncUiFromTab();
  });

  // Go
  backAct_ = actBack_;
  forwardAct_ = actForward_;
  upAct_ = actUp_;
  refreshAct_ = actRefresh_;

  homeAct_ = new QAction(tr("Home"), this);
  homeAct_->setIcon(IconUtil::fromTheme(
    QStringList{"go-home", "user-home"}, {},
    QStyle::SP_DirHomeIcon, this));
  homeAct_->setShortcut(QKeySequence("Alt+Home"));
  connect(homeAct_, &QAction::triggered, this, [this]{
    if (auto t = currentTab()) t->navigateTo(QDir::homePath(), true);
    syncUiFromTab();
  });

  trashLocationAct_ = new QAction(tr("Trash"), this);
  trashLocationAct_->setIcon(IconUtil::fromTheme(
    QStringList{"user-trash", "trash"}, {},
    QStyle::SP_TrashIcon, this));
  connect(trashLocationAct_, &QAction::triggered, this, [this]{
    if (auto t = currentTab()) t->navigateTo("trash:///", true);
    syncUiFromTab();
  });

  // Tools
  openTerminalAct_ = new QAction(tr("Open Terminal Here"), this);
  openTerminalAct_->setShortcut(QKeySequence("Ctrl+Alt+T"));
  openTerminalAct_->setIcon(IconUtil::fromTheme(
    QStringList{"utilities-terminal", "terminal"},
    QStringList{"system-run"},
    QStyle::SP_ComputerIcon, this));
  connect(openTerminalAct_, &QAction::triggered, this, &MainWindow::openCurrentDirInTerminal);

  emptyTrashAct_ = new QAction(tr("Empty Trash"), this);
    emptyTrashAct_->setIcon(IconUtil::fromTheme(
    QStringList{"user-trash", "edit-clear","edit-delete"},
    QStringList{"trash"},
    QStyle::SP_TrashIcon, this));
  connect(emptyTrashAct_, &QAction::triggered, this, &MainWindow::emptyTrashFromSidebar);
}


void MainWindow::createMenus()
{
  fileMenu_  = menuBar()->addMenu(tr("&File"));
  editMenu_  = menuBar()->addMenu(tr("&Edit"));
  viewMenu_  = menuBar()->addMenu(tr("&View"));
  goMenu_    = menuBar()->addMenu(tr("&Go"));
  toolsMenu_ = menuBar()->addMenu(tr("&Tools"));
  helpMenu_  = menuBar()->addMenu(tr("&Help"));

  // File
  fileMenu_->addAction(newTabAct_);
  fileMenu_->addAction(closeTabAct_);
  fileMenu_->addSeparator();
  fileMenu_->addAction(newFolderAct_);
  fileMenu_->addAction(newDocAct_);
  fileMenu_->addSeparator();
  fileMenu_->addAction(quitAct_);

  // Edit
  editMenu_->addAction(copyAct_);
  editMenu_->addAction(cutAct_);
  editMenu_->addAction(pasteAct_);
  editMenu_->addSeparator();
  editMenu_->addAction(renameAct_);
  editMenu_->addSeparator();
  editMenu_->addAction(trashAct_);
  editMenu_->addAction(deleteAct_);
  editMenu_->addSeparator();
  editMenu_->addAction(openWithAct_);
  editMenu_->addAction(propertiesAct_);

  // View
  viewMenu_->addAction(viewGridAct_);
  viewMenu_->addAction(viewListAct_);
  viewMenu_->addAction(viewCompactAct_);

  viewMenu_->addSeparator();

  QMenu *sortByMenu = viewMenu_->addMenu(tr("Sort By"));
  sortByMenu->addAction(sortByNameAct_);
  sortByMenu->addAction(sortBySizeAct_);
  sortByMenu->addAction(sortByTypeAct_);
  sortByMenu->addAction(sortByModifiedAct_);

  QMenu *sortOrderMenu = viewMenu_->addMenu(tr("Sort Order"));
  sortOrderMenu->addAction(sortAscAct_);
  sortOrderMenu->addAction(sortDescAct_);

  viewMenu_->addAction(foldersFirstAct_);

  viewMenu_->addSeparator();
  viewMenu_->addAction(toggleHiddenAct_);

  // Go
  goMenu_->addAction(backAct_);
  goMenu_->addAction(forwardAct_);
  goMenu_->addAction(upAct_);
  goMenu_->addAction(refreshAct_);
  goMenu_->addSeparator();
  goMenu_->addAction(homeAct_);
  goMenu_->addAction(trashLocationAct_);

  // Tools
  toolsMenu_->addAction(openTerminalAct_);
  toolsMenu_->addSeparator();
  toolsMenu_->addAction(emptyTrashAct_);
}

void MainWindow::initInlineStatusBar() {
  statusItemCount_ = new QLabel(this);
  statusSelectedSize_ = new QLabel(this);
  statusFreeSpace_ = new QLabel(this);

  statusItemCount_->setText("0 items");
  statusSelectedSize_->setText("Selected: —");
  statusFreeSpace_->setText("Free: —");

  // Left-to-right: count, selected size, free space (right aligned)
  statusBar()->addWidget(statusItemCount_, 1);
  statusBar()->addWidget(statusSelectedSize_, 1);
  statusBar()->addPermanentWidget(statusFreeSpace_, 0);
}

void MainWindow::updateInlineStatusBar() {
  updateStatusItemCount();
  updateStatusSelectedSize();
  updateStatusFreeSpace();
}

void MainWindow::updateStatusItemCount() {
  if (!statusItemCount_) return;
  auto *t = currentTab();
  const int n = t ? t->itemCount() : 0;
  statusItemCount_->setText(QString::number(n) + (n == 1 ? " item" : " items"));
}

void MainWindow::updateStatusSelectedSize() {
  if (!statusSelectedSize_) return;
  auto *t = currentTab();
  if (!t) {
    statusSelectedSize_->setText("Selected: —");
    return;
  }

  const QStringList sel = t->inTrash() ? t->selectedTrashedPaths() : t->selectedPaths();
  if (sel.isEmpty()) {
    statusSelectedSize_->setText("Selected: —");
    return;
  }

  const auto info = t->selectedSizeInfo();
  if (info.pending) {
    if (info.bytes > 0) statusSelectedSize_->setText("Selected: " + humanBytes(info.bytes) + " + …");
    else statusSelectedSize_->setText("Selected: …");
    return;
  }
  statusSelectedSize_->setText("Selected: " + humanBytes(info.bytes));
}

void MainWindow::updateStatusFreeSpace() {
  if (!statusFreeSpace_) return;
  auto *t = currentTab();
  if (!t) {
    statusFreeSpace_->setText("Free: —");
    return;
  }

  const QString p = t->storagePath();
  QStorageInfo si(p);
  if (!si.isValid() || !si.isReady()) {
    statusFreeSpace_->setText("Free: —");
    return;
  }
  statusFreeSpace_->setText("Free: " + humanBytes((qint64)si.bytesAvailable()));
}

QString MainWindow::humanBytes(qint64 b) {
  const char *units[] = {"B","KiB","MiB","GiB","TiB"};
  double v = (double)b;
  int i = 0;
  while (v >= 1024.0 && i < 4) { v /= 1024.0; i++; }
  return QString::number(v, 'f', (i == 0 ? 0 : 1)) + " " + units[i];
}

BrowserTab* MainWindow::currentTab() const {
  return qobject_cast<BrowserTab*>(tabs_->currentWidget());
}

void MainWindow::newTab(const QString &startLoc) {
  auto *tab = new BrowserTab(fsModel_, this);
  const int idx = tabs_->addTab(tab, "Tab");
  tabs_->setCurrentIndex(idx);

  connect(tab, &BrowserTab::locationChanged, this, [this](const QString &){
    if (sender() == currentTab()) syncUiFromTab();
  });
  connect(tab, &BrowserTab::titleChanged, this, [this, tab](const QString &title){
    const int i = tabs_->indexOf(tab);
    if (i >= 0) tabs_->setTabText(i, title);
  });

  // Inline status bar updates (safe + slightly redundant)
  connect(tab, &BrowserTab::selectionChanged, this, [this, tab]{
    if (tab == currentTab()) updateStatusSelectedSize();
  });
  connect(tab, &BrowserTab::itemCountChanged, this, [this, tab]{
    if (tab == currentTab()) updateStatusItemCount();
  });
  connect(tab, &BrowserTab::storageChanged, this, [this, tab]{
    if (tab == currentTab()) updateStatusFreeSpace();
  });
  connect(tab, &BrowserTab::requestNavigate, this, [this](const QString &path){
    currentTab()->navigateTo(path, true);
    syncUiFromTab();
  });
  connect(tab, &BrowserTab::openFolderInNewTabRequested, this, [this](const QString &folderPath){
    newTab(folderPath);
  });
  connect(tab, &BrowserTab::propertiesRequested, this, [this](const QString &p){
    showPropertiesForPath(p);
  });
  connect(tab, &BrowserTab::openWithDialogRequested, this, [this](const QString &p){
    OpenWithDialog dlg(p, this);
    dlg.exec();
  });
  connect(tab, &BrowserTab::openWithAppRequested, this, [this](const QString &desktopId, const QString &filePath){
    QString err;
    if (!OpenWithDialog::launchWithDesktopId(desktopId, filePath, &err)) {
      QMessageBox::warning(this, "Open With", err);
    }
  });
connect(tab, &BrowserTab::createNewFolderRequested, this, [this]{
  createNewFolder();
});
connect(tab, &BrowserTab::createNewDocumentRequested, this, [this]{
  createEmptyDocument();
});


  tab->navigateTo(startLoc, true);
  syncUiFromTab();
}

void MainWindow::closeTab(int index) {
  if (tabs_->count() <= 1) return;
  QWidget *w = tabs_->widget(index);
  tabs_->removeTab(index);
  delete w;
  syncUiFromTab();
}

void MainWindow::syncUiFromTab() {
  auto *t = currentTab();
  if (!t) return;

  // Always keep the inline status bar current.
  updateInlineStatusBar();

  // Location UI should update even during early construction.
  const QString loc = t->location();
  if (address_->text() != loc) address_->setText(loc);
  breadcrumbs_->setLocation(loc);

  // Actions may not exist yet during MainWindow construction.
  if (!viewListAct_ || !viewGridAct_ || !viewCompactAct_) return;
  if (!sortByNameAct_ || !sortBySizeAct_ || !sortByTypeAct_ || !sortByModifiedAct_) return;
  if (!sortAscAct_ || !sortDescAct_ || !foldersFirstAct_) return;

  actBack_->setEnabled(t->canGoBack());
  actForward_->setEnabled(t->canGoForward());
  actUp_->setEnabled(t->canGoUp());

  // View mode checks (per tab + active pane)
  switch (t->viewMode()) {
    case BrowserTab::ViewMode::GridIcons: viewGridAct_->setChecked(true); break;
    case BrowserTab::ViewMode::List:      viewListAct_->setChecked(true); break;
    case BrowserTab::ViewMode::Compact:   viewCompactAct_->setChecked(true); break;
  }

  // Sort checks (per tab + active pane)
  const auto st = t->sortState();
  auto setCheckedNoSignal = [](QAction *a, bool on) {
    if (!a) return;
    const QSignalBlocker b(a);
    a->setChecked(on);
  };

  setCheckedNoSignal(sortByNameAct_,     st.key == BrowserTab::SortKey::Name);
  setCheckedNoSignal(sortBySizeAct_,     st.key == BrowserTab::SortKey::Size);
  setCheckedNoSignal(sortByTypeAct_,     st.key == BrowserTab::SortKey::Type);
  setCheckedNoSignal(sortByModifiedAct_, st.key == BrowserTab::SortKey::Modified);

  setCheckedNoSignal(sortAscAct_,  st.order == Qt::AscendingOrder);
  setCheckedNoSignal(sortDescAct_, st.order == Qt::DescendingOrder);

  setCheckedNoSignal(foldersFirstAct_, st.foldersFirst);
}


void MainWindow::onAddressEntered() {
  const QString raw = address_->text().trimmed();
  if (raw.isEmpty()) return;

  QString loc = raw;
  if (!loc.startsWith('/') && !loc.startsWith("trash://")) {
    const QString base = currentTab()->inTrash() ? QDir::homePath() : currentTab()->location();
    loc = QDir(base).absoluteFilePath(loc);
  }

  if (loc.startsWith("trash://")) {
    currentTab()->navigateTo("trash:///", true);
    syncUiFromTab();
    pathStack_->setCurrentWidget(breadcrumbs_);
    return;
  }

  QFileInfo info(loc);
  if (!info.exists() || !info.isDir()) {
    QMessageBox::warning(this, "Invalid path", "Not a directory:\n" + loc);
    address_->setText(currentTab()->location());
    return;
  }

  currentTab()->navigateTo(QDir(loc).absolutePath(), true);
  syncUiFromTab();
  pathStack_->setCurrentWidget(breadcrumbs_);
}

void MainWindow::showPropertiesForPath(const QString &path) {
  if (path.isEmpty()) return;
  auto *dlg = new PropertiesDialog(path, this);
  dlg->setAttribute(Qt::WA_DeleteOnClose, true);
  dlg->show();
}

void MainWindow::showPropertiesForSelection() {
  BrowserTab *t = currentTab();
  if (!t || t->inTrash()) return;
  const QStringList sel = t->selectedPaths();
  if (sel.size() != 1) return;
  showPropertiesForPath(sel.front());
}

void MainWindow::copySelected() {
  BrowserTab *t = currentTab();
  QStringList paths = t->inTrash() ? t->selectedTrashedPaths() : t->selectedPaths();
  if (paths.isEmpty()) return;

  clipboard_.mode = ClipMode::Copy;
  clipboard_.paths = paths;
  QGuiApplication::clipboard()->setText(paths.join('\n'));
}

void MainWindow::cutSelected() {
  if (currentTab()->inTrash()) return;
  const QStringList paths = currentTab()->selectedPaths();
  if (paths.isEmpty()) return;

  clipboard_.mode = ClipMode::Cut;
  clipboard_.paths = paths;
  QGuiApplication::clipboard()->setText(paths.join('\n'));
}

void MainWindow::pasteIntoCurrentDir() {
  if (currentTab()->inTrash()) return;
  if (!clipboard_.hasData()) return;

  const QString destDir = currentTab()->location();

  if (clipboard_.mode == ClipMode::Copy) {
    jobs_->enqueueCopy(clipboard_.paths, destDir, "Copy");
  } else {
    jobs_->enqueueMove(clipboard_.paths, destDir, "Move");
    clipboard_.clear();
  }
}

void MainWindow::renameSelected() {
  if (!currentTab()) return;
  currentTab()->beginInlineRename();
}

void MainWindow::createNewFolder() {
  if (currentTab()->inTrash()) return;

  bool ok = false;
  const QString name = QInputDialog::getText(
      this, "New Folder", "Folder name:", QLineEdit::Normal, "New Folder", &ok);
  if (!ok) return;

  const QString trimmed = name.trimmed();
  if (trimmed.isEmpty()) return;

  QDir d(currentTab()->location());
  const QString path = d.filePath(trimmed);
  if (QFileInfo::exists(path)) {
    QMessageBox::warning(this, "New Folder failed", "Already exists:\n" + path);
    return;
  }
  if (!d.mkdir(trimmed)) {
    QMessageBox::warning(this, "New Folder failed", "Could not create:\n" + path);
    return;
  }
  refresh();
}

void MainWindow::createEmptyDocument() {
  if (!currentTab() || currentTab()->inTrash()) return;

  bool ok = false;
  const QString name = QInputDialog::getText(
      this, "New Document", "File name:", QLineEdit::Normal, "New Document.txt", &ok);
  if (!ok) return;

  const QString trimmed = name.trimmed();
  if (trimmed.isEmpty()) return;

  QDir d(currentTab()->location());
  const QString path = d.filePath(trimmed);
  if (QFileInfo::exists(path)) {
    QMessageBox::warning(this, "New Document failed", "Already exists:\n" + path);
    return;
  }

  QFile f(path);
  if (!f.open(QIODevice::WriteOnly)) {
    QMessageBox::warning(this, "New Document failed", "Could not create:\n" + path);
    return;
  }
  f.close();

  refresh();
}

void MainWindow::trashSelected() {
  const QStringList paths = currentTab()->selectedPaths();
  if (paths.isEmpty()) return;

  const auto resp = QMessageBox::question(
      this, "Move to Trash",
      "Move selected item(s) to Trash?\n\n(" + QString::number(paths.size()) + " item(s))",
      QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes);

  if (resp != QMessageBox::Yes) return;
  jobs_->enqueueTrash(paths, "Move to Trash");
}

void MainWindow::deleteSelectedPermanently() {
  const QStringList paths = currentTab()->selectedPaths();
  if (paths.isEmpty()) return;

  const auto resp = QMessageBox::warning(
      this, "Delete Permanently",
      "This will permanently delete the selected item(s).\n"
      "This cannot be undone.\n\n"
      "Delete " + QString::number(paths.size()) + " item(s)?",
      QMessageBox::Yes | QMessageBox::No, QMessageBox::No);

  if (resp != QMessageBox::Yes) return;
  jobs_->enqueueDelete(paths, "Delete Permanently");
}

void MainWindow::deleteSelectedInTrashView() {
  BrowserTab *t = currentTab();
  const QStringList sel = t->selectedTrashedPaths();
  if (sel.isEmpty()) return;

  const auto resp = QMessageBox::warning(
      this, "Delete from Trash",
      "Permanently delete the selected item(s) from Trash?\nThis cannot be undone.\n\n"
      "Delete " + QString::number(sel.size()) + " item(s)?",
      QMessageBox::Yes | QMessageBox::No, QMessageBox::No);

  if (resp != QMessageBox::Yes) return;

  QString err;
  if (!t->trashDeleteSelected(&err)) QMessageBox::warning(this, "Delete failed", err);
  refresh();
}

void MainWindow::restoreSelectedInTrashView() {
  BrowserTab *t = currentTab();
  if (!t->inTrash()) return;

  const QStringList sel = t->selectedTrashedPaths();
  if (sel.isEmpty()) return;

  const auto resp = QMessageBox::question(
      this, "Restore",
      "Restore selected item(s) to their original location(s)?\n\n"
      "Restore " + QString::number(sel.size()) + " item(s)?",
      QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes);

  if (resp != QMessageBox::Yes) return;

  QString err;
  if (!t->trashRestoreSelected(&err)) QMessageBox::warning(this, "Restore failed", err);
  refresh();
}

void MainWindow::refresh() {
  currentTab()->refresh();
  syncUiFromTab();
}

void MainWindow::openCurrentDirInTerminal() {
  const QString dir = currentTab()->inTrash() ? QDir::homePath() : currentTab()->location();
  QString err;
  if (!Terminal::openInTerminal(dir, &err)) QMessageBox::warning(this, "Terminal", err);
}

void MainWindow::emptyTrashFromSidebar() {
  currentTab()->navigateTo("trash:///", true);
  syncUiFromTab();

  const auto resp = QMessageBox::warning(
      this, "Empty Trash",
      "This will permanently delete everything in Trash.\nThis cannot be undone.\n\nEmpty Trash?",
      QMessageBox::Yes | QMessageBox::No, QMessageBox::No);

  if (resp != QMessageBox::Yes) return;

  const QString xdgData = qEnvironmentVariableIsSet("XDG_DATA_HOME")
      ? qEnvironmentVariable("XDG_DATA_HOME")
      : (QDir::homePath() + "/.local/share");
  const QString filesDir = xdgData + "/Trash/files";
  const QString infoDir  = xdgData + "/Trash/info";

  auto rm = [](const QString &p) -> bool {
    QDir d(p);
    if (!d.exists()) return true;
    return d.removeRecursively();
  };

  if (!rm(filesDir) || !rm(infoDir) || !QDir().mkpath(filesDir) || !QDir().mkpath(infoDir)) {
    QMessageBox::warning(this, "Empty Trash failed",
                         "Failed to empty Trash directories.\nCheck permissions and try again.");
  }

  refresh();
}