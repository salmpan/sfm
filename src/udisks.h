#pragma once

#include <QObject>
#include <QString>
#include <QStringList>

class UDisks2 final : public QObject {
  Q_OBJECT
public:
  struct Device {
    QString objectPath;        // /org/freedesktop/UDisks2/block_devices/...
    QString driveObjectPath;   // /org/freedesktop/UDisks2/drives/...
    QString displayName;
    QStringList mountPoints;
    bool hasFilesystem{false};
    bool canUnmount{false};
    bool canEject{false};
  };

  explicit UDisks2(QObject *parent = nullptr);

  bool isAvailable() const;
  QList<Device> devices(QString *errorOut = nullptr) const;

  bool mount(const QString &blockObjectPath, QString *mountedAtOut, QString *errorOut = nullptr);
  bool unmount(const QString &blockObjectPath, QString *errorOut = nullptr);
  bool ejectDrive(const QString &driveObjectPath, QString *errorOut = nullptr);

signals:
  void devicesChanged();

private:
  static QString decodeMountPoint(const QByteArray &ba);
  static QString pickDisplayName(const QVariantMap &ifBlock,
                                const QVariantMap &ifFS,
                                const QVariantMap &ifPartition,
                                const QVariantMap &ifDrive,
                                const QVariantMap &ifDriveAta);
  void attachSignalWatches();

private:
  bool available_{false};
};
