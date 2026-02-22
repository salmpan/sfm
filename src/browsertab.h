#pragma once

#include <QStringList>
#include <QModelIndex>
#include <QPoint>
#include <QEvent>
#include <QWidget>
#include <QString>
#include <QTimer>
#include <vector>

class QFileSystemModel;
class QAbstractItemView;
class QTreeView;
class QListView;
class QStackedWidget;
class TrashView;
class FileSortProxyModel;

class BrowserTab final : public QWidget {
  Q_OBJECT
public:
  enum class ViewMode {
    GridIcons,  // QListView::IconMode
    List,       // QTreeView detailed
    Compact     // QListView::ListMode
  };

  enum class SortKey {
    Name = 0,
    Size = 1,
    Type = 2,
    Modified = 3
  };

  struct SortState {
    SortKey key{SortKey::Name};
    Qt::SortOrder order{Qt::AscendingOrder};
    bool foldersFirst{true};
  };

  explicit BrowserTab(QFileSystemModel *sharedModel, QWidget *parent = nullptr);

  QString location() const { return location_; }
  bool inTrash() const { return location_ == "trash:///"; }

  // Inline status bar helpers
  int itemCount() const;
  qint64 selectedSizeBytesFast() const; // files-only (dirs count as 0)
  QString storagePath() const;          // path used for free-space lookup

  struct SelectedSizeInfo {
    qint64 bytes{0};
    bool pending{false};
  };

  // Includes folders when their size is available; triggers async folder-size computation.
  SelectedSizeInfo selectedSizeInfo() const;

  void navigateTo(const QString &loc, bool pushHistory = true);
  void goBack();
  void goForward();
  void goUp();
  void refresh();

  bool canGoBack() const { return historyIndex_ > 0; }
  bool canGoForward() const { return historyIndex_ + 1 < (int)history_.size(); }
  bool canGoUp() const { return !inTrash() && location_ != "/"; }

  QStringList selectedPaths() const;
  void beginInlineRename();

  QStringList selectedTrashedPaths() const;
  bool trashDeleteSelected(QString *errorOut = nullptr);
  bool trashRestoreSelected(QString *errorOut = nullptr);

  // Applies to the active pane (filesystem or trash). Stored per pane.
  void setViewMode(ViewMode m);
  ViewMode viewMode() const { return inTrash() ? trashViewMode_ : fileViewMode_; }

  // Sorting applies to the active pane (filesystem or trash). Stored per pane.
  void setSort(SortKey key, Qt::SortOrder order);
  void setFoldersFirst(bool on);
  SortState sortState() const { return inTrash() ? trashSort_ : fileSort_; }

  TrashView* trashView() const { return trashView_; }

signals:
  void locationChanged(const QString &loc);
  void titleChanged(const QString &title);

  // Inline status bar notifications (emitted redundantly from multiple views).
  void selectionChanged();
  void itemCountChanged();
  void storageChanged();

  void requestNavigate(const QString &path);

  void openFolderInNewTabRequested(const QString &folderPath);

  void propertiesRequested(const QString &path);

  void createNewFolderRequested();
  void createNewDocumentRequested();

  void openWithDialogRequested(const QString &filePath);
  void openWithAppRequested(const QString &desktopId, const QString &filePath);

  // Clipboard actions
  void cutRequested();
  void copyRequested();
  void pasteRequested();
  void trashRequested();

private slots:
  void onActivated(const QModelIndex &idx);
  void onContextMenu(const QPoint &pos);
  void onCtrlEnter();
  void onHeaderSortChanged(int logicalIndex, Qt::SortOrder order);

protected:
  bool eventFilter(QObject *obj, QEvent *event) override;

private:
  QAbstractItemView* currentFileView() const;
  void schedulePrefetchVisibleFolderSizes_();
  void prefetchVisibleFolderSizes_();
  void hookPrefetchSignals_(QAbstractItemView *v);
  void setTabTitleFromLocation();
  void showFilePane();
  void showTrashPane();
  static bool isTrashUrl(const QString &s) { return s.startsWith("trash://"); }

  QString currentSelectedDirOrEmpty() const;
  void openInNewTabIfDir(const QString &path);

  QModelIndex toSourceIndex(const QModelIndex &proxyIdx) const;
  QString pathForIndex(const QModelIndex &proxyIdx) const;

private:
  QFileSystemModel *fsModel_{nullptr};
  FileSortProxyModel *fsProxy_{nullptr};

  // File views
  QStackedWidget *fileStack_{nullptr};
  QTreeView *listView_{nullptr};
  QListView *iconView_{nullptr};
  QListView *compactView_{nullptr};

  QTimer *prefetchTimer_{nullptr};

  ViewMode fileViewMode_{ViewMode::List};
  SortState fileSort_{};

  QStackedWidget *stack_{nullptr};
  TrashView *trashView_{nullptr};

  ViewMode trashViewMode_{ViewMode::List};
  SortState trashSort_{};

  QString location_{"/"};
  std::vector<QString> history_;
  int historyIndex_{-1};
};
