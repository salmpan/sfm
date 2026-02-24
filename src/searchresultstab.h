#pragma once

#include <QWidget>
#include <QFutureWatcher>
#include <QAtomicInt>
#include <QProcess>

class QCheckBox;
class QComboBox;
class QTimer;
class QToolButton;

#include "searchtypes.h"

class QLabel;
class QPushButton;
class QLineEdit;
class QTableView;
class QSortFilterProxyModel;

class SearchResultsModel;

class SearchResultsTab final : public QWidget {
  Q_OBJECT
public:
  explicit SearchResultsTab(const SearchOptions &opt, QWidget *parent = nullptr);
  ~SearchResultsTab() override;

  QString title() const;

  // Dock integration
  void setRootPath(const QString &rootPath);
  void setCurrentFolderSuggestion(const QString &folderPath);
  void focusQuery();

signals:
  void openPathRequested(const QString &path);
  void openContainingFolderRequested(const QString &path);
  void requestCloseMe();

private slots:
  void cancel();
  void runNow_();
  void scheduleRun_();
  void onChooseRoot_();
  void onActivated_(const QModelIndex &idx);
  void onContextMenu_(const QPoint &pos);

  // ripgrep
  void onRgReadyRead_();
  void onRgFinished_(int exitCode, QProcess::ExitStatus status);

private:
  void rebuildOptionsFromUi_();
  void loadPersistedOptions_();
  void persistOptions_() const;
  void start_();
  void startNameSearch_();
  void startContentSearch_();
  void finish_(const QString &status);

  void addResult_(const QString &path, bool isDir, int line, int col, const QString &preview);
  void updateSummary_();

private:
  SearchOptions opt_;

  QString currentFolderPath_;

  // Search inputs (embedded)
  QLabel *rootLabel_{nullptr};
  QToolButton *chooseRootBtn_{nullptr};
  QPushButton *useCurrentFolderBtn_{nullptr};
  QLabel *currentFolderLabel_{nullptr};
  QLineEdit *queryEdit_{nullptr};
  QComboBox *modeCombo_{nullptr};
  QCheckBox *caseChk_{nullptr};
  QCheckBox *regexChk_{nullptr};
  QCheckBox *hiddenChk_{nullptr};
  QCheckBox *dirsChk_{nullptr};
  QCheckBox *followChk_{nullptr};
  QCheckBox *binaryChk_{nullptr};
  QToolButton *runBtn_{nullptr};
  QToolButton *closeBtn_{nullptr};
  QTimer *debounce_{nullptr};

  QLabel *summary_{nullptr};
  QPushButton *cancelBtn_{nullptr};

  QLineEdit *filterEdit_{nullptr};
  QTableView *view_{nullptr};
  SearchResultsModel *model_{nullptr};
  QSortFilterProxyModel *proxy_{nullptr};

  // Name search
  QFutureWatcher<void> nameWatcher_;
  QAtomicInt cancelFlag_{0};

  // Content search
  QProcess *rg_{nullptr};
  QByteArray rgBuf_;
};
