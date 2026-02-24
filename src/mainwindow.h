#pragma once

#include <QMainWindow>
#include <QString>
#include <QtGlobal>
#include <QStringList>

class QLabel;

class QCloseEvent;
class QFileSystemModel;
class QProgressDialog;
class QTabWidget;
class QLineEdit;
class QDockWidget;
class QStackedWidget;
class QSplitter;
class BreadcrumbBar;
class QAction;

class PlacesSidebar;
class JobManager;
class BrowserTab;

class QMenu;
class QAction;
class QActionGroup;
struct SearchOptions;
class SearchResultsTab;


class MainWindow final : public QMainWindow {
  Q_OBJECT
public:
  explicit MainWindow(QWidget *parent = nullptr);
  explicit MainWindow(const QString &startLoc, QWidget *parent = nullptr);

protected:
  void closeEvent(QCloseEvent *e) override;

private:
  enum class ClipMode { None, Copy, Cut };
  struct Clip {
    ClipMode mode{ClipMode::None};
    QStringList paths;

    bool hasData() const {
      return mode != ClipMode::None && !paths.isEmpty();
    }

    void clear() {
      mode = ClipMode::None;
      paths.clear();
    }
  };

  static QString humanBytes(qint64 b);

  void initInlineStatusBar();
  void updateInlineStatusBar();
  void updateStatusItemCount();
  void updateStatusSelectedSize();
  void updateStatusFreeSpace();

  BrowserTab* currentTab() const;

  void newTab(const QString &startLoc);
  void closeTab(int index);
  void syncUiFromTab();
  void onAddressEntered();

  void showPropertiesForPath(const QString &path);
  void showPropertiesForSelection();

  void copySelected();
  void cutSelected();
  void pasteIntoCurrentDir();
  void renameSelected();
  void createNewFolder();
  void createEmptyDocument();

  void trashSelected();
  void deleteSelectedPermanently();
  void deleteSelectedInTrashView();
  void restoreSelectedInTrashView();

  // Settings - session
  void loadSettings();
  void saveSettings() const;
  void addRecentLocation(const QString &loc);
  void rebuildRecentLocationsMenu();
  static QString prettifyLocation(const QString &loc);

  void refresh();
  void openCurrentDirInTerminal();
  void emptyTrashFromSidebar();
  void openNewWindow();
  void showAboutDialog();
  void openFindDialog();
  void startSearch(const SearchOptions &opt);

private:
  void createActions();
  void createMenus();

  QFileSystemModel *fsModel_{nullptr};
  JobManager *jobs_{nullptr};

  QProgressDialog *progress_{nullptr};
  qint64 bytesTotal_{-1};
  qint64 bytesDone_{0};

  PlacesSidebar *places_{nullptr};
  QTabWidget *tabs_{nullptr};
  QStackedWidget *pathStack_{nullptr};
  QSplitter *mainSplit_{nullptr};
  BreadcrumbBar *breadcrumbs_{nullptr};
  QLineEdit *address_{nullptr};

  QAction *actBack_{nullptr};
  QAction *actForward_{nullptr};
  QAction *actUp_{nullptr};
  QAction *actRefresh_{nullptr};

  Clip clipboard_;

  // Menus
  QMenu* fileMenu_{nullptr};
  QMenu* recentMenu_{nullptr};
  QStringList recentLocations_;
  QMenu* editMenu_{nullptr};
  QMenu* viewMenu_{nullptr};
  QMenu* goMenu_{nullptr};
  QMenu* toolsMenu_{nullptr};
  QMenu* helpMenu_{nullptr};

  // File
  QAction* newTabAct_{nullptr};
  QAction* newWindowAct_{nullptr};
  QAction* closeTabAct_{nullptr};
  QAction* newFolderAct_{nullptr};
  QAction* newDocAct_{nullptr};
  QAction* quitAct_{nullptr};

  // Edit
  QAction* copyAct_{nullptr};
  QAction* cutAct_{nullptr};
  QAction* pasteAct_{nullptr};
  QAction* renameAct_{nullptr};
  QAction* deleteAct_{nullptr};
  QAction* trashAct_{nullptr};
  QAction* propertiesAct_{nullptr};
  QAction* openWithAct_{nullptr};

  // View
  QActionGroup* viewModeGroup_{nullptr};
  QAction* viewGridAct_{nullptr};
  QAction* viewListAct_{nullptr};
  QAction* viewCompactAct_{nullptr};
  QAction* toggleHiddenAct_{nullptr};

  QActionGroup* sortKeyGroup_{nullptr};
  QAction* sortByNameAct_{nullptr};
  QAction* sortBySizeAct_{nullptr};
  QAction* sortByTypeAct_{nullptr};
  QAction* sortByModifiedAct_{nullptr};

  QActionGroup* sortOrderGroup_{nullptr};
  QAction* sortAscAct_{nullptr};
  QAction* sortDescAct_{nullptr};

  QAction* foldersFirstAct_{nullptr};

  // Go
  QAction* backAct_{nullptr};
  QAction* forwardAct_{nullptr};
  QAction* upAct_{nullptr};
  QAction* refreshAct_{nullptr};
  QAction* homeAct_{nullptr};
  QAction* trashLocationAct_{nullptr};

  // Tools
  QAction* openTerminalAct_{nullptr};
  QAction* findAct_{nullptr};
  QAction* toggleSearchDockAct_{nullptr};

  QDockWidget* searchDock_{nullptr};
  SearchResultsTab* searchTab_{nullptr};
  QAction* emptyTrashAct_{nullptr};
  
  // Help
  QAction* aboutAct_{nullptr};

  // Inline status bar widgets
  QLabel* statusItemCount_{nullptr};
  QLabel* statusSelectedSize_{nullptr};
  QLabel* statusFreeSpace_{nullptr};
};
