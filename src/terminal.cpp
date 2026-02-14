#include "terminal.h"

#include <QProcess>
#include <QFileInfo>
#include <QDir>

static bool tryStartDetached(const QString &prog, const QStringList &args) {
  return QProcess::startDetached(prog, args);
}

bool Terminal::openInTerminal(const QString &dirPath, QString *errorOut) {
  const QString dir = QDir(dirPath).absolutePath();
  if (!QFileInfo(dir).exists() || !QFileInfo(dir).isDir()) {
    if (errorOut) *errorOut = "Not a directory: " + dir;
    return false;
  }

  // Prefer user-configured terminal if present
  // xdg-terminal-exec is not universal; try common terminals.
  struct Candidate { QString prog; QStringList args; };
  const QList<Candidate> candidates = {
      {"kgx", {"--working-directory", dir}},
      {"gnome-terminal", {"--working-directory", dir}},
      {"konsole", {"--workdir", dir}},
      {"xfce4-terminal", {"--working-directory", dir}},
      {"mate-terminal", {"--working-directory", dir}},
      {"xterm", {"-e", "bash", "-lc", "cd \"" + dir + "\"; exec bash"}},
  };

  for (const auto &c : candidates) {
    if (tryStartDetached(c.prog, c.args)) return true;
  }

  // Last resort: x-terminal-emulator wrapper (Debian/Ubuntu)
  if (tryStartDetached("x-terminal-emulator", {"--working-directory", dir})) return true;

  if (errorOut) *errorOut = "No supported terminal emulator found.";
  return false;
}
