#pragma once

#include <QDialog>
#include <QString>
#include <QList>

class QListWidget;
class QLabel;
class QPushButton;

class OpenWithDialog final : public QDialog {
  Q_OBJECT
public:
  struct AppInfo {
    QString desktopId;
    QString name;
    QString iconName;
  };

  explicit OpenWithDialog(const QString &filePath, QWidget *parent = nullptr);

  QString selectedDesktopId() const;
  bool defaultWasSet() const { return defaultSet_; }

  static bool launchWithDesktopId(const QString &desktopId, const QString &filePath, QString *errorOut);
  static bool setDefaultForMime(const QString &desktopId, const QString &mime, QString *errorOut);

  static QString detectMime(const QString &filePath);
  static QString queryDefaultDesktopId(const QString &mime);
  static QList<AppInfo> queryAppsForMime(const QString &mime, int limit = 0);

private slots:
  void refresh();
  void onSelectionChanged();
  void onOpenClicked();
  void onSetDefaultClicked();

private:
  struct AppRow {
    QString desktopId;
    QString name;
    QString iconName;
    bool noDisplay{false};
  };

  static QList<AppRow> queryRegisteredAppsInternal(const QString &mime);
  void populateList();

private:
  QString filePath_;
  QString mime_;

  QListWidget *list_{nullptr};
  QLabel *lblInfo_{nullptr};
  QLabel *lblDefault_{nullptr};

  QPushButton *btnOpen_{nullptr};
  QPushButton *btnSetDefault_{nullptr};

  bool defaultSet_{false};
};
