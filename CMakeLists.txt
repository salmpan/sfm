cmake_minimum_required(VERSION 3.16)
project(sfm LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets DBus Concurrent)

add_executable(sfm
  src/main.cpp
  src/mainwindow.h
  src/mainwindow.cpp

  src/terminal.h
  src/terminal.cpp

  src/fileops.h
  src/fileops.cpp

  src/jobmanager.h
  src/jobmanager.cpp

  src/places.h
  src/places.cpp

  src/udisks.h
  src/udisks.cpp

  src/trashview.h
  src/trashview.cpp

  src/trashmodel.h
  src/trashmodel.cpp

  src/filesortproxy.h
  src/filesortproxy.cpp

  src/browsertab.h
  src/browsertab.cpp

  src/propertiesdialog.h
  src/propertiesdialog.cpp

  src/desktopentry.h
  src/desktopentry.cpp

  src/openwithdialog.h
  src/openwithdialog.cpp

  src/breadcrumbbar.h
  src/breadcrumbbar.cpp
)

target_link_libraries(sfm PRIVATE Qt6::Widgets Qt6::DBus Qt6::Concurrent)

