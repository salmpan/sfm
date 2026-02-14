#include "udisks.h"

#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusMessage>
#include <QDBusReply>
#include <QDBusArgument>
#include <QDBusObjectPath>
#include <QVariantMap>
#include <QHash>
#include <QApplication>
#include <algorithm>

static const char *UDISKS_SERVICE = "org.freedesktop.UDisks2";
static const char *UDISKS_PATH    = "/org/freedesktop/UDisks2";
static const char *OBJMGR_IFACE   = "org.freedesktop.DBus.ObjectManager";

static const char *IF_BLOCK       = "org.freedesktop.UDisks2.Block";
static const char *IF_FS          = "org.freedesktop.UDisks2.Filesystem";
static const char *IF_PART        = "org.freedesktop.UDisks2.Partition";
static const char *IF_DRIVE       = "org.freedesktop.UDisks2.Drive";
static const char *IF_DRIVE_ATA   = "org.freedesktop.UDisks2.Drive.Ata";

UDisks2::UDisks2(QObject *parent) : QObject(parent) {
  QDBusConnection bus = QDBusConnection::systemBus();
  if (!bus.isConnected()) { available_ = false; return; }
  QDBusInterface mgr(UDISKS_SERVICE, UDISKS_PATH, OBJMGR_IFACE, bus);
  available_ = mgr.isValid();
  if (available_) attachSignalWatches();
}

bool UDisks2::isAvailable() const { return available_; }

void UDisks2::attachSignalWatches() {
  QDBusConnection bus = QDBusConnection::systemBus();
  bus.connect(UDISKS_SERVICE, UDISKS_PATH, OBJMGR_IFACE, "InterfacesAdded",
              this, SIGNAL(devicesChanged()));
  bus.connect(UDISKS_SERVICE, UDISKS_PATH, OBJMGR_IFACE, "InterfacesRemoved",
              this, SIGNAL(devicesChanged()));
}

QString UDisks2::decodeMountPoint(const QByteArray &ba) {
  QByteArray s = ba;
  int nul = s.indexOf('\0');
  if (nul >= 0) s.truncate(nul);
  return QString::fromUtf8(s);
}

QString UDisks2::pickDisplayName(const QVariantMap &ifBlock,
                                 const QVariantMap &ifFS,
                                 const QVariantMap &ifPartition,
                                 const QVariantMap &ifDrive,
                                 const QVariantMap &ifDriveAta) {
  auto bytesToQString = [](const QVariant &v) -> QString {
    if (v.canConvert<QByteArray>()) {
      QByteArray b = v.toByteArray();
      int nul = b.indexOf('\0');
      if (nul >= 0) b.truncate(nul);
      return QString::fromUtf8(b).trimmed();
    }
    return v.toString().trimmed();
  };

  QString label = bytesToQString(ifFS.value("IdLabel"));
  if (!label.isEmpty()) return label;

  QString pname = bytesToQString(ifPartition.value("Name"));
  if (!pname.isEmpty()) return pname;

  QString vendor = bytesToQString(ifDrive.value("Vendor"));
  QString model  = bytesToQString(ifDrive.value("Model"));
  QString combined = (vendor + " " + model).trimmed();
  if (!combined.isEmpty()) return combined;

  QString dev = bytesToQString(ifBlock.value("PreferredDevice"));
  if (!dev.isEmpty()) return dev;

  QString ata = bytesToQString(ifDriveAta.value("SmartModel"));
  if (!ata.isEmpty()) return ata;

  return "Device";
}

static bool getManagedObjects(QVariantMap *out, QString *errorOut) {
  QDBusConnection bus = QDBusConnection::systemBus();
  QDBusInterface mgr(UDISKS_SERVICE, UDISKS_PATH, OBJMGR_IFACE, bus);
  if (!mgr.isValid()) {
    if (errorOut) *errorOut = "UDisks2 ObjectManager not available.";
    return false;
  }

  QDBusMessage msg = mgr.call("GetManagedObjects");
  if (msg.type() == QDBusMessage::ErrorMessage) {
    if (errorOut) *errorOut = msg.errorMessage();
    return false;
  }
  if (msg.arguments().isEmpty()) {
    if (errorOut) *errorOut = "GetManagedObjects returned no arguments.";
    return false;
  }

  QVariant v = msg.arguments().at(0);
  if (!v.canConvert<QDBusArgument>()) {
    if (errorOut) *errorOut = "Unexpected GetManagedObjects return type.";
    return false;
  }

  QDBusArgument arg = v.value<QDBusArgument>();
  QVariantMap objects;
  arg >> objects;
  *out = objects;
  return true;
}

QList<UDisks2::Device> UDisks2::devices(QString *errorOut) const {
  QList<Device> out;
  if (!available_) { if (errorOut) *errorOut = "UDisks2 not available."; return out; }

  QVariantMap objects;
  if (!getManagedObjects(&objects, errorOut)) return out;

  QHash<QString, QVariantMap> drivePropsByPath;
  for (auto it = objects.begin(); it != objects.end(); ++it) {
    const QString objPath = it.key();
    if (!it.value().canConvert<QDBusArgument>()) continue;

    QVariantMap ifaceMap;
    it.value().value<QDBusArgument>() >> ifaceMap;

    if (ifaceMap.contains(IF_DRIVE)) {
      QVariantMap driveProps;
      ifaceMap.value(IF_DRIVE).value<QDBusArgument>() >> driveProps;
      drivePropsByPath.insert(objPath, driveProps);
    }
  }

  for (auto it = objects.begin(); it != objects.end(); ++it) {
    const QString objPath = it.key();
    if (!it.value().canConvert<QDBusArgument>()) continue;

    QVariantMap ifaceMap;
    it.value().value<QDBusArgument>() >> ifaceMap;

    if (!ifaceMap.contains(IF_BLOCK)) continue;
    if (!ifaceMap.contains(IF_FS)) continue;

    QVariantMap blockProps;
    ifaceMap.value(IF_BLOCK).value<QDBusArgument>() >> blockProps;

    QVariantMap fsProps;
    ifaceMap.value(IF_FS).value<QDBusArgument>() >> fsProps;

    QVariantMap partProps;
    if (ifaceMap.contains(IF_PART)) ifaceMap.value(IF_PART).value<QDBusArgument>() >> partProps;

    QVariantMap ataProps;
    if (ifaceMap.contains(IF_DRIVE_ATA)) ifaceMap.value(IF_DRIVE_ATA).value<QDBusArgument>() >> ataProps;

    QString driveObj;
    QVariant vDrive = blockProps.value("Drive");
    if (vDrive.canConvert<QDBusObjectPath>()) driveObj = vDrive.value<QDBusObjectPath>().path();
    else driveObj = vDrive.toString();

    QVariantMap driveProps;
    if (!driveObj.isEmpty() && drivePropsByPath.contains(driveObj)) driveProps = drivePropsByPath.value(driveObj);

    Device d;
    d.objectPath = objPath;
    d.driveObjectPath = driveObj;
    d.hasFilesystem = true;

    d.displayName = pickDisplayName(blockProps, fsProps, partProps, driveProps, ataProps);

    QStringList mps;
    const QVariant mpVar = fsProps.value("MountPoints");
    if (mpVar.canConvert<QDBusArgument>()) {
      QDBusArgument mpArg = mpVar.value<QDBusArgument>();
      QList<QByteArray> raw;
      mpArg >> raw;
      for (const QByteArray &ba : raw) {
        const QString mp = decodeMountPoint(ba);
        if (!mp.isEmpty()) mps << mp;
      }
    }
    d.mountPoints = mps;
    d.canUnmount = !d.mountPoints.isEmpty();

    if (!driveProps.isEmpty()) {
      const bool ejectable = driveProps.value("Ejectable").toBool();
      const bool removable = driveProps.value("Removable").toBool();
      d.canEject = ejectable || removable;
    }

    out << d;
  }

  std::sort(out.begin(), out.end(), [](const Device &a, const Device &b){
    const bool am = !a.mountPoints.isEmpty();
    const bool bm = !b.mountPoints.isEmpty();
    if (am != bm) return am > bm;
    return a.displayName.toLower() < b.displayName.toLower();
  });

  return out;
}

bool UDisks2::mount(const QString &blockObjectPath, QString *mountedAtOut, QString *errorOut) {
  QDBusConnection bus = QDBusConnection::systemBus();
  QDBusInterface fs(UDISKS_SERVICE, blockObjectPath, IF_FS, bus);
  if (!fs.isValid()) {
    if (errorOut) *errorOut = "Filesystem interface not available for: " + blockObjectPath;
    return false;
  }
  QVariantMap options;
  QDBusReply<QString> r = fs.call("Mount", options);
  if (!r.isValid()) { if (errorOut) *errorOut = r.error().message(); return false; }
  if (mountedAtOut) *mountedAtOut = r.value();
  return true;
}

bool UDisks2::unmount(const QString &blockObjectPath, QString *errorOut) {
  QDBusConnection bus = QDBusConnection::systemBus();
  QDBusInterface fs(UDISKS_SERVICE, blockObjectPath, IF_FS, bus);
  if (!fs.isValid()) {
    if (errorOut) *errorOut = "Filesystem interface not available for: " + blockObjectPath;
    return false;
  }
  QVariantMap options;
  QDBusReply<void> r = fs.call("Unmount", options);
  if (!r.isValid()) { if (errorOut) *errorOut = r.error().message(); return false; }
  return true;
}

bool UDisks2::ejectDrive(const QString &driveObjectPath, QString *errorOut) {
  if (driveObjectPath.isEmpty()) { if (errorOut) *errorOut = "No drive object to eject."; return false; }

  QDBusConnection bus = QDBusConnection::systemBus();
  QDBusInterface drive(UDISKS_SERVICE, driveObjectPath, IF_DRIVE, bus);
  if (!drive.isValid()) { if (errorOut) *errorOut = "Drive interface not available for: " + driveObjectPath; return false; }

  QVariantMap options;
  QDBusReply<void> r = drive.call("Eject", options);
  if (!r.isValid()) { if (errorOut) *errorOut = r.error().message(); return false; }
  return true;
}
