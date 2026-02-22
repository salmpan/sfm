#pragma once

#include <QIcon>
#include <QString>
#include <QStringList>
#include <QStyle>

class QWidget;

namespace IconUtil {

// Try theme names (in order).
// Then fallback theme names.
// Then optional Qt standard icon.
// Otherwise return empty QIcon.
QIcon fromTheme(const QString &name,
                const QStringList &fallbackThemeNames = {},
                QStyle::StandardPixmap fallbackStandardPixmap = QStyle::SP_CustomBase,
                QWidget *styleContext = nullptr);

QIcon fromTheme(const QStringList &names,
                const QStringList &fallbackThemeNames = {},
                QStyle::StandardPixmap fallbackStandardPixmap = QStyle::SP_CustomBase,
                QWidget *styleContext = nullptr);

} // namespace IconUtil