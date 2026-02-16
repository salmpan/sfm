#pragma once

#include <QSortFilterProxyModel>
#include <QCollator>
#include <QHash>
#include <QSet>
#include <QQueue>
#include <QPersistentModelIndex>

class QFutureWatcherBase;

class QFileSystemModel;

class FileSortProxyModel final : public QSortFilterProxyModel {
  Q_OBJECT
public:
  explicit FileSortProxyModel(QObject *parent = nullptr);

  void setFoldersFirst(bool on);
  bool foldersFirst() const { return foldersFirst_; }

  void setNaturalSort(bool on);
  bool naturalSort() const { return naturalSort_; }
  // Explicit request for a directory size calculation.
  // This is intentionally NOT done from data() to keep the UI responsive.
  void requestFolderSize(const QModelIndex& proxyIndex);

signals:
  void folderSizeReady(const QString& path);

public:
  QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;

protected:
  bool lessThan(const QModelIndex &left, const QModelIndex &right) const override;

private:
  QFileSystemModel* fsModel() const;

  void enqueueDirSizeTask_(const QString &path, const QPersistentModelIndex &waiter) const;
  void startNextDirSizeTask_() const;
  qint64 cachedDirSizeBytes(const QString &path) const;
  static qint64 computeDirectorySizeBytes(const QString &path);

  bool foldersFirst_{true};
  bool naturalSort_{true};
  mutable QCollator collator_;

  // Directory size cache (bytes), keyed by absolute path.
  // Populated only via explicit requests.
  mutable QHash<QString, qint64> dirSizeCache_;

  // Paths currently computing.
  mutable QSet<QString> dirSizeInFlight_;

  // Paths queued to compute.
  mutable QSet<QString> dirSizeQueuedSet_;
  mutable QQueue<QString> dirSizeQueue_;

  // For each path, which indices should be updated when the size becomes available.
  mutable QHash<QString, QList<QPersistentModelIndex>> dirSizeWaiters_;

  mutable int dirSizeActiveJobs_ = 0;
  static constexpr int kMaxDirSizeJobs = 4;
};