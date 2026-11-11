#pragma once
// The build's base version: the executable's version resource (launcher.rc) and the label of a developer build.
// scripts/package.ps1 refuses a numbered package version that does not start with it.
#define BG3RTX_PRODUCT_VERSION "0.9.6.0"
#define BG3RTX_PRODUCT_VERSION_NUM 0,9,6,0
#ifndef RC_INVOKED
#include <string>
namespace launcher {
// The version the launcher shows: the package's own, line 1 of VERSION.txt ("BG3RTX <version>", written by
// scripts/package.ps1), so the window always names the installed package (0.9.6-hairtest8 showed 0.9.5.0).
// Without a package file (developer tree) it is the base version marked as a developer build.
inline std::string display_version(const std::string& version_txt) {
  std::string line = version_txt.substr(0, version_txt.find_first_of("\r\n"));
  if (line.rfind("\xEF\xBB\xBF", 0) == 0) line.erase(0, 3);   // Windows PowerShell writes UTF-8 with a BOM
  const std::string prefix = "BG3RTX ";
  if (line.rfind(prefix, 0) == 0 && line.size() > prefix.size() &&
      line.find_first_not_of("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789._-", prefix.size()) == std::string::npos)
    return line.substr(prefix.size());
  return BG3RTX_PRODUCT_VERSION " (developer build)";
}
}
#endif
