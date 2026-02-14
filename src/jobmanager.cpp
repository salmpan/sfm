#include "jobmanager.h"

#include <QThread>
#include <QAbstractButton>
#include <QPushButton>
#include <QMetaObject>
#include <QMessageBox>
#include <QInputDialog>
#include <QFileInfo>
#include <QDateTime>
#include <QWidget>

static QString fmtDate(const QDateTime &dt) {
  if (!dt.isValid()) return "—";
  return dt.toLocalTime().toString("yyyy-MM-dd HH:mm:ss");
}

static QString humanBytes(qint64 b) {
  if (b < 0) return "—";
  const char *units[] = {"B","KiB","MiB","GiB","TiB"};
  double v = (double)b;
  int i = 0;
  while (v >= 1024.0 && i < 4) { v /= 1024.0; i++; }
  return QString::number(v, 'f', (i == 0 ? 0 : 1)) + " " + units[i];
}

class JobManager::Worker final : public QObject {
  Q_OBJECT
public:
  explicit Worker(JobManager *ui) : QObject(nullptr), ui_(ui), ops_(new FileOps(this)) {
    connect(ops_, &FileOps::started,       this, &Worker::started);
    connect(ops_, &FileOps::progress,      this, &Worker::progress);
    connect(ops_, &FileOps::bytesTotal,    this, &Worker::bytesTotal);
    connect(ops_, &FileOps::bytesProgress, this, &Worker::bytesProgress);
    connect(ops_, &FileOps::finished,      this, &Worker::finished);

    ops_->setConflictHandler([this](const FileOps::ConflictInfo &ci) -> FileOps::ConflictResult {
      QVariantMap info;
      info["op"] = (int)ci.op;
      info["src"] = ci.srcPath;
      info["dst"] = ci.dstPath;
      info["isDir"] = ci.isDir;
      info["srcSize"] = (qlonglong)ci.srcSize;
      info["dstSize"] = (qlonglong)ci.dstSize;
      info["srcMtime"] = ci.srcMtime.toMSecsSinceEpoch();
      info["dstMtime"] = ci.dstMtime.toMSecsSinceEpoch();

      QVariantMap out;
      QMetaObject::invokeMethod(ui_, "resolveConflict",
                                Qt::BlockingQueuedConnection,
                                Q_RETURN_ARG(QVariantMap, out),
                                Q_ARG(QVariantMap, info));

      FileOps::ConflictResult r;
      r.action = (FileOps::ConflictAction)out.value("action").toInt();
      r.newName = out.value("newName").toString();
      return r;
    });
  }

public slots:
  void runJob(const JobManager::Job &job) {
    ops_->clearCancel();

    QString err;
    bool ok = false;

    switch (job.op) {
      case FileOps::Op::Copy:
        ok = ops_->copyPaths(job.sources, job.destDir, &err);
        break;
      case FileOps::Op::Move:
        ok = ops_->movePaths(job.sources, job.destDir, &err);
        break;
      case FileOps::Op::Trash:
        ok = ops_->trashPaths(job.sources, &err);
        break;
      case FileOps::Op::Delete:
        ok = ops_->deletePathsPermanently(job.sources, &err);
        break;
      default:
        err = "Unknown operation.";
        ok = false;
        break;
    }

    Q_UNUSED(ok);
    Q_UNUSED(err);
  }

  void cancelCurrent() { ops_->requestCancel(); }

signals:
  void started(FileOps::Op op, int totalItems);
  void progress(FileOps::Op op, const QString &currentPath, int doneItems, int totalItems);
  void bytesTotal(FileOps::Op op, qint64 totalBytes);
  void bytesProgress(FileOps::Op op, const QString &currentPath, qint64 doneBytes, qint64 totalBytes);
  void finished(FileOps::Op op, bool ok, const QString &errorMessage);

private:
  JobManager *ui_{nullptr};
  FileOps *ops_{nullptr};
};

JobManager::JobManager(QObject *parent) : QObject(parent) {
  thread_ = new QThread(this);
  worker_ = new Worker(this);
  worker_->moveToThread(thread_);

  connect(thread_, &QThread::finished, worker_, &QObject::deleteLater);

  connect(worker_, &Worker::started,       this, &JobManager::onWorkerStarted);
  connect(worker_, &Worker::progress,      this, &JobManager::onWorkerProgress);
  connect(worker_, &Worker::bytesTotal,    this, &JobManager::onWorkerBytesTotal);
  connect(worker_, &Worker::bytesProgress, this, &JobManager::onWorkerBytesProgress);
  connect(worker_, &Worker::finished,      this, &JobManager::onWorkerFinished);

  thread_->start();
}

JobManager::~JobManager() {
  cancelAll();
  thread_->quit();
  thread_->wait();
}

void JobManager::enqueueCopy(const QStringList &sources, const QString &destDir, const QString &displayName) {
  enqueueJob(Job{FileOps::Op::Copy, sources, destDir, displayName, sources.size()});
}
void JobManager::enqueueMove(const QStringList &sources, const QString &destDir, const QString &displayName) {
  enqueueJob(Job{FileOps::Op::Move, sources, destDir, displayName, sources.size()});
}
void JobManager::enqueueTrash(const QStringList &paths, const QString &displayName) {
  enqueueJob(Job{FileOps::Op::Trash, paths, QString(), displayName, paths.size()});
}
void JobManager::enqueueDelete(const QStringList &paths, const QString &displayName) {
  enqueueJob(Job{FileOps::Op::Delete, paths, QString(), displayName, paths.size()});
}

void JobManager::enqueueJob(const Job &job) {
  queue_.push_back(job);
  startNextIfIdle();
}

void JobManager::cancelAll() {
  queue_.clear();
  if (active_) {
    QMetaObject::invokeMethod(worker_, "cancelCurrent", Qt::QueuedConnection);
  } else {
    emit idle();
  }
}

QVariantMap JobManager::resolveConflict(const QVariantMap &info) {
  QWidget *parentW = qobject_cast<QWidget*>(parent());

  const bool isDir = info.value("isDir").toBool();
  const QString src = info.value("src").toString();
  const QString dst = info.value("dst").toString();

  const qint64 srcSize = (qint64)info.value("srcSize").toLongLong();
  const qint64 dstSize = (qint64)info.value("dstSize").toLongLong();

  const QDateTime srcMt = QDateTime::fromMSecsSinceEpoch(info.value("srcMtime").toLongLong());
  const QDateTime dstMt = QDateTime::fromMSecsSinceEpoch(info.value("dstMtime").toLongLong());

  QFileInfo dfi(dst);

  QString text;
  text += "A file/folder named:\n\n";
  text += dfi.fileName() + "\n\n";
  text += "already exists in:\n\n";
  text += dfi.absolutePath() + "\n\n";
  text += "Source:\n" + src + "\n\nDestination:\n" + dst + "\n\n";

  if (!isDir) {
    text += "Source size: " + humanBytes(srcSize) + "    Modified: " + fmtDate(srcMt) + "\n";
    text += "Existing size: " + humanBytes(dstSize) + "   Modified: " + fmtDate(dstMt) + "\n";
  }

  QMessageBox box(parentW);
  box.setIcon(QMessageBox::Question);
  box.setWindowTitle("File conflict");
  box.setText(text);

  QPushButton *bOverwriteOrReplace = nullptr;
  QPushButton *bMerge = nullptr;

  QPushButton *bSkip   = box.addButton("Skip", QMessageBox::RejectRole);
  QPushButton *bRename = box.addButton("Rename…", QMessageBox::ActionRole);
  QPushButton *bCancel = box.addButton("Cancel", QMessageBox::DestructiveRole);

  if (isDir) {
    bMerge = box.addButton("Merge", QMessageBox::AcceptRole);
    bOverwriteOrReplace = box.addButton("Replace", QMessageBox::AcceptRole);
  } else {
    bOverwriteOrReplace = box.addButton("Overwrite", QMessageBox::AcceptRole);
  }

  box.setDefaultButton(bOverwriteOrReplace ? bOverwriteOrReplace : bSkip);
  box.exec();

  QAbstractButton *clicked = box.clickedButton();

  QVariantMap out;

  if (clicked == static_cast<QAbstractButton*>(bCancel)) {
    out["action"] = (int)FileOps::ConflictAction::CancelAll;
    return out;
  }
  if (clicked == static_cast<QAbstractButton*>(bSkip)) {
    out["action"] = (int)FileOps::ConflictAction::Skip;
    return out;
  }
  if (clicked == static_cast<QAbstractButton*>(bRename)) {
    bool ok = false;
    const QString suggested = dfi.fileName();
    const QString newName = QInputDialog::getText(
        parentW, "Rename",
        "New name:", QLineEdit::Normal, suggested, &ok);
    if (!ok || newName.trimmed().isEmpty()) {
      out["action"] = (int)FileOps::ConflictAction::CancelAll;
      return out;
    }
    out["action"] = (int)FileOps::ConflictAction::Rename;
    out["newName"] = newName.trimmed();
    return out;
  }
  if (isDir && bMerge && clicked == static_cast<QAbstractButton*>(bMerge)) {
    out["action"] = (int)FileOps::ConflictAction::Merge;
    return out;
  }
  if (clicked == static_cast<QAbstractButton*>(bOverwriteOrReplace)) {
    out["action"] = (int)FileOps::ConflictAction::Overwrite;
    return out;
  }

  out["action"] = (int)FileOps::ConflictAction::Skip;
  return out;
}

void JobManager::startNextIfIdle() {
  if (active_) return;
  if (queue_.empty()) { emit idle(); return; }

  current_ = queue_.front();
  queue_.pop_front();
  active_ = true;

  QMetaObject::invokeMethod(worker_, "runJob",
                            Qt::QueuedConnection,
                            Q_ARG(JobManager::Job, current_));
}

void JobManager::onWorkerStarted(FileOps::Op, int totalItems) { emit jobStarted(current_, totalItems); }
void JobManager::onWorkerProgress(FileOps::Op, const QString &p, int d, int t) { emit jobProgress(current_, p, d, t); }
void JobManager::onWorkerBytesTotal(FileOps::Op, qint64 tb) { emit jobBytesTotal(current_, tb); }
void JobManager::onWorkerBytesProgress(FileOps::Op, const QString &p, qint64 db, qint64 tb) { emit jobBytesProgress(current_, p, db, tb); }

void JobManager::onWorkerFinished(FileOps::Op, bool ok, const QString &errorMessage) {
  emit jobFinished(current_, ok, errorMessage);
  active_ = false;
  current_ = Job{};
  startNextIfIdle();
}

#include "jobmanager.moc"
