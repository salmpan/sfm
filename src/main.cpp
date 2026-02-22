#include <QApplication>
#include <QCommandLineParser>

#include "mainwindow.h"

int main(int argc, char **argv) {
  QApplication app(argc, argv);
  QApplication::setApplicationName("sfm");
  QApplication::setOrganizationName("local");
  QApplication::setApplicationVersion("0.1");

  QCommandLineParser parser;
  parser.setApplicationDescription("sfm file manager");
  parser.addHelpOption();
  parser.addPositionalArgument("location", "Start location (path or trash:///).");
  parser.process(app);

  QString startLoc;
  const QStringList pos = parser.positionalArguments();
  if (!pos.isEmpty())
    startLoc = pos.first();

  MainWindow w(startLoc);
  w.show();

  return app.exec();
}
