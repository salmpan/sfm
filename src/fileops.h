#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QDateTime>
#include <atomic>
#include <functional>

/*
  FileOps:
  - recursive copy/move/delete/trash
  - Linux FreeDesktop trash (~/.local/share/Trash/{files,info})
  - byte-based progress for copy/move/trash (delete remains item-based)
  - real cancellation (cooperative)
  - conflict resolution callback (UI provided by JobManager)
*/

class FileOps final : public QObject {
  Q_OBJECT
public:
  enum class Op { Copy, Move, Delete, Trash };
  Q_ENUM(Op)

  enum class ConflictAction {
    Overwrite,
    Merge,
    Skip,
    Rename,
    CancelAll
  };
  Q_ENUM(ConflictAction)

  struct ConflictInfo {
    Op op;
    QString srcPath;
    QString dstPath;
    bool isDir{false};

    qint64 srcSize{-1};
    qint64 dstSize{-1};
    QDateTime srcMtime;
    QDateTime dstMtime;
  };

  struct ConflictResult {
    ConflictAction action{ConflictAction::Skip};
    QString newName; // for Rename: destination base name
  };

  using ConflictHandler = std::function<ConflictResult(const ConflictInfo&)>;

  explicit FileOps(QObject *parent = nullptr);

  void setConflictHandler(ConflictHandler h);

  Q_INVOKABLE void requestCancel();
  Q_INVOKABLE void clearCancel();

  bool copyPaths(const QStringList &sources, const QString &destDir, QString *errorOut = nullptr);
  bool movePaths(const QStringList &sources, const QString &destDir, QString *errorOut = nullptr);
  bool deletePathsPermanently(const QStringList &paths, QString *errorOut = nullptr);
  bool trashPaths(const QStringList &paths, QString *errorOut = nullptr);

signals:
  // Item-based
  void started(FileOps::Op op, int totalItems);
  void progress(FileOps::Op op, const QString &currentPath, int doneItems, int totalItems);
  void finished(FileOps::Op op, bool ok, const QString &errorMessage);

  // Byte-based
  void bytesTotal(FileOps::Op op, qint64 totalBytes);
  void bytesProgress(FileOps::Op op, const QString &currentPath, qint64 doneBytes, qint64 totalBytes);

private:
  bool isCancelled() const;

  static QString homeTrashFilesDir();
  static QString homeTrashInfoDir();
  static bool ensureHomeTrash(QString *errorOut);

  qint64 computeTotalBytes(const QStringList &paths, QString *errorOut);
  qint64 computeSinglePathBytes(const QString &path);

  bool copyRecursively(const QString &srcPath, const QString &dstPath, qint64 &doneBytes, qint64 totalBytes, QString *errorOut);
  bool moveRecursively(const QString &srcPath, const QString &dstPath, qint64 &doneBytes, qint64 totalBytes, QString *errorOut);
  bool removeRecursively(const QString &path, QString *errorOut);

  bool copyFileWithProgress(const QString &srcFile, const QString &dstFile, qint64 &doneBytes, qint64 totalBytes, QString *errorOut);

  static QString uniquePathInDir(const QString &dirPath, const QString &baseName);
  static QString encodeTrashInfoPath(const QString &absOriginalPath);
  static bool writeTrashInfo(const QString &infoFilePath,
                             const QString &absOriginalPath,
                             QString *errorOut);

  bool copyOrMoveImpl(Op op, const QStringList &sources, const QString &destDir, QString *errorOut);

  // Conflict handling
  ConflictResult resolveConflict(const ConflictInfo &info);
  QString applyRenameToDst(const QString &dstPath, const QString &newName) const;

private:
  std::atomic_bool cancelRequested_{false};
  ConflictHandler conflictHandler_;
};
