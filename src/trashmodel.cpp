#include "trashmodel.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLocale>
#include <QUrl>
#include <QStandardPaths>
#include <QTextStream>


TrashModel::TrashModel(QObject *parent)
  : QAbstractTableModel(parent) {
  refresh();
}

QString TrashModel::xdgDataHome() {
  const QString xdg = qEnvironmentVariable("XDG_DATA_HOME");
  if (!xdg.isEmpty()) return xdg;
  return QDir::home().filePath(".local/share");
}

QString TrashModel::trashFilesDir() {
  return QDir(xdgDataHome()).filePath("Trash/files");
}

QString TrashModel::trashInfoDir() {
  return QDir(xdgDataHome()).filePath("Trash/info");
}

QString TrashModel::originalPathFromTrashInfo(const QString &trashInfoPath) {
  QFile f(trashInfoPath);
  if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return QString();
  QTextStream ts(&f);
  QString line;
  while (ts.readLineInto(&line)) {
    if (line.startsWith("Path=")) {
      return QUrl::fromPercentEncoding(line.mid(5).toUtf8());
    }
  }
  return QString();
}

QDateTime TrashModel::deletedAtFromTrashInfo(const QString &trashInfoPath) {
  QFile f(trashInfoPath);
  if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return {};
  QTextStream ts(&f);
  QString line;
  while (ts.readLineInto(&line)) {
    if (line.startsWith("DeletionDate=")) {
      const QString v = line.mid(QString("DeletionDate=").size()).trimmed();
      // Spec uses ISO 8601 local time; treat as local.
      return QDateTime::fromString(v, Qt::ISODate);
    }
  }
  return {};
}

void TrashModel::refresh() {
  beginResetModel();
  items_.clear();

  const QDir filesDir(trashFilesDir());
  const QDir infoDir(trashInfoDir());

  if (!filesDir.exists() || !infoDir.exists()) {
    endResetModel();
    return;
  }

  const QFileInfoList fileEntries = filesDir.entryInfoList(QDir::AllEntries | QDir::NoDotAndDotDot, QDir::Name);
  items_.reserve(fileEntries.size());

  for (const QFileInfo &fi : fileEntries) {
    const QString name = fi.fileName();
    const QString trashedPath = fi.absoluteFilePath();
    const QString infoPath = infoDir.filePath(name + ".trashinfo");

    Item it;
    it.name = name;
    it.trashedPath = trashedPath;
    it.infoPath = infoPath;
    it.originalPath = originalPathFromTrashInfo(infoPath);
    it.deletedAt = deletedAtFromTrashInfo(infoPath);
    it.isDir = fi.isDir();
    it.size = fi.isDir() ? 0 : fi.size();

    items_.push_back(std::move(it));
  }

  endResetModel();
}

int TrashModel::rowCount(const QModelIndex &parent) const {
  if (parent.isValid()) return 0;
  return items_.size();
}

int TrashModel::columnCount(const QModelIndex &parent) const {
  Q_UNUSED(parent);
  return ColumnCount;
}

QVariant TrashModel::headerData(int section, Qt::Orientation orientation, int role) const {
  if (orientation != Qt::Horizontal || role != Qt::DisplayRole) return {};
  switch (section) {
    case Name: return QStringLiteral("Name");
    case OriginalPath: return QStringLiteral("Original Location");
    case DeletedAt: return QStringLiteral("Deleted");
    case Size: return QStringLiteral("Size");
    default: return {};
  }
}

QVariant TrashModel::data(const QModelIndex &index, int role) const {
  if (!index.isValid()) return {};
  const int row = index.row();
  if (row < 0 || row >= items_.size()) return {};
  const Item &it = items_.at(row);

  if (role == Qt::DisplayRole) {
    switch (index.column()) {
      case Name: return it.name;
      case OriginalPath: return it.originalPath;
      case DeletedAt: return it.deletedAt.isValid() ? QLocale().toString(it.deletedAt, QLocale::ShortFormat) : QString();
      case Size: {
        if (it.isDir) return QString();
        return it.size;
      }
      default: return {};
    }
  }

  if (role == TrashedPathRole) return it.trashedPath;
  if (role == InfoPathRole) return it.infoPath;
  if (role == OriginalPathRole) return it.originalPath;
  if (role == DeletedAtRole) return it.deletedAt;
  if (role == IsDirRole) return it.isDir;
  if (role == SizeRole) return it.size;

  return {};
}

QString TrashModel::trashedPathForRow(int row) const {
  if (row < 0 || row >= items_.size()) return {};
  return items_.at(row).trashedPath;
}

QString TrashModel::infoPathForRow(int row) const {
  if (row < 0 || row >= items_.size()) return {};
  return items_.at(row).infoPath;
}

QString TrashModel::originalPathForRow(int row) const {
  if (row < 0 || row >= items_.size()) return {};
  return items_.at(row).originalPath;
}
