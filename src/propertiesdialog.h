#pragma once

#include <QDialog>
#include <QString>

class QLabel;
class QLineEdit;
class QCheckBox;
class QDialogButtonBox;

class PropertiesDialog final : public QDialog {
  Q_OBJECT
public:
  explicit PropertiesDialog(const QString &path, QWidget *parent = nullptr);

private slots:
  void reload();
  void applyChanges();

private:
  void buildUi();
  void loadStat();
  void loadBasicTab();
  void loadPermTab();

  static QString humanBytes(qint64 b);
  static QString fmtTime(qint64 secs);

  QString path_;

  QLabel *lblPath_{nullptr};
  QLabel *lblType_{nullptr};
  QLabel *lblSize_{nullptr};
  QLabel *lblMtime_{nullptr};
  QLabel *lblAtime_{nullptr};
  QLabel *lblCtime_{nullptr};

  QLineEdit *editOwner_{nullptr};
  QLineEdit *editGroup_{nullptr};

  QCheckBox *uR_{nullptr}; QCheckBox *uW_{nullptr}; QCheckBox *uX_{nullptr};
  QCheckBox *gR_{nullptr}; QCheckBox *gW_{nullptr}; QCheckBox *gX_{nullptr};
  QCheckBox *oR_{nullptr}; QCheckBox *oW_{nullptr}; QCheckBox *oX_{nullptr};

  QCheckBox *setuid_{nullptr};
  QCheckBox *setgid_{nullptr};
  QCheckBox *sticky_{nullptr};

  QLabel *lblCurrentMode_{nullptr};
  QLabel *lblOwnerGroup_{nullptr};

  QDialogButtonBox *buttons_{nullptr};

  bool isDir_{false};
  qint64 size_{0};
  qint64 atime_{0}, mtime_{0}, ctime_{0};
  unsigned int mode_{0};
  unsigned int uid_{0};
  unsigned int gid_{0};
};
