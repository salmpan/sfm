#include "desktopentry.h"

#include <QIcon>
#include <QDir>
#include <QFileInfo>
#include <QSettings>
#include <QStringList>
#include <QLocale>

static QStringList xdgDataDirs() {
  const QString env = qEnvironmentVariable("XDG_DATA_DIRS");
  if (!env.isEmpty()) return env.split(':', Qt::SkipEmptyParts);
  return {"/usr/local/share", "/usr/share"};
}

static QString xdgDataHome() {
  const QString env = qEnvironmentVariable("XDG_DATA_HOME");
  if (!env.isEmpty()) return env;
  return QDir::homePath() + "/.local/share";
}

QString findDesktopFilePath(const QString &desktopId) {
  const QString fileName = desktopId.endsWith(".desktop") ? desktopId : (desktopId + ".desktop");

  {
    const QString p = xdgDataHome() + "/applications/" + fileName;
    if (QFileInfo::exists(p)) return p;
  }

  for (const QString &d : xdgDataDirs()) {
    const QString p = d + "/applications/" + fileName;
    if (QFileInfo::exists(p)) return p;
  }

  {
    const QString p1 = QDir::homePath() + "/.local/share/flatpak/exports/share/applications/" + fileName;
    if (QFileInfo::exists(p1)) return p1;
    const QString p2 = "/var/lib/flatpak/exports/share/applications/" + fileName;
    if (QFileInfo::exists(p2)) return p2;
  }

  return {};
}

static QString pickLocalized(QSettings &ini, const QString &baseKey) {
  const QString locale = QLocale().name(); // en_US
  const QString lang = locale.left(2);

  const QString k1 = baseKey + "[" + locale + "]";
  if (ini.contains(k1)) return ini.value(k1).toString();

  const QString k2 = baseKey + "[" + lang + "]";
  if (ini.contains(k2)) return ini.value(k2).toString();

  if (ini.contains(baseKey)) return ini.value(baseKey).toString();
  return {};
}

QIcon DesktopEntry::icon() const {
  if (iconName.isEmpty()) return QIcon();
  return QIcon::fromTheme(iconName);
}

bool loadDesktopEntry(const QString &desktopId, DesktopEntry *out) {
  if (!out) return false;

  const QString path = findDesktopFilePath(desktopId);
  if (path.isEmpty()) return false;

  QSettings ini(path, QSettings::IniFormat);
  // ini.setIniCodec("UTF-8");
  ini.beginGroup("Desktop Entry");

  DesktopEntry e;
  e.desktopId = desktopId.endsWith(".desktop") ? desktopId : (desktopId + ".desktop");
  e.name = pickLocalized(ini, "Name");
  e.exec = ini.value("Exec").toString();
  e.iconName = ini.value("Icon").toString();
  e.noDisplay = ini.value("NoDisplay").toBool() || ini.value("Hidden").toBool();

  ini.endGroup();

  if (e.name.isEmpty()) e.name = QFileInfo(e.desktopId).completeBaseName();

  *out = e;
  return true;
}
