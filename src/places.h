#pragma once

#include <QWidget>
#include <QString>

class QListView;
class QStandardItemModel;
class QStandardItem;
class UDisks2;

class PlacesSidebar final : public QWidget {
  Q_OBJECT
public:
  explicit PlacesSidebar(QWidget *parent = nullptr);
  void refresh();

signals:
  void placeActivated(const QString &path);
  void emptyTrashRequested();

private slots:
  void onActivated(const QModelIndex &index);
  void onContextMenu(const QPoint &pos);

private:
  enum class Section { Places, Devices };
  enum Roles {
    RolePath = Qt::UserRole + 1,
    RoleSection,
    RoleKind,
    RoleUDisksBlockPath,
    RoleUDisksDrivePath,
    RoleIsMounted,
    RoleCanEject
  };

  enum class Kind { Normal, Trash, Device };

  void buildStaticPlaces();
  void buildDeviceSection();

  void addSectionHeader(const QString &title, Section section);
  QStandardItem* addPlaceItem(const QString &label,
                              const QString &path,
                              const QString &iconName,
                              Section section,
                              Kind kind);

  void updateTrashBadge();

  static int trashCount();
  static QString xdgDataHome();

private:
  QListView *view_{nullptr};
  QStandardItemModel *model_{nullptr};

  QStandardItem *trashItem_{nullptr};

  UDisks2 *udisks_{nullptr};
};
