#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantMap>
#include <deque>

#include "fileops.h"

// JobManager:
// - owns a worker thread
// - queues jobs FIFO
// - exposes progress signals
// - provides UI conflict resolver (dialog) invoked from worker thread via BlockingQueuedConnection
class JobManager final : public QObject {
  Q_OBJECT
public:
  struct Job {
    FileOps::Op op;
    QStringList sources;
    QString destDir;
    QString displayName;
    int totalItems{0};
  };

  explicit JobManager(QObject *parent = nullptr);
  ~JobManager() override;

  void enqueueCopy(const QStringList &sources, const QString &destDir, const QString &displayName = "Copy");
  void enqueueMove(const QStringList &sources, const QString &destDir, const QString &displayName = "Move");
  void enqueueTrash(const QStringList &paths, const QString &displayName = "Move to Trash");
  void enqueueDelete(const QStringList &paths, const QString &displayName = "Delete Permanently");

  void cancelAll();

  // UI thread method called from worker to show conflict dialog and return decision.
  Q_INVOKABLE QVariantMap resolveConflict(const QVariantMap &info);

signals:
  void jobStarted(const JobManager::Job &job, int totalItems);
  void jobProgress(const JobManager::Job &job, const QString &currentPath, int doneItems, int totalItems);

  void jobBytesTotal(const JobManager::Job &job, qint64 totalBytes);
  void jobBytesProgress(const JobManager::Job &job, const QString &currentPath, qint64 doneBytes, qint64 totalBytes);

  void jobFinished(const JobManager::Job &job, bool ok, const QString &errorMessage);
  void idle();

private slots:
  void startNextIfIdle();

  void onWorkerStarted(FileOps::Op op, int totalItems);
  void onWorkerProgress(FileOps::Op op, const QString &currentPath, int doneItems, int totalItems);
  void onWorkerBytesTotal(FileOps::Op op, qint64 totalBytes);
  void onWorkerBytesProgress(FileOps::Op op, const QString &currentPath, qint64 doneBytes, qint64 totalBytes);
  void onWorkerFinished(FileOps::Op op, bool ok, const QString &errorMessage);

private:
  void enqueueJob(const Job &job);

private:
  class Worker;
  Worker *worker_{nullptr};
  QThread *thread_{nullptr};

  std::deque<Job> queue_;
  bool active_{false};
  Job current_;
};

Q_DECLARE_METATYPE(JobManager::Job)
