#pragma once
#include <cstdio>

namespace bg3 {
inline bool write_and_close(FILE* f, const void* data, size_t size) {
  const bool written = fwrite(data, 1, size, f) == size;
  const bool closed = fclose(f) == 0;
  return written && closed;
}
}  // namespace bg3
