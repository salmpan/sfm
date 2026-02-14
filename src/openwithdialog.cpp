#include "openwithdialog.h"
#include "desktopentry.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QListWidget>
#include <QLabel>
#include <QPushButton>
#include <QFileInfo>
#include <QMimeDatabase>
#include <QProcess>
#include <QUrl>
#include <QSet>
#include <QMessageBox>

static QString runAndCapture(const QString &prog, const QStringList &args, int *exitCodeOut = nullptr) {
  QProcess p;
  p.start(prog, args);
  if (!p.waitForFinished(3000)) {
    if (exitCodeOut) *exitCodeOut = -1;
    return {};
  }
  if (exitCodeOut) *exitCodeOut = p.exitCode();
  return QString::fromLocal8Bit(p.readAllStandardOutput());
}

static QStringList splitLines(const QString &s) {
  QStringList out;
  for (const QString &line : s.split('\n')) {
    const QString t = line.trimmed();
    if (!t.isEmpty()) out << t;
  }
  return out;
}

OpenWithDialog::OpenWithDialog(const QString &filePath, QWidget *parent)
  : QDialog(parent), filePath_(filePath) {
  setWindowTitle("Open With");
  resize(560, 420);

  mime_ = detectMime(filePath_);

  auto *root = new QVBoxLayout(this);

  lblInfo_ = new QLabel(this);
  lblInfo_->setWordWrap(true);
  lblInfo_->setText("File:\n" + filePath_ + "\n\nMIME:\n" + (mime_.isEmpty() ? "unknown" : mime_));
  root->addWidget(lblInfo_);

  lblDefault_ = new QLabel(this);
  lblDefault_->setWordWrap(true);
  root->addWidget(lblDefault_);

  list_ = new QListWidget(this);
  list_->setSelectionMode(QAbstractItemView::SingleSelection);
  root->addWidget(list_, 1);

  auto *btnRow = new QHBoxLayout();
  btnOpen_ = new QPushButton("Open", this);
  btnSetDefault_ = new QPushButton("Set as default", this);
  auto *btnCancel = new QPushButton("Cancel", this);

  btnOpen_->setEnabled(false);
  btnSetDefault_->setEnabled(false);

  btnRow->addStretch(1);
  btnRow->addWidget(btnSetDefault_);
  btnRow->addWidget(btnOpen_);
  btnRow->addWidget(btnCancel);
  root->addLayout(btnRow);

  connect(btnCancel, &QPushButton::clicked, this, &QDialog::reject);
  connect(btnOpen_, &QPushButton::clicked, this, &OpenWithDialog::onOpenClicked);
  connect(btnSetDefault_, &QPushButton::clicked, this, &OpenWithDialog::onSetDefaultClicked);
  connect(list_, &QListWidget::itemSelectionChanged, this, &OpenWithDialog::onSelectionChanged);
  connect(list_, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem*){ onOpenClicked(); });

  refresh();
}

QString OpenWithDialog::selectedDesktopId() const {
  auto *it = list_->currentItem();
  if (!it) return {};
  return it->data(Qt::UserRole).toString();
}

QString OpenWithDialog::detectMime(const QString &filePath) {
  QMimeDatabase db;
  const QMimeType mt = db.mimeTypeForFile(filePath, QMimeDatabase::MatchContent);
  if (mt.isValid()) return mt.name();
  return {};
}

QString OpenWithDialog::queryDefaultDesktopId(const QString &mime) {
  if (mime.isEmpty()) return {};
  int code = 0;
  const QString out = runAndCapture("xdg-mime", {"query", "default", mime}, &code).trimmed();
  if (code != 0) return {};
  return out;
}

QList<OpenWithDialog::AppRow> OpenWithDialog::queryRegisteredAppsInternal(const QString &mime) {
  QList<AppRow> apps;
  if (mime.isEmpty()) return apps;

  int code = 0;
  const QString out = runAndCapture("gio", {"mime", mime}, &code);
  if (code != 0 || out.isEmpty()) return apps;

  const QStringList lines = splitLines(out);

  bool inRegistered = false;
  for (const QString &ln : lines) {
    if (ln.startsWith("Registered applications:")) { inRegistered = true; continue; }
    if (ln.endsWith("applications:") && !ln.startsWith("Registered applications:")) {
      inRegistered = false;
      continue;
    }
    if (!inRegistered) continue;

    QString id = ln;
    if (!id.endsWith(".desktop")) continue;

    DesktopEntry de;
    AppRow r;
    r.desktopId = id;
    if (loadDesktopEntry(id, &de)) {
      r.name = de.name;
      r.iconName = de.iconName;
      r.noDisplay = de.noDisplay;
    } else {
      r.name = QFileInfo(id).completeBaseName();
    }
    apps.push_back(r);
  }

  if (apps.isEmpty()) {
    const QString def = queryDefaultDesktopId(mime);
    if (!def.isEmpty()) {
      DesktopEntry de;
      AppRow r;
      r.desktopId = def;
      if (loadDesktopEntry(def, &de)) {
        r.name = de.name;
        r.iconName = de.iconName;
        r.noDisplay = de.noDisplay;
      } else {
        r.name = QFileInfo(def).completeBaseName();
      }
      apps.push_back(r);
    }
  }

  QList<AppRow> filtered;
  for (const auto &a : apps) if (!a.noDisplay) filtered.push_back(a);
  if (!filtered.isEmpty()) apps = filtered;

  QSet<QString> seen;
  QList<AppRow> uniq;
  for (const auto &a : apps) {
    if (seen.contains(a.desktopId)) continue;
    seen.insert(a.desktopId);
    uniq.push_back(a);
  }
  return uniq;
}

QList<OpenWithDialog::AppInfo> OpenWithDialog::queryAppsForMime(const QString &mime, int limit) {
  QList<AppInfo> out;
  const QList<AppRow> rows = queryRegisteredAppsInternal(mime);
  for (const auto &r : rows) {
    if (limit > 0 && out.size() >= limit) break;
    AppInfo a;
    a.desktopId = r.desktopId;
    a.name = r.name;
    a.iconName = r.iconName;
    out.push_back(a);
  }
  return out;
}

void OpenWithDialog::refresh() {
  populateList();
  onSelectionChanged();
}

void OpenWithDialog::populateList() {
  list_->clear();

  const QString def = queryDefaultDesktopId(mime_);
  if (def.isEmpty()) {
    lblDefault_->setText("Default application: (unknown)");
  } else {
    DesktopEntry de;
    const QString name = loadDesktopEntry(def, &de) ? de.name : QFileInfo(def).completeBaseName();
    lblDefault_->setText("Default application: " + name + "  (" + def + ")");
  }

  const QList<AppRow> apps = queryRegisteredAppsInternal(mime_);
  for (const auto &a : apps) {
    auto *it = new QListWidgetItem(a.name, list_);
    it->setData(Qt::UserRole, a.desktopId);
    if (!a.iconName.isEmpty()) it->setIcon(QIcon::fromTheme(a.iconName));
    if (!def.isEmpty() && a.desktopId == def) it->setText(a.name + "  (default)");
  }

  if (!def.isEmpty()) {
    for (int i = 0; i < list_->count(); ++i) {
      if (list_->item(i)->data(Qt::UserRole).toString() == def) { list_->setCurrentRow(i); break; }
    }
  } else if (list_->count() > 0) {
    list_->setCurrentRow(0);
  }
}

void OpenWithDialog::onSelectionChanged() {
  const bool has = (list_->currentItem() != nullptr);
  btnOpen_->setEnabled(has);
  btnSetDefault_->setEnabled(has && !mime_.isEmpty());
}

bool OpenWithDialog::launchWithDesktopId(const QString &desktopId, const QString &filePath, QString *errorOut) {
  if (desktopId.isEmpty()) {
    if (errorOut) *errorOut = "No application selected.";
    return false;
  }

  const QString uri = QUrl::fromLocalFile(filePath).toString();
  const QString idNoExt = desktopId.endsWith(".desktop")
      ? desktopId.left(desktopId.size() - QString(".desktop").size())
      : desktopId;

  int code = QProcess::execute("gio", {"launch", idNoExt, uri});
  if (code == 0) return true;

  code = QProcess::execute("gtk-launch", {idNoExt, uri});
  if (code == 0) return true;

  code = QProcess::execute("xdg-open", {filePath});
  if (code == 0) return true;

  if (errorOut) *errorOut = "Failed to launch application (gio/gtk-launch/xdg-open all failed).";
  return false;
}

bool OpenWithDialog::setDefaultForMime(const QString &desktopId, const QString &mime, QString *errorOut) {
  if (desktopId.isEmpty() || mime.isEmpty()) {
    if (errorOut) *errorOut = "Missing desktopId or MIME type.";
    return false;
  }

  const QString id = desktopId.endsWith(".desktop") ? desktopId : (desktopId + ".desktop");
  QProcess p;
  p.start("xdg-mime", {"default", id, mime});
  if (!p.waitForFinished(3000) || p.exitCode() != 0) {
    if (errorOut) *errorOut = "xdg-mime failed to set default.";
    return false;
  }
  return true;
}

void OpenWithDialog::onOpenClicked() {
  QString err;
  if (!launchWithDesktopId(selectedDesktopId(), filePath_, &err)) {
    QMessageBox::warning(this, "Open With", err);
    return;
  }
  accept();
}

void OpenWithDialog::onSetDefaultClicked() {
  QString err;
  const QString id = selectedDesktopId();
  if (!setDefaultForMime(id, mime_, &err)) {
    QMessageBox::warning(this, "Set default", err);
    return;
  }
  defaultSet_ = true;
  refresh();
}
