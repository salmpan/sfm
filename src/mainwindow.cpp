#include <QMainWindow>
#include <QToolBar>
#include <QLineEdit>
#include <QTabWidget>
#include <QSplitter>
#include <QFileSystemModel>
#include <QAction>
#include <QMessageBox>
#include <QKeySequence>
#include <QShortcut>
#include <QGuiApplication>
#include <QClipboard>
#include <QInputDialog>
#include <QFileInfo>
#include <QDir>
#include <QProgressDialog>

#include "terminal.h"
#include "places.h"
#include "browsertab.h"
#include "jobmanager.h"
#include "propertiesdialog.h"
#include "openwithdialog.h"
#include "mainwindow.h"

MainWindow::MainWindow(QWidget *parent)
  : QMainWindow(parent) {
  setWindowTitle("sfm");
  resize(1250, 760);

  fsModel_ = new QFileSystemModel(this);
  fsModel_->setFilter(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::AllDirs | QDir::Hidden);
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
  actBack_->setShortcut(QKeySequence::Back);
  connect(actBack_, &QAction::triggered, this, [this]{ currentTab()->goBack(); syncUiFromTab(); });

  actForward_ = tb->addAction("Forward");
  actForward_->setShortcut(QKeySequence::Forward);
  connect(actForward_, &QAction::triggered, this, [this]{ currentTab()->goForward(); syncUiFromTab(); });

  actUp_ = tb->addAction("Up");
  actUp_->setShortcut(QKeySequence(Qt::ALT | Qt::Key_Up));
  connect(actUp_, &QAction::triggered, this, [this]{ currentTab()->goUp(); syncUiFromTab(); });

  actRefresh_ = tb->addAction("Refresh");
  actRefresh_->setShortcut(QKeySequence::Refresh);
  connect(actRefresh_, &QAction::triggered, this, &MainWindow::refresh);

  tb->addSeparator();

  address_ = new QLineEdit(this);
  address_->setClearButtonEnabled(true);
  address_->setPlaceholderText("Path or trash:///");
  address_->setMinimumWidth(520);
  tb->addWidget(address_);
  connect(address_, &QLineEdit::returnPressed, this, &MainWindow::onAddressEntered);

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
  auto *scFocusPath = new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_L), this);
  connect(scFocusPath, &QShortcut::activated, this, [this]{
    address_->setFocus();
    address_->selectAll();
  });

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
  BrowserTab *t = currentTab();
  if (!t) return;

  address_->setText(t->location());
  actBack_->setEnabled(t->canGoBack());
  actForward_->setEnabled(t->canGoForward());
  actUp_->setEnabled(t->canGoUp());

  if (places_) places_->refresh();
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
  if (currentTab()->inTrash()) return;

  const QStringList paths = currentTab()->selectedPaths();
  if (paths.size() != 1) return;

  const QString oldPath = paths.front();
  const QFileInfo fi(oldPath);
  const QString dir = fi.absolutePath();

  bool ok = false;
  const QString newName = QInputDialog::getText(
      this, "Rename", "New name:", QLineEdit::Normal, fi.fileName(), &ok);
  if (!ok) return;

  const QString trimmed = newName.trimmed();
  if (trimmed.isEmpty() || trimmed == fi.fileName()) return;

  const QString newPath = QDir(dir).filePath(trimmed);
  if (QFileInfo::exists(newPath)) {
    QMessageBox::warning(this, "Rename failed", "Target already exists:\n" + newPath);
    return;
  }

  if (!QFile::rename(oldPath, newPath)) {
    QMessageBox::warning(this, "Rename failed", "Could not rename:\n" + oldPath);
    return;
  }

  refresh();
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
