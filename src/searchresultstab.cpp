#include "searchresultstab.h"

#include "searchresultsmodel.h"

#include <QAction>
#include <QBoxLayout>
#include <QDirIterator>
#include <QFileInfo>
#include <QFontMetrics>
#include <QHeaderView>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QGuiApplication>
#include <QClipboard>
#include <QLineEdit>
#include <QLabel>
#include <QMenu>
#include <QProcess>
#include <QPushButton>
#include <QCheckBox>
#include <QComboBox>
#include <QFileDialog>
#include <QRegularExpression>
#include <QSortFilterProxyModel>
#include <QStandardPaths>
#include <QSettings>
#include <QTableView>
#include <QTimer>
#include <QToolButton>
#include <QtConcurrent>

SearchResultsTab::SearchResultsTab(const SearchOptions &opt, QWidget *parent)
  : QWidget(parent), opt_(opt)
{
  // Restore last-used mode/flags.
  loadPersistedOptions_();

  // Root + query line
  rootLabel_ = new QLabel(this);
  rootLabel_->setTextInteractionFlags(Qt::TextSelectableByMouse);

  chooseRootBtn_ = new QToolButton(this);
  chooseRootBtn_->setText(tr("Choose…"));
  connect(chooseRootBtn_, &QToolButton::clicked, this, &SearchResultsTab::onChooseRoot_);

  useCurrentFolderBtn_ = new QPushButton(tr("Search in current folder"), this);
  useCurrentFolderBtn_->setEnabled(false);
  connect(useCurrentFolderBtn_, &QPushButton::clicked, this, [this]{
    if (currentFolderPath_.isEmpty()) return;
    setRootPath(currentFolderPath_);
    scheduleRun_();
  });

  currentFolderLabel_ = new QLabel(this);
  currentFolderLabel_->setTextInteractionFlags(Qt::TextSelectableByMouse);
  currentFolderLabel_->setToolTip(tr("Updates when you switch tabs"));

  queryEdit_ = new QLineEdit(this);
  queryEdit_->setPlaceholderText(tr("Search…"));
  queryEdit_->setClearButtonEnabled(true);
  queryEdit_->setText(opt_.query);

  modeCombo_ = new QComboBox(this);
  modeCombo_->addItem(tr("Name"), static_cast<int>(SearchOptions::Mode::Name));
  modeCombo_->addItem(tr("Content"), static_cast<int>(SearchOptions::Mode::Content));
  modeCombo_->setCurrentIndex(opt_.mode == SearchOptions::Mode::Content ? 1 : 0);

  runBtn_ = new QToolButton(this);
  runBtn_->setText(tr("Run"));
  connect(runBtn_, &QToolButton::clicked, this, &SearchResultsTab::runNow_);

  closeBtn_ = new QToolButton(this);
  closeBtn_->setText(tr("Hide"));
  connect(closeBtn_, &QToolButton::clicked, this, &SearchResultsTab::requestCloseMe);

  debounce_ = new QTimer(this);
  debounce_->setSingleShot(true);
  debounce_->setInterval(300);
  connect(debounce_, &QTimer::timeout, this, &SearchResultsTab::runNow_);

  connect(queryEdit_, &QLineEdit::textChanged, this, &SearchResultsTab::scheduleRun_);
  connect(modeCombo_, qOverload<int>(&QComboBox::currentIndexChanged), this, &SearchResultsTab::scheduleRun_);

  // Options
  caseChk_ = new QCheckBox(tr("Case"), this);
  caseChk_->setChecked(opt_.caseSensitive);
  connect(caseChk_, &QCheckBox::toggled, this, &SearchResultsTab::scheduleRun_);

  regexChk_ = new QCheckBox(tr("Regex"), this);
  regexChk_->setChecked(opt_.useRegex);
  connect(regexChk_, &QCheckBox::toggled, this, &SearchResultsTab::scheduleRun_);

  hiddenChk_ = new QCheckBox(tr("Hidden"), this);
  hiddenChk_->setChecked(opt_.includeHidden);
  connect(hiddenChk_, &QCheckBox::toggled, this, &SearchResultsTab::scheduleRun_);

  dirsChk_ = new QCheckBox(tr("Dirs"), this);
  dirsChk_->setChecked(opt_.includeDirectories);
  connect(dirsChk_, &QCheckBox::toggled, this, &SearchResultsTab::scheduleRun_);

  followChk_ = new QCheckBox(tr("Follow symlinks"), this);
  followChk_->setChecked(opt_.followSymlinks);
  connect(followChk_, &QCheckBox::toggled, this, &SearchResultsTab::scheduleRun_);

  binaryChk_ = new QCheckBox(tr("Binary"), this);
  binaryChk_->setChecked(opt_.searchBinary);
  connect(binaryChk_, &QCheckBox::toggled, this, &SearchResultsTab::scheduleRun_);

  summary_ = new QLabel(this);
  summary_->setTextInteractionFlags(Qt::TextSelectableByMouse);

  cancelBtn_ = new QPushButton(tr("Cancel"), this);
  connect(cancelBtn_, &QPushButton::clicked, this, &SearchResultsTab::cancel);

  filterEdit_ = new QLineEdit(this);
  filterEdit_->setPlaceholderText(tr("Filter results…"));

  // Layout: root row
  auto *rootRow = new QHBoxLayout();
  rootRow->addWidget(new QLabel(tr("In:"), this));
  rootRow->addWidget(rootLabel_, 1);
  rootRow->addWidget(chooseRootBtn_, 0);
  rootRow->addSpacing(8);
  rootRow->addWidget(useCurrentFolderBtn_, 0);
  rootRow->addWidget(currentFolderLabel_, 1);

  // Query row
  auto *queryRow = new QHBoxLayout();
  queryRow->addWidget(new QLabel(tr("Find:"), this));
  queryRow->addWidget(queryEdit_, 1);
  queryRow->addWidget(modeCombo_, 0);
  queryRow->addWidget(runBtn_, 0);
  queryRow->addWidget(cancelBtn_, 0);
  queryRow->addWidget(closeBtn_, 0);

  // Options row
  auto *optRow = new QHBoxLayout();
  optRow->addWidget(caseChk_);
  optRow->addWidget(regexChk_);
  optRow->addWidget(hiddenChk_);
  optRow->addWidget(dirsChk_);
  optRow->addWidget(followChk_);
  optRow->addWidget(binaryChk_);
  optRow->addStretch(1);

  model_ = new SearchResultsModel(this);
  proxy_ = new QSortFilterProxyModel(this);
  proxy_->setSourceModel(model_);
  proxy_->setFilterCaseSensitivity(Qt::CaseInsensitive);
  proxy_->setFilterKeyColumn(SearchResultsModel::Name);
  connect(filterEdit_, &QLineEdit::textChanged, proxy_, &QSortFilterProxyModel::setFilterFixedString);

  view_ = new QTableView(this);
  view_->setModel(proxy_);
  view_->setSelectionBehavior(QAbstractItemView::SelectRows);
  view_->setSelectionMode(QAbstractItemView::ExtendedSelection);
  view_->setSortingEnabled(true);
  view_->horizontalHeader()->setStretchLastSection(true);
  view_->horizontalHeader()->setSectionResizeMode(SearchResultsModel::Name, QHeaderView::ResizeToContents);
  view_->horizontalHeader()->setSectionResizeMode(SearchResultsModel::Line, QHeaderView::ResizeToContents);
  view_->horizontalHeader()->setSectionResizeMode(SearchResultsModel::Size, QHeaderView::ResizeToContents);
  view_->horizontalHeader()->setSectionResizeMode(SearchResultsModel::Modified, QHeaderView::ResizeToContents);
  view_->setContextMenuPolicy(Qt::CustomContextMenu);
  connect(view_, &QTableView::doubleClicked, this, &SearchResultsTab::onActivated_);
  connect(view_, &QTableView::customContextMenuRequested, this, &SearchResultsTab::onContextMenu_);

  auto *layout = new QVBoxLayout(this);
  layout->addLayout(rootRow);
  layout->addLayout(queryRow);
  layout->addLayout(optRow);
  layout->addWidget(summary_);
  layout->addWidget(filterEdit_);
  layout->addWidget(view_, 1);
  setLayout(layout);

  connect(&nameWatcher_, &QFutureWatcher<void>::finished, this, [this]{
    if (cancelFlag_.loadAcquire()) finish_(tr("Search canceled"));
    else finish_(tr("Search complete — %1 result(s)").arg(model_->rowCount()));
  });

  setRootPath(opt_.rootPath);
  updateSummary_();
  // Don't auto-run on construction if query empty; otherwise start immediately.
  if (!opt_.query.trimmed().isEmpty()) start_();
}

SearchResultsTab::~SearchResultsTab() {
  cancel();
}

QString SearchResultsTab::title() const {
  const QString q = opt_.query.trimmed();
  const QString root = QFileInfo(opt_.rootPath).fileName();
  const QString mode = (opt_.mode == SearchOptions::Mode::Content) ? tr("Content") : tr("Name");
  return tr("Find: %1 (%2)").arg(q.left(24)).arg(mode);
}

void SearchResultsTab::setRootPath(const QString &rootPath) {
  opt_.rootPath = rootPath;
  const QString elided = QFontMetrics(rootLabel_->font()).elidedText(rootPath, Qt::ElideMiddle, 800);
  rootLabel_->setText(elided);
  rootLabel_->setToolTip(rootPath);
  updateSummary_();
}

void SearchResultsTab::setCurrentFolderSuggestion(const QString &folderPath) {
  currentFolderPath_ = folderPath;
  useCurrentFolderBtn_->setEnabled(!currentFolderPath_.isEmpty());

  const QString shown = currentFolderPath_.isEmpty() ? tr("(unavailable)") : currentFolderPath_;
  const QString elided = QFontMetrics(currentFolderLabel_->font()).elidedText(shown, Qt::ElideMiddle, 520);
  currentFolderLabel_->setText(elided);
  currentFolderLabel_->setToolTip(shown);
}

void SearchResultsTab::focusQuery() {
  if (!queryEdit_) return;
  queryEdit_->setFocus();
  queryEdit_->selectAll();
}

void SearchResultsTab::scheduleRun_() {
  cancelBtn_->setEnabled(false);
  rebuildOptionsFromUi_();
  persistOptions_();
  if (debounce_) debounce_->start();
}

void SearchResultsTab::runNow_() {
  if (debounce_) debounce_->stop();
  rebuildOptionsFromUi_();

  const QString q = opt_.query.trimmed();
  if (q.isEmpty()) {
    cancel();
    model_->clear();
    summary_->setText(tr("Enter a query to search in %1").arg(opt_.rootPath));
    return;
  }

  cancel();
  cancelBtn_->setEnabled(true);
  start_();
}

void SearchResultsTab::onChooseRoot_() {
  const QString dir = QFileDialog::getExistingDirectory(this, tr("Choose folder"), opt_.rootPath);
  if (dir.isEmpty()) return;
  setRootPath(dir);
  scheduleRun_();
}

void SearchResultsTab::rebuildOptionsFromUi_() {
  opt_.query = queryEdit_ ? queryEdit_->text() : opt_.query;
  if (modeCombo_) {
    const int v = modeCombo_->currentData().toInt();
    opt_.mode = static_cast<SearchOptions::Mode>(v);
  }
  if (caseChk_) opt_.caseSensitive = caseChk_->isChecked();
  if (regexChk_) opt_.useRegex = regexChk_->isChecked();
  if (hiddenChk_) opt_.includeHidden = hiddenChk_->isChecked();
  if (dirsChk_) opt_.includeDirectories = dirsChk_->isChecked();
  if (followChk_) opt_.followSymlinks = followChk_->isChecked();
  if (binaryChk_) opt_.searchBinary = binaryChk_->isChecked();
}

void SearchResultsTab::loadPersistedOptions_() {
  QSettings s;
  s.beginGroup("search");
  if (s.contains("mode")) opt_.mode = static_cast<SearchOptions::Mode>(s.value("mode").toInt());
  if (s.contains("case")) opt_.caseSensitive = s.value("case").toBool();
  if (s.contains("regex")) opt_.useRegex = s.value("regex").toBool();
  if (s.contains("hidden")) opt_.includeHidden = s.value("hidden").toBool();
  if (s.contains("dirs")) opt_.includeDirectories = s.value("dirs").toBool();
  if (s.contains("follow")) opt_.followSymlinks = s.value("follow").toBool();
  if (s.contains("binary")) opt_.searchBinary = s.value("binary").toBool();
  s.endGroup();
}

void SearchResultsTab::persistOptions_() const {
  QSettings s;
  s.beginGroup("search");
  s.setValue("mode", static_cast<int>(opt_.mode));
  s.setValue("case", opt_.caseSensitive);
  s.setValue("regex", opt_.useRegex);
  s.setValue("hidden", opt_.includeHidden);
  s.setValue("dirs", opt_.includeDirectories);
  s.setValue("follow", opt_.followSymlinks);
  s.setValue("binary", opt_.searchBinary);
  s.endGroup();
}

void SearchResultsTab::start_() {
  cancelFlag_.storeRelease(0);
  model_->clear();
  rgBuf_.clear();

  if (opt_.useRegex) {
    QRegularExpression::PatternOptions rxOpts = QRegularExpression::NoPatternOption;
    if (!opt_.caseSensitive) rxOpts |= QRegularExpression::CaseInsensitiveOption;
    const QRegularExpression rx(opt_.query, rxOpts);
    if (!rx.isValid()) {
      finish_(tr("Invalid regular expression: %1").arg(rx.errorString()));
      return;
    }
  }

  if (opt_.mode == SearchOptions::Mode::Content) startContentSearch_();
  else startNameSearch_();
}

void SearchResultsTab::cancel() {
  cancelFlag_.storeRelease(1);
  if (rg_) {
    rg_->kill();
    rg_->deleteLater();
    rg_ = nullptr;
  }
  if (nameWatcher_.isRunning()) {
    nameWatcher_.cancel();
    nameWatcher_.waitForFinished();
  }
  cancelBtn_->setEnabled(false);
  updateSummary_();
}

void SearchResultsTab::finish_(const QString &status) {
  cancelBtn_->setEnabled(false);
  summary_->setText(status);
}

void SearchResultsTab::updateSummary_() {
  const int n = model_->rowCount();
  const QString root = opt_.rootPath;
  QString s = tr("%1 result(s) in %2").arg(n).arg(root);
  if (cancelFlag_.loadAcquire()) s += tr(" — canceled");
  summary_->setText(s);
}

void SearchResultsTab::addResult_(const QString &path, bool isDir, int line, int col, const QString &preview) {
  SearchResultItem it;
  it.path = path;
  it.isDir = isDir;
  it.line = line;
  it.column = col;
  it.preview = preview;

  QFileInfo fi(path);
  if (fi.exists()) {
    it.size = fi.isDir() ? -1 : fi.size();
    it.modified = fi.lastModified();
  }
  model_->addResult(it);
  updateSummary_();
}

void SearchResultsTab::startNameSearch_() {
  cancelBtn_->setEnabled(true);
  const QString root = opt_.rootPath;
  const QString query = opt_.query;
  const bool caseSensitive = opt_.caseSensitive;
  const bool useRegex = opt_.useRegex;
  const bool includeHidden = opt_.includeHidden;
  const bool includeDirectories = opt_.includeDirectories;
  const bool followSymlinks = opt_.followSymlinks;

  const Qt::CaseSensitivity cs = caseSensitive ? Qt::CaseSensitive : Qt::CaseInsensitive;
  QRegularExpression rx;
  if (useRegex) {
    QRegularExpression::PatternOptions opts = QRegularExpression::NoPatternOption;
    if (!caseSensitive) opts |= QRegularExpression::CaseInsensitiveOption;
    rx = QRegularExpression(query, opts);
  }

  auto future = QtConcurrent::run([this, root, query, cs, rx, useRegex, includeHidden, includeDirectories, followSymlinks]{
    QDirIterator::IteratorFlags itFlags;

    if (followSymlinks) itFlags |= QDirIterator::FollowSymlinks;

    QDirIterator it(root, QDir::AllEntries | QDir::NoDotAndDotDot, itFlags | QDirIterator::Subdirectories);
    while (it.hasNext()) {
      if (cancelFlag_.loadAcquire()) return;
      const QString p = it.next();
      const QFileInfo fi = it.fileInfo();

      if (!includeHidden) {
        if (fi.fileName().startsWith('.')) continue;
        // also skip hidden components (cheap):
        if (p.contains("/.") || p.contains("\\.")) continue;
      }

      if (fi.isDir() && !includeDirectories) continue;

      bool match = false;
      if (useRegex) match = rx.isValid() && rx.match(fi.fileName()).hasMatch();
      else match = fi.fileName().contains(query, cs);

      if (match) {
        QMetaObject::invokeMethod(this, [this, p, isDir = fi.isDir()](){
          addResult_(p, isDir, 0, 0, QString());
        }, Qt::QueuedConnection);
      }
    }
  });

  nameWatcher_.setFuture(future);
}

static bool hasProgramOnPath(const QString &name) {
  const QString p = QStandardPaths::findExecutable(name);
  return !p.isEmpty();
}

void SearchResultsTab::startContentSearch_() {
  cancelBtn_->setEnabled(true);
  if (!hasProgramOnPath("rg")) {
    finish_(tr("ripgrep (rg) not found in PATH"));
    cancelBtn_->setEnabled(false);
    return;
  }

  rg_ = new QProcess(this);
  rg_->setProgram("rg");
  QStringList args;
  args << "--json" << "--no-messages";

  if (opt_.includeHidden) args << "--hidden";
  if (opt_.followSymlinks) args << "--follow";

  if (!opt_.useRegex) args << "--fixed-strings";
  if (!opt_.caseSensitive) args << "--ignore-case";
  if (opt_.searchBinary) args << "--text";

  // Give consistent column numbers
  args << "--column";

  args << opt_.query;
  args << opt_.rootPath;

  rg_->setArguments(args);
  rg_->setProcessChannelMode(QProcess::MergedChannels);

  connect(rg_, &QProcess::readyReadStandardOutput, this, &SearchResultsTab::onRgReadyRead_);
  connect(rg_, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this, &SearchResultsTab::onRgFinished_);

  rg_->start();
  if (!rg_->waitForStarted(1500)) {
    finish_(tr("Failed to start ripgrep"));
    cancelBtn_->setEnabled(false);
    rg_->deleteLater();
    rg_ = nullptr;
    return;
  }
}

void SearchResultsTab::onRgReadyRead_() {
  if (!rg_) return;
  rgBuf_.append(rg_->readAllStandardOutput());
  for (;;) {
    const int nl = rgBuf_.indexOf('\n');
    if (nl < 0) break;
    const QByteArray line = rgBuf_.left(nl);
    rgBuf_.remove(0, nl + 1);
    if (line.trimmed().isEmpty()) continue;

    const QJsonDocument doc = QJsonDocument::fromJson(line);
    if (!doc.isObject()) continue;
    const QJsonObject obj = doc.object();
    const QString type = obj.value("type").toString();
    if (type != "match") continue;

    const QJsonObject data = obj.value("data").toObject();
    QString path = data.value("path").toObject().value("text").toString();
      if (QDir::isRelativePath(path)) {
        path = QDir(opt_.rootPath).filePath(path);
      }
    const int lineNo = data.value("line_number").toInt();
    const QJsonObject sub = data.value("submatches").toArray().isEmpty() ? QJsonObject()
      : data.value("submatches").toArray().first().toObject();
    int col = 0;
    if (sub.contains("start")) {
      // ripgrep gives byte offset into the line; we keep it simple
      col = sub.value("start").toInt() + 1;
    }
    QString preview = data.value("lines").toObject().value("text").toString();
    preview = preview.trimmed();

    addResult_(path, false, lineNo, col, preview);
  }
}

void SearchResultsTab::onRgFinished_(int exitCode, QProcess::ExitStatus status) {
  onRgReadyRead_();

  if (status == QProcess::CrashExit) {
    if (cancelFlag_.loadAcquire()) finish_(tr("Search canceled"));
    else finish_(tr("ripgrep crashed"));
  } else {
    // rg exit codes: 0 = matches, 1 = no matches, 2 = error
    if (cancelFlag_.loadAcquire()) finish_(tr("Search canceled"));
    else if (exitCode == 2) finish_(tr("ripgrep error"));
    else finish_(tr("Search complete — %1 result(s)").arg(model_->rowCount()));
  }

  if (rg_) {
    rg_->deleteLater();
    rg_ = nullptr;
  }
}

void SearchResultsTab::onActivated_(const QModelIndex &idx) {
  if (!idx.isValid()) return;
  const QModelIndex src = proxy_->mapToSource(idx);
  const auto &it = model_->itemAt(src.row());
  if (it.isDir) emit openContainingFolderRequested(it.path);
  else emit openPathRequested(it.path);
}

void SearchResultsTab::onContextMenu_(const QPoint &pos) {
  const QModelIndex idx = view_->indexAt(pos);
  if (!idx.isValid()) return;

  const QModelIndex src = proxy_->mapToSource(idx);
  const auto &it = model_->itemAt(src.row());

  QMenu m(this);
  QAction *openAct = m.addAction(tr("Open"));
  QAction *openFolderAct = m.addAction(tr("Open containing folder"));
  m.addSeparator();
  QAction *copyPathAct = m.addAction(tr("Copy path"));

  QAction *chosen = m.exec(view_->viewport()->mapToGlobal(pos));
  if (!chosen) return;
  if (chosen == openAct) {
    if (it.isDir) emit openContainingFolderRequested(it.path);
    else emit openPathRequested(it.path);
  } else if (chosen == openFolderAct) {
    emit openContainingFolderRequested(it.path);
  } else if (chosen == copyPathAct) {
    QGuiApplication::clipboard()->setText(it.path);
  }
}
