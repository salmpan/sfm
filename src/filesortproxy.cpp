#include "filesortproxy.h"

#include <QFileSystemModel>
#include <QFileInfo>

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
    const quint64 ls = (li.isDir() ? 0 : (quint64)li.size());
    const quint64 rs = (ri.isDir() ? 0 : (quint64)ri.size());
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
