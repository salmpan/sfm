#pragma once

#include <QWidget>
#include <QStringList>

class QListWidget;
class QListWidgetItem;

class TrashView final : public QWidget {
  Q_OBJECT
public:
  explicit TrashView(QWidget *parent = nullptr);

  void refresh();

  // Returns absolute paths inside Trash/files for selected items.
  QStringList selectedTrashedPaths() const;

  // Permanently delete selected from Trash/files and matching .trashinfo entries.
  bool deleteSelectedPermanently(QString *errorOut = nullptr);

  // Restore selected to original location (from .trashinfo).
  bool restoreSelected(QString *errorOut = nullptr);

signals:
  // request to navigate into a trashed folder (path inside Trash/files)
  void requestNavigate(const QString &path);

private slots:
  void onItemActivated(QListWidgetItem *it);
  void onContextMenu(const QPoint &pos);

private:
  static QString xdgDataHome();
  static QString trashFilesDir();
  static QString trashInfoDir();

  static QString infoFileForTrashedName(const QString &trashedName);
  static QString originalPathFromTrashInfo(const QString &trashInfoPath);

private:
  QListWidget *list_{nullptr};
};
