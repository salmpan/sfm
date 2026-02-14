#include <QApplication>

#include "mainwindow.h"

int main(int argc, char **argv) {
  QApplication app(argc, argv);
  QApplication::setApplicationName("sfm");
  QApplication::setOrganizationName("local");

  MainWindow w;
  w.show();

  return app.exec();
}
