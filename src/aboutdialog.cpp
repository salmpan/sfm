#include "aboutdialog.h"

#include <QApplication>
#include <QCoreApplication>
#include <QDialogButtonBox>
#include <QFile>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPlainTextEdit>
#include <QTabWidget>
#include <QTextStream>
#include <QVBoxLayout>

AboutDialog::AboutDialog(QWidget *parent)
  : QDialog(parent)
{
  setWindowTitle(tr("About %1").arg(QApplication::applicationName().isEmpty()
                                     ? QStringLiteral("sfm")
                                     : QApplication::applicationName()));
  setModal(true);
  resize(640, 480);

  auto *tabs = new QTabWidget(this);

  // About tab
  {
    auto *w = new QWidget(this);
    auto *layout = new QVBoxLayout(w);

    const QString appName =
        QApplication::applicationName().isEmpty()
          ? QStringLiteral("sfm")
          : QApplication::applicationName();

    const QString appVer =
        QApplication::applicationVersion().isEmpty()
          ? QStringLiteral("dev")
          : QApplication::applicationVersion();

    auto *title = new QLabel(
        "<div style='font-size:18px; font-weight:600;'>" + appName + "</div>"
        "<div style='margin-top:4px;'>Version: " + appVer + "</div>"
        "<div style='margin-top:10px;'>A lightweight Qt file manager.</div>",
        w);
    title->setTextFormat(Qt::RichText);
    title->setWordWrap(true);

    layout->addWidget(title);
    layout->addStretch(1);

    tabs->addTab(w, tr("About"));
  }

  // Build tab
  {
    auto *w = new QWidget(this);
    auto *form = new QFormLayout(w);

    const QString qtVer = QString::fromLatin1(QT_VERSION_STR);

    auto *lQt = new QLabel(qtVer, w);
    lQt->setTextInteractionFlags(Qt::TextSelectableByMouse);

    auto *lCommit = new QLabel(gitCommit(), w);
    lCommit->setTextInteractionFlags(Qt::TextSelectableByMouse);

    auto *lBuild = new QLabel(buildDateTime(), w);
    lBuild->setTextInteractionFlags(Qt::TextSelectableByMouse);

    form->addRow(tr("Qt:"), lQt);
    form->addRow(tr("Git commit:"), lCommit);
    form->addRow(tr("Build date:"), lBuild);

    tabs->addTab(w, tr("Build"));
  }

  // License tab
  {
    auto *w = new QWidget(this);
    auto *layout = new QVBoxLayout(w);

    auto *licenseEdit = new QPlainTextEdit(w);
    licenseEdit->setReadOnly(true);
    licenseEdit->setPlainText(loadLicenseText());
    licenseEdit->setLineWrapMode(QPlainTextEdit::NoWrap);

    layout->addWidget(licenseEdit);

    tabs->addTab(w, tr("License"));
  }

  auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok, this);
  connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);

  auto *root = new QVBoxLayout(this);
  root->addWidget(tabs);
  root->addWidget(buttons);
}

QString AboutDialog::gitCommit() const
{
#ifdef SFM_GIT_COMMIT
  return QStringLiteral(SFM_GIT_COMMIT);
#else
  return QStringLiteral("unknown");
#endif
}

QString AboutDialog::buildDateTime() const
{
  // compile-time stamp
  return QStringLiteral(__DATE__) + QStringLiteral(" ") + QStringLiteral(__TIME__);
}

QString AboutDialog::loadLicenseText() const
{
  // Try common locations:
  // 1) alongside build/run working dir: ./LICENSE
  // 2) appdir/../LICENSE (common when running from build/bin)
  QStringList candidates;
  candidates << QStringLiteral("LICENSE");
  candidates << (QCoreApplication::applicationDirPath() + QStringLiteral("/../LICENSE"));
  candidates << (QCoreApplication::applicationDirPath() + QStringLiteral("/LICENSE"));

  for (const QString &path : candidates) {
    QFile f(path);
    if (!f.exists()) continue;
    if (f.open(QIODevice::ReadOnly | QIODevice::Text)) {
      QTextStream ts(&f);
      return ts.readAll();
    }
  }

  return QStringLiteral("LICENSE file not found.");
}