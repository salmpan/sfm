#pragma once

#include <QModelIndex>
#include <QWidget>
#include <QStringList>

class QAbstractItemView;
class QTreeView;
class QListView;
class QSortFilterProxyModel;
class QStackedWidget;
class TrashModel;

class TrashView final : public QWidget {
  Q_OBJECT
public:
  enum class ViewMode {
    GridIcons,
    List,
    Compact
  };

  explicit TrashView(QWidget *parent = nullptr);

  void refresh();

  void setViewMode(ViewMode m);
  ViewMode viewMode() const { return viewMode_; }

  void setSort(int column, Qt::SortOrder order);
  int sortColumn() const { return sortColumn_; }
  Qt::SortOrder sortOrder() const { return sortOrder_; }

  void setFoldersFirst(bool on);
  bool foldersFirst() const { return foldersFirst_; }

  // Returns absolute paths inside Trash/files for selected items.
  QStringList selectedTrashedPaths() const;

  // Inline status bar helpers
  int itemCount() const;

  // Permanently delete selected from Trash/files and matching .trashinfo entries.
  bool deleteSelectedPermanently(QString *errorOut = nullptr);

  // Restore selected to original location (from .trashinfo).
  bool restoreSelected(QString *errorOut = nullptr);

signals:
  // request to navigate into a trashed folder (path inside Trash/files)
  void requestNavigate(const QString &path);

  // Inline status bar notifications
  void selectionChanged();
  void itemCountChanged();

private slots:
  void onActivated(const QModelIndex &idx);
  void onContextMenu(const QPoint &pos);

private:
  QAbstractItemView* currentView() const;
  QModelIndexList selectedRows() const;

private:
  TrashModel *model_{nullptr};
  QSortFilterProxyModel *proxy_{nullptr};

  QStackedWidget *stack_{nullptr};
  QTreeView *listView_{nullptr};
  QListView *iconView_{nullptr};
  QListView *compactView_{nullptr};

  ViewMode viewMode_{ViewMode::List};

  int sortColumn_{0};
  Qt::SortOrder sortOrder_{Qt::AscendingOrder};
  bool foldersFirst_{true};
};
