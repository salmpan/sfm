#pragma once

#include <QString>

struct SearchOptions {
  enum class Mode {
    Name,
    Content
  };

  QString rootPath;        // absolute directory
  QString query;           // user query
  Mode mode{Mode::Name};

  bool useRegex{false};
  bool caseSensitive{false};

  bool includeHidden{false};
  bool includeDirectories{true};   // applies to name mode
  bool followSymlinks{false};      // applies to name mode

  // Content-mode options (ripgrep)
  bool searchBinary{false};        // if false, let rg skip binary
};
