#include "fileops.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QDirIterator>
#include <QSaveFile>
#include <QTextStream>
#include <QDateTime>

FileOps::FileOps(QObject *parent) : QObject(parent) {}

void FileOps::setConflictHandler(ConflictHandler h) { conflictHandler_ = std::move(h); }

void FileOps::requestCancel() { cancelRequested_.store(true, std::memory_order_relaxed); }
void FileOps::clearCancel()   { cancelRequested_.store(false, std::memory_order_relaxed); }
bool FileOps::isCancelled() const { return cancelRequested_.load(std::memory_order_relaxed); }

QString FileOps::homeTrashFilesDir() {
  const QString xdgDataHome = qEnvironmentVariableIsSet("XDG_DATA_HOME")
      ? qEnvironmentVariable("XDG_DATA_HOME")
      : (QDir::homePath() + "/.local/share");
  return xdgDataHome + "/Trash/files";
}

QString FileOps::homeTrashInfoDir() {
  const QString xdgDataHome = qEnvironmentVariableIsSet("XDG_DATA_HOME")
      ? qEnvironmentVariable("XDG_DATA_HOME")
      : (QDir::homePath() + "/.local/share");
  return xdgDataHome + "/Trash/info";
}

bool FileOps::ensureHomeTrash(QString *errorOut) {
  QDir dir;
  const QString filesDir = homeTrashFilesDir();
  const QString infoDir  = homeTrashInfoDir();
  if (!dir.mkpath(filesDir)) {
    if (errorOut) *errorOut = "Failed to create trash files dir: " + filesDir;
    return false;
  }
  if (!dir.mkpath(infoDir)) {
    if (errorOut) *errorOut = "Failed to create trash info dir: " + infoDir;
    return false;
  }
  return true;
}

QString FileOps::uniquePathInDir(const QString &dirPath, const QString &baseName) {
  QFileInfo bi(baseName);
  const QString stem = bi.completeBaseName();
  const QString ext  = bi.suffix();

  QDir d(dirPath);
  if (!d.exists(baseName)) return d.filePath(baseName);

  for (int i = 1; i < 100000; ++i) {
    QString name = stem + " (" + QString::number(i) + ")";
    if (!ext.isEmpty()) name += "." + ext;
    if (!d.exists(name)) return d.filePath(name);
  }

  const QString ts = QString::number(QDateTime::currentMSecsSinceEpoch());
  QString name = stem + " (" + ts + ")";
  if (!ext.isEmpty()) name += "." + ext;
  return d.filePath(name);
}

QString FileOps::encodeTrashInfoPath(const QString &absOriginalPath) {
  const QString infoDir = homeTrashInfoDir();
  const QString base = QFileInfo(absOriginalPath).fileName();
  const QString baseInfo = base + ".trashinfo";
  return uniquePathInDir(infoDir, baseInfo);
}

bool FileOps::writeTrashInfo(const QString &infoFilePath,
                             const QString &absOriginalPath,
                             QString *errorOut) {
  QString safePath = absOriginalPath;
  safePath.replace('\n', '_');
  safePath.replace('\r', '_');

  const QString deletionDate = QDateTime::currentDateTimeUtc().toString("yyyy-MM-ddThh:mm:ss");

  QSaveFile f(infoFilePath);
  if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) {
    if (errorOut) *errorOut = "Failed to create trashinfo: " + infoFilePath;
    return false;
  }

  QTextStream out(&f);
  out.setEncoding(QStringConverter::Utf8);
  out << "[Trash Info]\n";
  out << "Path=" << safePath << "\n";
  out << "DeletionDate=" << deletionDate << "\n";

  if (!f.commit()) {
    if (errorOut) *errorOut = "Failed to write trashinfo: " + infoFilePath;
    return false;
  }
  return true;
}

qint64 FileOps::computeTotalBytes(const QStringList &paths, QString *errorOut) {
  qint64 total = 0;

  for (const QString &p : paths) {
    if (isCancelled()) {
      if (errorOut) *errorOut = "Cancelled.";
      return -1;
    }

    QFileInfo info(p);
    if (!info.exists()) continue;
    if (info.isSymLink()) continue;

    if (info.isFile()) { total += info.size(); continue; }

    if (info.isDir()) {
      QDirIterator it(p, QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot | QDir::Hidden,
                      QDirIterator::Subdirectories);
      while (it.hasNext()) {
        if (isCancelled()) {
          if (errorOut) *errorOut = "Cancelled.";
          return -1;
        }
        it.next();
        QFileInfo e = it.fileInfo();
        if (e.isSymLink()) continue;
        if (e.isFile()) total += e.size();
      }
    }
  }

  return total;
}

qint64 FileOps::computeSinglePathBytes(const QString &path) {
  QFileInfo info(path);
  if (!info.exists() || info.isSymLink()) return 0;
  if (info.isFile()) return info.size();

  qint64 total = 0;
  if (info.isDir()) {
    QDirIterator it(path, QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot | QDir::Hidden,
                    QDirIterator::Subdirectories);
    while (it.hasNext()) {
      it.next();
      QFileInfo e = it.fileInfo();
      if (e.isSymLink()) continue;
      if (e.isFile()) total += e.size();
    }
  }
  return total;
}

bool FileOps::copyFileWithProgress(const QString &srcFile,
                                   const QString &dstFile,
                                   qint64 &doneBytes,
                                   qint64 totalBytes,
                                   QString *errorOut) {
  if (isCancelled()) { if (errorOut) *errorOut = "Cancelled."; return false; }

  QFile in(srcFile);
  if (!in.open(QIODevice::ReadOnly)) {
    if (errorOut) *errorOut = "Failed to open for read: " + srcFile;
    return false;
  }

  QSaveFile out(dstFile);
  if (!out.open(QIODevice::WriteOnly)) {
    if (errorOut) *errorOut = "Failed to open for write: " + dstFile;
    return false;
  }

  static constexpr qint64 kBuf = 1024 * 1024;
  QByteArray buf;
  buf.resize((int)kBuf);

  while (true) {
    if (isCancelled()) {
      out.cancelWriting();
      if (errorOut) *errorOut = "Cancelled.";
      return false;
    }

    const qint64 n = in.read(buf.data(), buf.size());
    if (n < 0) {
      out.cancelWriting();
      if (errorOut) *errorOut = "Read error: " + srcFile;
      return false;
    }
    if (n == 0) break;

    const qint64 w = out.write(buf.constData(), n);
    if (w != n) {
      out.cancelWriting();
      if (errorOut) *errorOut = "Write error: " + dstFile;
      return false;
    }

    doneBytes += n;
    emit bytesProgress(Op::Copy, srcFile, doneBytes, totalBytes);
  }

  if (!out.commit()) {
    if (errorOut) *errorOut = "Failed to commit file: " + dstFile;
    return false;
  }
  return true;
}

bool FileOps::removeRecursively(const QString &path, QString *errorOut) {
  if (isCancelled()) { if (errorOut) *errorOut = "Cancelled."; return false; }

  QFileInfo info(path);
  if (!info.exists()) return true;

  if (info.isSymLink() || info.isFile()) {
    if (!QFile::remove(path)) {
      if (errorOut) *errorOut = "Failed to remove file: " + path;
      return false;
    }
    return true;
  }

  if (!info.isDir()) {
    if (errorOut) *errorOut = "Unsupported file type for delete: " + path;
    return false;
  }

  QDir dir(path);
  const QFileInfoList entries = dir.entryInfoList(
      QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden,
      QDir::Name | QDir::DirsFirst);

  for (const QFileInfo &e : entries) {
    if (!removeRecursively(e.absoluteFilePath(), errorOut)) return false;
  }

  if (!dir.rmdir(path)) {
    if (errorOut) *errorOut = "Failed to remove directory: " + path;
    return false;
  }
  return true;
}

FileOps::ConflictResult FileOps::resolveConflict(const ConflictInfo &info) {
  if (!conflictHandler_) {
    ConflictResult r;
    r.action = info.isDir ? ConflictAction::Merge : ConflictAction::Overwrite;
    return r;
  }
  return conflictHandler_(info);
}

QString FileOps::applyRenameToDst(const QString &dstPath, const QString &newName) const {
  const QString dir = QFileInfo(dstPath).absolutePath();
  return QDir(dir).filePath(newName);
}

bool FileOps::copyRecursively(const QString &srcPath,
                              const QString &dstPath,
                              qint64 &doneBytes,
                              qint64 totalBytes,
                              QString *errorOut) {
  if (isCancelled()) { if (errorOut) *errorOut = "Cancelled."; return false; }

  QFileInfo srcInfo(srcPath);
  if (!srcInfo.exists()) {
    if (errorOut) *errorOut = "Source does not exist: " + srcPath;
    return false;
  }

  QFileInfo dstInfo(dstPath);
  if (dstInfo.exists()) {
    ConflictInfo ci;
    ci.op = Op::Copy;
    ci.srcPath = srcPath;
    ci.dstPath = dstPath;
    ci.isDir = srcInfo.isDir();
    ci.srcSize = srcInfo.isFile() ? srcInfo.size() : -1;
    ci.dstSize = dstInfo.isFile() ? dstInfo.size() : -1;
    ci.srcMtime = srcInfo.lastModified();
    ci.dstMtime = dstInfo.lastModified();

    ConflictResult cr = resolveConflict(ci);
    if (cr.action == ConflictAction::CancelAll) {
      requestCancel();
      if (errorOut) *errorOut = "Cancelled.";
      return false;
    }
    if (cr.action == ConflictAction::Skip) {
      doneBytes += computeSinglePathBytes(srcPath);
      emit bytesProgress(Op::Copy, srcPath, doneBytes, totalBytes);
      return true;
    }
    if (cr.action == ConflictAction::Rename) {
      QString newDst = applyRenameToDst(dstPath, cr.newName);
      newDst = uniquePathInDir(QFileInfo(newDst).absolutePath(), QFileInfo(newDst).fileName());
      return copyRecursively(srcPath, newDst, doneBytes, totalBytes, errorOut);
    }
    if (cr.action == ConflictAction::Overwrite) {
      if (!removeRecursively(dstPath, errorOut)) return false;
    }
    if (cr.action == ConflictAction::Merge) {
      if (!srcInfo.isDir()) {
        if (!removeRecursively(dstPath, errorOut)) return false;
      }
    }
  }

  if (srcInfo.isSymLink()) {
    const QString target = QFileInfo(srcPath).symLinkTarget();
    if (QFile::exists(dstPath)) QFile::remove(dstPath);
    if (!QFile::link(target, dstPath)) {
      if (errorOut) *errorOut = "Failed to copy symlink: " + srcPath;
      return false;
    }
    return true;
  }

  if (srcInfo.isFile()) {
    QDir().mkpath(QFileInfo(dstPath).absolutePath());
    if (QFile::exists(dstPath)) QFile::remove(dstPath);
    if (!copyFileWithProgress(srcPath, dstPath, doneBytes, totalBytes, errorOut)) return false;
    return true;
  }

  if (!srcInfo.isDir()) {
    if (errorOut) *errorOut = "Unsupported file type: " + srcPath;
    return false;
  }

  if (!QDir().mkpath(dstPath)) {
    if (errorOut) *errorOut = "Failed to create directory: " + dstPath;
    return false;
  }

  QDir srcDir(srcPath);
  const QFileInfoList entries = srcDir.entryInfoList(
      QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden,
      QDir::Name | QDir::DirsFirst);

  for (const QFileInfo &e : entries) {
    if (isCancelled()) { if (errorOut) *errorOut = "Cancelled."; return false; }
    const QString childSrc = e.absoluteFilePath();
    const QString childDst = QDir(dstPath).filePath(e.fileName());
    if (!copyRecursively(childSrc, childDst, doneBytes, totalBytes, errorOut)) return false;
  }
  return true;
}

bool FileOps::moveRecursively(const QString &srcPath,
                              const QString &dstPath,
                              qint64 &doneBytes,
                              qint64 totalBytes,
                              QString *errorOut) {
  if (isCancelled()) { if (errorOut) *errorOut = "Cancelled."; return false; }

  QFileInfo srcInfo(srcPath);
  if (!srcInfo.exists()) {
    if (errorOut) *errorOut = "Source does not exist: " + srcPath;
    return false;
  }

  QFileInfo dstInfo(dstPath);
  if (dstInfo.exists()) {
    ConflictInfo ci;
    ci.op = Op::Move;
    ci.srcPath = srcPath;
    ci.dstPath = dstPath;
    ci.isDir = srcInfo.isDir();
    ci.srcSize = srcInfo.isFile() ? srcInfo.size() : -1;
    ci.dstSize = dstInfo.isFile() ? dstInfo.size() : -1;
    ci.srcMtime = srcInfo.lastModified();
    ci.dstMtime = dstInfo.lastModified();

    ConflictResult cr = resolveConflict(ci);
    if (cr.action == ConflictAction::CancelAll) {
      requestCancel();
      if (errorOut) *errorOut = "Cancelled.";
      return false;
    }
    if (cr.action == ConflictAction::Skip) {
      doneBytes += computeSinglePathBytes(srcPath);
      emit bytesProgress(Op::Move, srcPath, doneBytes, totalBytes);
      return true;
    }
    if (cr.action == ConflictAction::Rename) {
      QString newDst = applyRenameToDst(dstPath, cr.newName);
      newDst = uniquePathInDir(QFileInfo(newDst).absolutePath(), QFileInfo(newDst).fileName());
      return moveRecursively(srcPath, newDst, doneBytes, totalBytes, errorOut);
    }
    if (cr.action == ConflictAction::Merge) {
      if (srcInfo.isDir() && dstInfo.isDir()) {
        QDir srcDir(srcPath);
        const QFileInfoList entries = srcDir.entryInfoList(
            QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden,
            QDir::Name | QDir::DirsFirst);

        for (const QFileInfo &e : entries) {
          if (isCancelled()) { if (errorOut) *errorOut = "Cancelled."; return false; }
          const QString childSrc = e.absoluteFilePath();
          const QString childDst = QDir(dstPath).filePath(e.fileName());
          if (!moveRecursively(childSrc, childDst, doneBytes, totalBytes, errorOut)) return false;
        }

        // Remove empty source directory
        QDir s(srcPath);
        if (!s.rmdir(".")) {
          QString err;
          if (!removeRecursively(srcPath, &err)) {
            if (errorOut) *errorOut = err;
            return false;
          }
        }
        return true;
      } else {
        if (!removeRecursively(dstPath, errorOut)) return false;
      }
    }
    if (cr.action == ConflictAction::Overwrite) {
      if (!removeRecursively(dstPath, errorOut)) return false;
    }
  }

  QDir().mkpath(QFileInfo(dstPath).absolutePath());

  // fast rename
  if (QFile::rename(srcPath, dstPath)) {
    const qint64 subtree = computeSinglePathBytes(dstPath);
    if (subtree > 0) {
      doneBytes += subtree;
      emit bytesProgress(Op::Move, dstPath, doneBytes, totalBytes);
    }
    return true;
  }

  // cross-device
  if (!copyRecursively(srcPath, dstPath, doneBytes, totalBytes, errorOut)) return false;
  if (!removeRecursively(srcPath, errorOut)) return false;
  return true;
}

bool FileOps::copyOrMoveImpl(Op op, const QStringList &sources, const QString &destDir, QString *errorOut) {
  clearCancel();

  QFileInfo dd(destDir);
  if (!dd.exists() || !dd.isDir()) {
    if (errorOut) *errorOut = "Destination is not a directory: " + destDir;
    emit finished(op, false, errorOut ? *errorOut : QString());
    return false;
  }

  const int totalItems = sources.size();
  emit started(op, totalItems);

  QString scanErr;
  const qint64 totalBytes = computeTotalBytes(sources, &scanErr);
  if (totalBytes < 0) {
    if (errorOut) *errorOut = scanErr.isEmpty() ? "Cancelled." : scanErr;
    emit finished(op, false, *errorOut);
    return false;
  }
  emit bytesTotal(op, totalBytes);

  qint64 doneBytes = 0;
  emit bytesProgress(op, QString(), doneBytes, totalBytes);

  int doneItems = 0;
  for (const QString &src : sources) {
    if (isCancelled()) {
      const QString err = "Cancelled.";
      if (errorOut) *errorOut = err;
      emit finished(op, false, err);
      return false;
    }

    QFileInfo si(src);
    const QString dst = QDir(destDir).filePath(si.fileName());

    emit progress(op, src, doneItems, totalItems);
    emit bytesProgress(op, src, doneBytes, totalBytes);

    QString err;
    bool ok = false;
    if (op == Op::Copy) ok = copyRecursively(src, dst, doneBytes, totalBytes, &err);
    else                ok = moveRecursively(src, dst, doneBytes, totalBytes, &err);

    if (!ok) {
      if (errorOut) *errorOut = err;
      emit finished(op, false, err);
      return false;
    }

    doneItems++;
    emit progress(op, src, doneItems, totalItems);
  }

  emit bytesProgress(op, QString(), totalBytes, totalBytes);
  emit finished(op, true, QString());
  return true;
}

bool FileOps::copyPaths(const QStringList &sources, const QString &destDir, QString *errorOut) {
  return copyOrMoveImpl(Op::Copy, sources, destDir, errorOut);
}

bool FileOps::movePaths(const QStringList &sources, const QString &destDir, QString *errorOut) {
  return copyOrMoveImpl(Op::Move, sources, destDir, errorOut);
}

bool FileOps::deletePathsPermanently(const QStringList &paths, QString *errorOut) {
  clearCancel();

  const int total = paths.size();
  emit started(Op::Delete, total);

  int done = 0;
  for (const QString &p : paths) {
    if (isCancelled()) {
      const QString err = "Cancelled.";
      if (errorOut) *errorOut = err;
      emit finished(Op::Delete, false, err);
      return false;
    }

    emit progress(Op::Delete, p, done, total);
    QString err;
    if (!removeRecursively(p, &err)) {
      if (errorOut) *errorOut = err;
      emit finished(Op::Delete, false, err);
      return false;
    }
    done++;
    emit progress(Op::Delete, p, done, total);
  }

  emit finished(Op::Delete, true, QString());
  return true;
}

bool FileOps::trashPaths(const QStringList &paths, QString *errorOut) {
  clearCancel();

  if (!ensureHomeTrash(errorOut)) {
    emit finished(Op::Trash, false, errorOut ? *errorOut : QString("Failed to ensure trash."));
    return false;
  }

  const QString filesDir = homeTrashFilesDir();
  const int totalItems = paths.size();
  emit started(Op::Trash, totalItems);

  QString scanErr;
  const qint64 totalBytes = computeTotalBytes(paths, &scanErr);
  if (totalBytes < 0) {
    if (errorOut) *errorOut = scanErr.isEmpty() ? "Cancelled." : scanErr;
    emit finished(Op::Trash, false, *errorOut);
    return false;
  }
  emit bytesTotal(Op::Trash, totalBytes);

  qint64 doneBytes = 0;
  emit bytesProgress(Op::Trash, QString(), doneBytes, totalBytes);

  int doneItems = 0;
  for (const QString &p : paths) {
    if (isCancelled()) {
      const QString err = "Cancelled.";
      if (errorOut) *errorOut = err;
      emit finished(Op::Trash, false, err);
      return false;
    }

    QFileInfo pi(p);
    if (!pi.exists()) { doneItems++; continue; }

    const QString abs = pi.absoluteFilePath();
    emit progress(Op::Trash, abs, doneItems, totalItems);
    emit bytesProgress(Op::Trash, abs, doneBytes, totalBytes);

    const QString trashTarget = uniquePathInDir(filesDir, pi.fileName());
    const QString infoFile    = encodeTrashInfoPath(abs);

    QString err;
    if (!writeTrashInfo(infoFile, abs, &err)) {
      if (errorOut) *errorOut = err;
      emit finished(Op::Trash, false, err);
      return false;
    }

    if (!moveRecursively(abs, trashTarget, doneBytes, totalBytes, &err)) {
      QFile::remove(infoFile);
      if (errorOut) *errorOut = err;
      emit finished(Op::Trash, false, err);
      return false;
    }

    doneItems++;
    emit progress(Op::Trash, abs, doneItems, totalItems);
  }

  emit bytesProgress(Op::Trash, QString(), totalBytes, totalBytes);
  emit finished(Op::Trash, true, QString());
  return true;
}
