#pragma once

#include <QDialog>

class QTabWidget;
class QTextEdit;

class AboutDialog final : public QDialog
{
  Q_OBJECT
public:
  explicit AboutDialog(QWidget *parent = nullptr);

private:
  QString loadLicenseText() const;
  QString gitCommit() const;
  QString buildDateTime() const;
};
