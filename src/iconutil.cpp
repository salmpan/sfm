#include "iconutil.h"

#include <QApplication>
#include <QStyle>
#include <QWidget>

namespace {

static QIcon firstThemeIcon(const QStringList &names) {
    for (const auto &n : names) {
        if (n.trimmed().isEmpty()) continue;
        QIcon ic = QIcon::fromTheme(n);
        if (!ic.isNull()) return ic;
    }
    return {};
}

static QIcon standardIcon(QStyle::StandardPixmap sp, QWidget *ctx) {
    if (sp == QStyle::SP_CustomBase) return {};
    QStyle *st = ctx ? ctx->style() : QApplication::style();
    return st ? st->standardIcon(sp) : QIcon();
}

} // namespace

namespace IconUtil {

QIcon fromTheme(const QString &name,
                const QStringList &fallbackThemeNames,
                QStyle::StandardPixmap fallbackStandardPixmap,
                QWidget *styleContext) {
    return fromTheme(QStringList{name}, fallbackThemeNames,
                     fallbackStandardPixmap, styleContext);
}

QIcon fromTheme(const QStringList &names,
                const QStringList &fallbackThemeNames,
                QStyle::StandardPixmap fallbackStandardPixmap,
                QWidget *styleContext) {

    if (QIcon ic = firstThemeIcon(names); !ic.isNull())
        return ic;

    if (QIcon ic = firstThemeIcon(fallbackThemeNames); !ic.isNull())
        return ic;

    if (QIcon ic = standardIcon(fallbackStandardPixmap, styleContext); !ic.isNull())
        return ic;

    return {};
}

} // namespace IconUtil