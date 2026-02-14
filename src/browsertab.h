#pragma once

#include <QWidget>
#include <QString>
#include <vector>

class QFileSystemModel;
class QAbstractItemView;
class QTreeView;
class QListView;
class QStackedWidget;
class TrashView;

class BrowserTab final : public QWidget {
  Q_OBJECT
public:
  enum class ViewMode {
    GridIcons,  // QListView::IconMode
    List,       // QTreeView detailed
    Compact     // QListView::ListMode
  };

  explicit BrowserTab(QFileSystemModel *sharedModel, QWidget *parent = nullptr);

  QString location() const { return location_; }
  bool inTrash() const { return location_ == "trash:///"; }

  void navigateTo(const QString &loc, bool pushHistory = true);
  void goBack();
  void goForward();
  void goUp();
  void refresh();

  bool canGoBack() const { return historyIndex_ > 0; }
  bool canGoForward() const { return historyIndex_ + 1 < (int)history_.size(); }
  bool canGoUp() const { return !inTrash() && location_ != "/"; }

  QStringList selectedPaths() const;

  QStringList selectedTrashedPaths() const;
  bool trashDeleteSelected(QString *errorOut = nullptr);
  bool trashRestoreSelected(QString *errorOut = nullptr);

  void setViewMode(ViewMode m);
  ViewMode viewMode() const { return viewMode_; }

  QTreeView* fileView() const { return listView_; }
  TrashView* trashView() const { return trashView_; }

signals:
  void locationChanged(const QString &loc);
  void titleChanged(const QString &title);

  void requestNavigate(const QString &path);

  void openFolderInNewTabRequested(const QString &folderPath);

  void propertiesRequested(const QString &path);

  void openWithDialogRequested(const QString &filePath);
  void openWithAppRequested(const QString &desktopId, const QString &filePath);

private slots:
  void onActivated(const QModelIndex &idx);
  void onContextMenu(const QPoint &pos);
  void onCtrlEnter();

protected:
  bool eventFilter(QObject *obj, QEvent *event) override;

private:
  QAbstractItemView* currentFileView() const;
  void setTabTitleFromLocation();
  void showFilePane();
  void showTrashPane();
  static bool isTrashUrl(const QString &s) { return s.startsWith("trash://"); }

  QString currentSelectedDirOrEmpty() const;
  void openInNewTabIfDir(const QString &path);

private:
  QFileSystemModel *fsModel_{nullptr};
  // File views
  QStackedWidget *fileStack_{nullptr};
  QTreeView *listView_{nullptr};
  QListView *iconView_{nullptr};
  QListView *compactView_{nullptr};
  ViewMode viewMode_{ViewMode::List};

  QStackedWidget *stack_{nullptr};
  TrashView *trashView_{nullptr};

  QString location_{"/"};
  std::vector<QString> history_;
  int historyIndex_{-1};
};
