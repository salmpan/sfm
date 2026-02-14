#include "trashview.h"

#include <QListWidget>
#include <QVBoxLayout>
#include <QDir>
#include <QFileInfo>
#include <QFile>
#include <QMenu>
#include <QMessageBox>
#include <QSettings>
#include <QDesktopServices>
#include <QUrl>

TrashView::TrashView(QWidget *parent) : QWidget(parent) {
  list_ = new QListWidget(this);
  list_->setSelectionMode(QAbstractItemView::ExtendedSelection);
  list_->setContextMenuPolicy(Qt::CustomContextMenu);

  auto *layout = new QVBoxLayout(this);
  layout->setContentsMargins(0,0,0,0);
  layout->addWidget(list_);

  connect(list_, &QListWidget::itemActivated, this, &TrashView::onItemActivated);
  connect(list_, &QWidget::customContextMenuRequested, this, &TrashView::onContextMenu);

  refresh();
}

QString TrashView::xdgDataHome() {
  const QString env = qEnvironmentVariable("XDG_DATA_HOME");
  if (!env.isEmpty()) return env;
  return QDir::homePath() + "/.local/share";
}

QString TrashView::trashFilesDir() { return xdgDataHome() + "/Trash/files"; }
QString TrashView::trashInfoDir()  { return xdgDataHome() + "/Trash/info"; }

void TrashView::refresh() {
  list_->clear();

  QDir d(trashFilesDir());
  if (!d.exists()) return;

  const QFileInfoList items = d.entryInfoList(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden,
                                              QDir::Time | QDir::Reversed);
  for (const QFileInfo &fi : items) {
    auto *it = new QListWidgetItem(fi.fileName(), list_);
    it->setData(Qt::UserRole, fi.absoluteFilePath()); // trashed path
    it->setToolTip(fi.absoluteFilePath());
    if (fi.isDir()) it->setIcon(QIcon::fromTheme("folder"));
    else it->setIcon(QIcon::fromTheme("text-x-generic"));
  }
}

QStringList TrashView::selectedTrashedPaths() const {
  QStringList out;
  const auto sel = list_->selectedItems();
  for (auto *it : sel) out << it->data(Qt::UserRole).toString();
  return out;
}

// Try to find info file matching trashed name by scanning (robust but fine for trash size).
QString TrashView::infoFileForTrashedName(const QString &trashedName) {
  QDir infoDir(trashInfoDir());
  const QFileInfoList infos = infoDir.entryInfoList(QStringList() << "*.trashinfo",
                                                    QDir::Files | QDir::NoDotAndDotDot);
  for (const QFileInfo &fi : infos) {
    const QString base = fi.completeBaseName(); // removes .trashinfo
    if (base == trashedName) return fi.absoluteFilePath();
  }
  return {};
}

QString TrashView::originalPathFromTrashInfo(const QString &trashInfoPath) {
  if (trashInfoPath.isEmpty()) return {};
  QSettings ini(trashInfoPath, QSettings::IniFormat);
  // ini.setIniCodec("UTF-8");
  ini.beginGroup("Trash Info");
  const QString p = ini.value("Path").toString();
  ini.endGroup();
  return p;
}

static bool removePathRecursively(const QString &p, QString *errorOut) {
  QFileInfo fi(p);
  if (!fi.exists()) return true;
  if (fi.isFile() || fi.isSymLink()) {
    if (!QFile::remove(p)) { if (errorOut) *errorOut = "Failed to remove: " + p; return false; }
    return true;
  }
  if (fi.isDir()) {
    QDir d(p);
    if (!d.removeRecursively()) { if (errorOut) *errorOut = "Failed to remove directory: " + p; return false; }
    return true;
  }
  if (errorOut) *errorOut = "Unsupported type: " + p;
  return false;
}

bool TrashView::deleteSelectedPermanently(QString *errorOut) {
  const auto sel = list_->selectedItems();
  for (auto *it : sel) {
    const QString trashedPath = it->data(Qt::UserRole).toString();
    const QString name = QFileInfo(trashedPath).fileName();

    const QString infoPath = infoFileForTrashedName(name);

    QString err;
    if (!removePathRecursively(trashedPath, &err)) {
      if (errorOut) *errorOut = err;
      return false;
    }
    if (!infoPath.isEmpty()) QFile::remove(infoPath);
  }
  refresh();
  return true;
}

bool TrashView::restoreSelected(QString *errorOut) {
  const auto sel = list_->selectedItems();
  for (auto *it : sel) {
    const QString trashedPath = it->data(Qt::UserRole).toString();
    const QString name = QFileInfo(trashedPath).fileName();
    const QString infoPath = infoFileForTrashedName(name);
    const QString orig = originalPathFromTrashInfo(infoPath);

    if (orig.isEmpty()) {
      if (errorOut) *errorOut = "Missing original path for: " + name;
      return false;
    }

    QDir().mkpath(QFileInfo(orig).absolutePath());

    // Prefer rename restore
    if (!QFile::rename(trashedPath, orig)) {
      // Cross-device etc; fallback: open location hint
      if (errorOut) *errorOut = "Failed to restore (rename failed): " + name;
      return false;
    }

    if (!infoPath.isEmpty()) QFile::remove(infoPath);
  }
  refresh();
  return true;
}

void TrashView::onItemActivated(QListWidgetItem *it) {
  if (!it) return;
  const QString p = it->data(Qt::UserRole).toString();
  QFileInfo fi(p);
  if (fi.isDir()) {
    emit requestNavigate(p);
  } else {
    QDesktopServices::openUrl(QUrl::fromLocalFile(p));
  }
}

void TrashView::onContextMenu(const QPoint &pos) {
  QListWidgetItem *it = list_->itemAt(pos);
  if (!it) return;

  QMenu menu(this);
  QAction *aOpen = menu.addAction("Open");
  QAction *aRestore = menu.addAction("Restore");
  QAction *aDelete = menu.addAction("Delete Permanently");

  QAction *chosen = menu.exec(list_->viewport()->mapToGlobal(pos));
  if (!chosen) return;

  if (chosen == aOpen) onItemActivated(it);
  else if (chosen == aRestore) {
    QString err;
    if (!restoreSelected(&err)) QMessageBox::warning(this, "Restore failed", err);
  } else if (chosen == aDelete) {
    QString err;
    if (!deleteSelectedPermanently(&err)) QMessageBox::warning(this, "Delete failed", err);
  }
}
