#include <QApplication>

#include "mainwindow.h"

int main(int argc, char **argv) {
  QApplication app(argc, argv);
  QApplication::setApplicationName("sfm");
  QApplication::setOrganizationName("local");
  QApplication::setApplicationVersion("0.1");

  MainWindow w;
  w.show();

  return app.exec();
}
