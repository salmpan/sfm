#pragma once

#include <QMainWindow>
#include <QString>
#include <QtGlobal>
#include <QStringList>

class QFileSystemModel;
class QProgressDialog;
class QTabWidget;
class QLineEdit;
class QAction;

class PlacesSidebar;
class JobManager;
class BrowserTab;

class MainWindow final : public QMainWindow {
  Q_OBJECT
public:
  explicit MainWindow(QWidget *parent = nullptr);

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

  void trashSelected();
  void deleteSelectedPermanently();
  void deleteSelectedInTrashView();
  void restoreSelectedInTrashView();

  void refresh();
  void openCurrentDirInTerminal();
  void emptyTrashFromSidebar();

private:
  QFileSystemModel *fsModel_{nullptr};
  JobManager *jobs_{nullptr};

  QProgressDialog *progress_{nullptr};
  qint64 bytesTotal_{-1};
  qint64 bytesDone_{0};

  PlacesSidebar *places_{nullptr};
  QTabWidget *tabs_{nullptr};
  QLineEdit *address_{nullptr};

  QAction *actBack_{nullptr};
  QAction *actForward_{nullptr};
  QAction *actUp_{nullptr};
  QAction *actRefresh_{nullptr};

  Clip clipboard_;
};
