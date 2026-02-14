#include "propertiesdialog.h"

#include <QTabWidget>
#include <QVBoxLayout>
#include <QFormLayout>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QCheckBox>
#include <QDialogButtonBox>
#include <QPushButton>
#include <QMessageBox>

#include <QFileInfo>
#include <QDateTime>

#include <sys/stat.h>
#include <unistd.h>
#include <pwd.h>
#include <grp.h>
#include <errno.h>
#include <string.h>

static QString errnoString() { return QString::fromLocal8Bit(strerror(errno)); }

static QString userNameFromUid(uid_t uid) {
  passwd *pw = getpwuid(uid);
  if (!pw) return QString::number(uid);
  return QString::fromLocal8Bit(pw->pw_name);
}

static QString groupNameFromGid(gid_t gid) {
  group *gr = getgrgid(gid);
  if (!gr) return QString::number(gid);
  return QString::fromLocal8Bit(gr->gr_name);
}

static bool uidFromName(const QString &name, uid_t *out) {
  bool okNum = false;
  uint v = name.toUInt(&okNum);
  if (okNum) { *out = (uid_t)v; return true; }
  passwd *pw = getpwnam(name.toLocal8Bit().constData());
  if (!pw) return false;
  *out = pw->pw_uid;
  return true;
}

static bool gidFromName(const QString &name, gid_t *out) {
  bool okNum = false;
  uint v = name.toUInt(&okNum);
  if (okNum) { *out = (gid_t)v; return true; }
  group *gr = getgrnam(name.toLocal8Bit().constData());
  if (!gr) return false;
  *out = gr->gr_gid;
  return true;
}

PropertiesDialog::PropertiesDialog(const QString &path, QWidget *parent)
  : QDialog(parent), path_(path) {
  setWindowTitle("Properties");
  setModal(false);
  resize(520, 360);

  buildUi();
  reload();
}

void PropertiesDialog::buildUi() {
  auto *tabs = new QTabWidget(this);

  auto *basic = new QWidget(this);
  auto *basicForm = new QFormLayout(basic);

  lblPath_  = new QLabel(basic);
  lblType_  = new QLabel(basic);
  lblSize_  = new QLabel(basic);
  lblMtime_ = new QLabel(basic);
  lblAtime_ = new QLabel(basic);
  lblCtime_ = new QLabel(basic);

  for (auto *l : {lblPath_, lblType_, lblSize_, lblMtime_, lblAtime_, lblCtime_}) {
    l->setTextInteractionFlags(Qt::TextSelectableByMouse);
    l->setWordWrap(true);
  }

  basicForm->addRow("Path:", lblPath_);
  basicForm->addRow("Type:", lblType_);
  basicForm->addRow("Size:", lblSize_);
  basicForm->addRow("Modified:", lblMtime_);
  basicForm->addRow("Accessed:", lblAtime_);
  basicForm->addRow("Status changed:", lblCtime_);

  tabs->addTab(basic, "Basic");

  auto *perm = new QWidget(this);
  auto *permLayout = new QVBoxLayout(perm);

  auto *ownerForm = new QFormLayout();
  editOwner_ = new QLineEdit(perm);
  editGroup_ = new QLineEdit(perm);
  ownerForm->addRow("Owner:", editOwner_);
  ownerForm->addRow("Group:", editGroup_);

  lblOwnerGroup_ = new QLabel(perm);
  lblOwnerGroup_->setTextInteractionFlags(Qt::TextSelectableByMouse);
  ownerForm->addRow("Current:", lblOwnerGroup_);

  permLayout->addLayout(ownerForm);

  auto *grid = new QGridLayout();
  grid->addWidget(new QLabel("", perm), 0, 0);
  grid->addWidget(new QLabel("Read", perm), 0, 1);
  grid->addWidget(new QLabel("Write", perm), 0, 2);
  grid->addWidget(new QLabel("Execute", perm), 0, 3);

  grid->addWidget(new QLabel("Owner", perm), 1, 0);
  uR_ = new QCheckBox(perm); uW_ = new QCheckBox(perm); uX_ = new QCheckBox(perm);
  grid->addWidget(uR_, 1, 1); grid->addWidget(uW_, 1, 2); grid->addWidget(uX_, 1, 3);

  grid->addWidget(new QLabel("Group", perm), 2, 0);
  gR_ = new QCheckBox(perm); gW_ = new QCheckBox(perm); gX_ = new QCheckBox(perm);
  grid->addWidget(gR_, 2, 1); grid->addWidget(gW_, 2, 2); grid->addWidget(gX_, 2, 3);

  grid->addWidget(new QLabel("Others", perm), 3, 0);
  oR_ = new QCheckBox(perm); oW_ = new QCheckBox(perm); oX_ = new QCheckBox(perm);
  grid->addWidget(oR_, 3, 1); grid->addWidget(oW_, 3, 2); grid->addWidget(oX_, 3, 3);

  permLayout->addLayout(grid);

  auto *special = new QFormLayout();
  setuid_ = new QCheckBox("setuid", perm);
  setgid_ = new QCheckBox("setgid", perm);
  sticky_ = new QCheckBox("sticky", perm);

  auto *specialRow = new QWidget(perm);
  auto *specialRowL = new QHBoxLayout(specialRow);
  specialRowL->setContentsMargins(0,0,0,0);
  specialRowL->addWidget(setuid_);
  specialRowL->addWidget(setgid_);
  specialRowL->addWidget(sticky_);
  specialRowL->addStretch(1);

  lblCurrentMode_ = new QLabel(perm);
  lblCurrentMode_->setTextInteractionFlags(Qt::TextSelectableByMouse);

  special->addRow("Special:", specialRow);
  special->addRow("Mode:", lblCurrentMode_);

  permLayout->addLayout(special);
  permLayout->addStretch(1);

  tabs->addTab(perm, "Permissions");

  buttons_ = new QDialogButtonBox(QDialogButtonBox::Close, this);
  auto *btnApply = buttons_->addButton("Apply", QDialogButtonBox::ApplyRole);
  auto *btnReload = buttons_->addButton("Reload", QDialogButtonBox::ResetRole);

  connect(buttons_, &QDialogButtonBox::rejected, this, &QDialog::reject);
  connect(btnApply, &QPushButton::clicked, this, &PropertiesDialog::applyChanges);
  connect(btnReload, &QPushButton::clicked, this, &PropertiesDialog::reload);

  auto *root = new QVBoxLayout(this);
  root->addWidget(tabs);
  root->addWidget(buttons_);
}

QString PropertiesDialog::humanBytes(qint64 b) {
  const char *u[] = {"B","KiB","MiB","GiB","TiB"};
  double v = (double)b;
  int i = 0;
  while (v >= 1024.0 && i < 4) { v /= 1024.0; ++i; }
  return QString::number(v, 'f', (i==0 ? 0 : 1)) + " " + u[i];
}

QString PropertiesDialog::fmtTime(qint64 secs) {
  if (secs <= 0) return "—";
  return QDateTime::fromSecsSinceEpoch(secs).toLocalTime().toString("yyyy-MM-dd HH:mm:ss");
}

void PropertiesDialog::loadStat() {
  struct stat st;
  if (::lstat(path_.toLocal8Bit().constData(), &st) != 0) {
    throw std::runtime_error(("lstat failed: " + errnoString()).toStdString());
  }

  mode_ = (unsigned int)st.st_mode;
  uid_ = (unsigned int)st.st_uid;
  gid_ = (unsigned int)st.st_gid;
  isDir_ = S_ISDIR(st.st_mode);
  size_ = (qint64)st.st_size;
  atime_ = (qint64)st.st_atime;
  mtime_ = (qint64)st.st_mtime;
  ctime_ = (qint64)st.st_ctime;
}

void PropertiesDialog::reload() {
  try { loadStat(); }
  catch (const std::exception &e) {
    QMessageBox::warning(this, "Properties", e.what());
    return;
  }
  loadBasicTab();
  loadPermTab();
}

void PropertiesDialog::loadBasicTab() {
  lblPath_->setText(path_);

  QString type = "Unknown";
  if (S_ISDIR(mode_)) type = "Folder";
  else if (S_ISREG(mode_)) type = "File";
  else if (S_ISLNK(mode_)) type = "Symlink";
  else if (S_ISCHR(mode_)) type = "Character device";
  else if (S_ISBLK(mode_)) type = "Block device";
  else if (S_ISFIFO(mode_)) type = "FIFO";
  else if (S_ISSOCK(mode_)) type = "Socket";
  lblType_->setText(type);

  QFileInfo fi(path_);
  if (fi.isDir()) lblSize_->setText("— (folder)");
  else lblSize_->setText(humanBytes(size_));

  lblMtime_->setText(fmtTime(mtime_));
  lblAtime_->setText(fmtTime(atime_));
  lblCtime_->setText(fmtTime(ctime_));
}

void PropertiesDialog::loadPermTab() {
  editOwner_->setText(userNameFromUid((uid_t)uid_));
  editGroup_->setText(groupNameFromGid((gid_t)gid_));

  lblOwnerGroup_->setText(
      userNameFromUid((uid_t)uid_) + " : " + groupNameFromGid((gid_t)gid_) +
      "   (uid=" + QString::number(uid_) + ", gid=" + QString::number(gid_) + ")");

  auto has = [&](mode_t bit){ return (mode_ & bit) != 0; };

  uR_->setChecked(has(S_IRUSR));
  uW_->setChecked(has(S_IWUSR));
  uX_->setChecked(has(S_IXUSR));

  gR_->setChecked(has(S_IRGRP));
  gW_->setChecked(has(S_IWGRP));
  gX_->setChecked(has(S_IXGRP));

  oR_->setChecked(has(S_IROTH));
  oW_->setChecked(has(S_IWOTH));
  oX_->setChecked(has(S_IXOTH));

  setuid_->setChecked(has(S_ISUID));
  setgid_->setChecked(has(S_ISGID));
  sticky_->setChecked(has(S_ISVTX));

  const unsigned int permOnly = (mode_ & 07777);
  lblCurrentMode_->setText("0" + QString::number(permOnly, 8));
}

void PropertiesDialog::applyChanges() {
  mode_t newPerm = 0;

  if (uR_->isChecked()) newPerm |= S_IRUSR;
  if (uW_->isChecked()) newPerm |= S_IWUSR;
  if (uX_->isChecked()) newPerm |= S_IXUSR;

  if (gR_->isChecked()) newPerm |= S_IRGRP;
  if (gW_->isChecked()) newPerm |= S_IWGRP;
  if (gX_->isChecked()) newPerm |= S_IXGRP;

  if (oR_->isChecked()) newPerm |= S_IROTH;
  if (oW_->isChecked()) newPerm |= S_IWOTH;
  if (oX_->isChecked()) newPerm |= S_IXOTH;

  if (setuid_->isChecked()) newPerm |= S_ISUID;
  if (setgid_->isChecked()) newPerm |= S_ISGID;
  if (sticky_->isChecked()) newPerm |= S_ISVTX;

  const QString ownerText = editOwner_->text().trimmed();
  const QString groupText = editGroup_->text().trimmed();

  uid_t newUid = (uid_t)uid_;
  gid_t newGid = (gid_t)gid_;

  if (!ownerText.isEmpty()) {
    if (!uidFromName(ownerText, &newUid)) {
      QMessageBox::warning(this, "Apply", "Unknown owner: " + ownerText);
      return;
    }
  }
  if (!groupText.isEmpty()) {
    if (!gidFromName(groupText, &newGid)) {
      QMessageBox::warning(this, "Apply", "Unknown group: " + groupText);
      return;
    }
  }

  bool okAll = true;
  QStringList errs;

  if (newUid != (uid_t)uid_ || newGid != (gid_t)gid_) {
    if (::chown(path_.toLocal8Bit().constData(), newUid, newGid) != 0) {
      okAll = false;
      errs << ("chown failed: " + errnoString());
    }
  }

  const mode_t typeBits = (mode_ & ~07777);
  const mode_t finalMode = typeBits | newPerm;

  if (::chmod(path_.toLocal8Bit().constData(), finalMode) != 0) {
    okAll = false;
    errs << ("chmod failed: " + errnoString());
  }

  if (!okAll) QMessageBox::warning(this, "Apply", errs.join("\n"));
  reload();
}
