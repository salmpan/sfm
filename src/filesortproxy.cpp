#include "filesortproxy.h"

#include <QFileSystemModel>
#include <QFileInfo>
#include <QDirIterator>
#include <QLocale>

#include <QtConcurrent/QtConcurrentRun>
#include <QFutureWatcher>

#include <algorithm>

FileSortProxyModel::FileSortProxyModel(QObject *parent)
  : QSortFilterProxyModel(parent) {
  collator_.setCaseSensitivity(Qt::CaseInsensitive);
  collator_.setNumericMode(true);
  setDynamicSortFilter(true);
}

void FileSortProxyModel::setFoldersFirst(bool on) {
  if (foldersFirst_ == on) return;
  foldersFirst_ = on;
  invalidate();
}

void FileSortProxyModel::setNaturalSort(bool on) {
  if (naturalSort_ == on) return;
  naturalSort_ = on;
  collator_.setNumericMode(on);
  invalidate();
}

QFileSystemModel* FileSortProxyModel::fsModel() const {
  return qobject_cast<QFileSystemModel*>(sourceModel());
}

bool FileSortProxyModel::lessThan(const QModelIndex &left, const QModelIndex &right) const {
  auto *m = fsModel();
  if (!m) return QSortFilterProxyModel::lessThan(left, right);
  if (!left.isValid() || !right.isValid()) return false;

  const QFileInfo li = m->fileInfo(left);
  const QFileInfo ri = m->fileInfo(right);

  if (foldersFirst_) {
    const bool ld = li.isDir();
    const bool rd = ri.isDir();
    if (ld != rd) return ld; // directories first
  }

  const int col = left.column();

  // Name
  if (col == 0) {
    return collator_.compare(li.fileName(), ri.fileName()) < 0;
  }

  // Size (directories sort as 0; consistent with QFileSystemModel)
  if (col == 1) {
    const quint64 ls = (li.isDir() ? (quint64)cachedDirSizeBytes(li.absoluteFilePath()) : (quint64)li.size());
    const quint64 rs = (ri.isDir() ? (quint64)cachedDirSizeBytes(ri.absoluteFilePath()) : (quint64)ri.size());
    if (ls != rs) return ls < rs;
    return collator_.compare(li.fileName(), ri.fileName()) < 0;
  }

  // Type (extension)
  if (col == 2) {
    const QString le = li.suffix().toLower();
    const QString re = ri.suffix().toLower();
    const int c = collator_.compare(le, re);
    if (c != 0) return c < 0;
    return collator_.compare(li.fileName(), ri.fileName()) < 0;
  }

  // Modified time
  if (col == 3) {
    const auto lt = li.lastModified();
    const auto rt = ri.lastModified();
    if (lt != rt) return lt < rt;
    return collator_.compare(li.fileName(), ri.fileName()) < 0;
  }

  return QSortFilterProxyModel::lessThan(left, right);
}

QVariant FileSortProxyModel::data(const QModelIndex &index, int role) const {
  if (!index.isValid()) return {};

  // For everything except the Size column, defer to base.
  if (index.column() != 1) return QSortFilterProxyModel::data(index, role);

  auto *m = fsModel();
  if (!m) return QSortFilterProxyModel::data(index, role);

  const QModelIndex src = mapToSource(index);
  if (!src.isValid()) return QSortFilterProxyModel::data(index, role);

  const QFileInfo fi = m->fileInfo(src);
  if (!fi.isDir()) {
    // Keep QFileSystemModel behavior for files.
    return QSortFilterProxyModel::data(index, role);
  }

  const QString path = fi.absoluteFilePath();

  // IMPORTANT: data() must be side-effect free.
  // Directory size computation is triggered explicitly via requestFolderSize().
  const auto it = dirSizeCache_.constFind(path);

  // Cached.
  if (it != dirSizeCache_.cend()) {
    if (role == Qt::DisplayRole || role == Qt::ToolTipRole) {
      return QLocale().formattedDataSize(it.value());
    }
    if (role == Qt::UserRole) {
      return it.value();
    }
  }

  // In-flight / queued.
  if (dirSizeInFlight_.contains(path) || dirSizeQueuedSet_.contains(path)) {
    if (role == Qt::DisplayRole || role == Qt::ToolTipRole) return QStringLiteral("…");
    if (role == Qt::UserRole) return QVariant();
  }

  // Not requested.
  if (role == Qt::DisplayRole || role == Qt::ToolTipRole) return QStringLiteral("—");
  if (role == Qt::UserRole) return QVariant();

  return QSortFilterProxyModel::data(index, role);
}

qint64 FileSortProxyModel::cachedDirSizeBytes(const QString &path) const {
  return dirSizeCache_.contains(path) ? dirSizeCache_.value(path) : 0;
}

void FileSortProxyModel::requestFolderSize(const QModelIndex& proxyIndex) {
  if (!proxyIndex.isValid()) return;

  auto *m = fsModel();
  if (!m) return;

  // Use column 0 for fileInfo lookup.
  const QModelIndex src0 = mapToSource(proxyIndex.sibling(proxyIndex.row(), 0));
  if (!src0.isValid()) return;

  const QFileInfo fi = m->fileInfo(src0);
  if (!fi.isDir()) return;

  const QString path = fi.absoluteFilePath();
  if (dirSizeCache_.contains(path)) return;

  const QPersistentModelIndex waiter(proxyIndex.sibling(proxyIndex.row(), 1));
  enqueueDirSizeTask_(path, waiter);
}

void FileSortProxyModel::enqueueDirSizeTask_(const QString &path, const QPersistentModelIndex &waiter) const {
  if (!waiter.isValid()) return;
  dirSizeWaiters_[path].push_back(waiter);

  if (dirSizeCache_.contains(path)) {
    emit const_cast<FileSortProxyModel*>(this)->dataChanged(waiter, waiter, {Qt::DisplayRole, Qt::ToolTipRole, Qt::UserRole});
    return;
  }
  if (dirSizeInFlight_.contains(path)) return;

  if (!dirSizeQueuedSet_.contains(path)) {
    dirSizeQueuedSet_.insert(path);
    dirSizeQueue_.enqueue(path);
  }
  startNextDirSizeTask_();
}

void FileSortProxyModel::startNextDirSizeTask_() const {
  if (dirSizeActiveJobs_ >= kMaxDirSizeJobs) return;
  if (dirSizeQueue_.isEmpty()) return;

  const QString path = dirSizeQueue_.dequeue();
  dirSizeQueuedSet_.remove(path);

  if (dirSizeCache_.contains(path) || dirSizeInFlight_.contains(path)) {
    startNextDirSizeTask_();
    return;
  }

  dirSizeInFlight_.insert(path);
  dirSizeActiveJobs_++;

  auto *watcher = new QFutureWatcher<qint64>(const_cast<FileSortProxyModel*>(this));
  QObject::connect(watcher, &QFutureWatcher<qint64>::finished,
                   const_cast<FileSortProxyModel*>(this),
                   [self = const_cast<FileSortProxyModel*>(this), watcher, path]() {
    const qint64 bytes = watcher->result();
    watcher->deleteLater();

    self->dirSizeCache_.insert(path, bytes);
    self->dirSizeInFlight_.remove(path);
    self->dirSizeActiveJobs_ = std::max(0, self->dirSizeActiveJobs_ - 1);

    const auto waiters = self->dirSizeWaiters_.take(path);
    for (const auto &pidx : waiters) {
      if (!pidx.isValid()) continue;
      emit self->dataChanged(pidx, pidx, {Qt::DisplayRole, Qt::ToolTipRole, Qt::UserRole});
    }
    emit self->folderSizeReady(path);

    self->startNextDirSizeTask_();
  });

  watcher->setFuture(QtConcurrent::run(&FileSortProxyModel::computeDirectorySizeBytes, path));
}

qint64 FileSortProxyModel::computeDirectorySizeBytes(const QString &path) {
  qint64 total = 0;
  QDirIterator it(path,
                 QDir::Files | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot,
                 QDirIterator::Subdirectories);
  while (it.hasNext()) {
    it.next();
    const QFileInfo fi = it.fileInfo();
    if (fi.isSymLink()) continue;
    if (fi.isFile()) total += fi.size();
  }
  return total;
}
