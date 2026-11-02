// bg3rtx.log size cap for long sessions (layer/util.cpp log(); docs/release/RELEASE_BETA.md).
// At start the previous session's log becomes bg3rtx.prev.log; when the current file reaches the
// cap it also moves to bg3rtx.prev.log and a new bg3rtx.log starts with a note. At most two files,
// about 2 x cap on disk. Header-only, standard C++ only (unit test: tests/log_rotate_test.cpp).
#pragma once

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

namespace bg3 {
namespace logrot {

// ~3 KB/s at the default settings: one file holds about 6 hours.
inline constexpr uint64_t kLogCapBytes = 64ull << 20;

// "x\bg3rtx.log" -> "x\bg3rtx.prev.log"; "x\bg3rtx.run1.log" -> "x\bg3rtx.run1.prev.log".
inline std::string prev_path(const std::string& path) {
  const size_t dot = path.rfind('.');
  const size_t sep = path.find_last_of("\\/");
  if (dot == std::string::npos || (sep != std::string::npos && dot < sep)) return path + ".prev";
  return path.substr(0, dot) + ".prev" + path.substr(dot);
}

// Whether writing `add` more bytes to a file holding `written` crosses `cap` (a non-empty file only:
// one line longer than the cap is still written).
inline bool due(uint64_t written, uint64_t add, uint64_t cap) { return written > 0 && written + add > cap; }

// Moves `path` to its prev_path, replacing an older one. False when `path` is absent or the move
// fails (another process holds it without delete sharing): the caller's "w" open then truncates.
inline bool shift(const std::string& path) {
  const std::string prev = prev_path(path);
  std::remove(prev.c_str());
  return std::rename(path.c_str(), prev.c_str()) == 0;
}

// The log file: opened on the first line (shifting the previous session's file), rotated at the cap.
// Not thread-safe: the caller holds its lock.
struct Writer {
  std::string path;
  uint64_t cap = kLogCapBytes;
  FILE* (*open)(const char*) = [](const char* p) { return std::fopen(p, "w"); };
  FILE* f = nullptr;
  uint64_t written = 0;
  unsigned rotations = 0;
  bool opened = false;

  // False when the file cannot be opened (the line is dropped; the caller still has its debugger output).
  bool write(const char* line) {
    const size_t n = std::strlen(line);
    if (f && due(written, n, cap)) {
      std::fclose(f);
      f = nullptr;
      ++rotations;
    }
    if (!f) {
      shift(path);
      f = open(path.c_str());
      written = 0;
      if (f && opened) {
        int k = std::fprintf(f, "[log] reached %llu MB: the earlier part of this session is in %s\n",
                             (unsigned long long)(cap >> 20), prev_path(path).c_str());
        written += k > 0 ? uint64_t(k) : 0;
      }
      opened = true;
    }
    if (!f) return false;
    std::fputs(line, f);
    std::fflush(f);
    written += n;
    return true;
  }
};

}  // namespace logrot
}  // namespace bg3
