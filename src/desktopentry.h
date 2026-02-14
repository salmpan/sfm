#pragma once

#include <QString>
#include <QIcon>

struct DesktopEntry {
  QString desktopId;
  QString name;
  QString exec;
  QString iconName;
  bool noDisplay{false};

  QIcon icon() const;
};

bool loadDesktopEntry(const QString &desktopId, DesktopEntry *out);
QString findDesktopFilePath(const QString &desktopId);
