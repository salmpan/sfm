#pragma once

#include <QString>

class Terminal final {
public:
  // Open a terminal emulator with working directory = dirPath.
  // Linux only; best-effort chain of common terminals.
  static bool openInTerminal(const QString &dirPath, QString *errorOut = nullptr);
};
