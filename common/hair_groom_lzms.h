#pragma once
// LZMS decompression for .hgrm v3 (common/hair_groom_v3.h) through the Windows Compression API (cabinet.dll, part
// of Windows 8 and later): nothing vendored. tools/hair_groom/storage.py lzms_compress writes the same buffer format.
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <compressapi.h>

#include <cstddef>
#include <cstdint>

#pragma comment(lib, "Cabinet.lib")

namespace bg3::hair_groom {

inline bool lzms_inflate(const uint8_t* src, size_t stored, uint8_t* dst, size_t raw) {
  DECOMPRESSOR_HANDLE h = nullptr;
  if (!CreateDecompressor(COMPRESS_ALGORITHM_LZMS, nullptr, &h)) return false;
  SIZE_T got = 0;
  const bool ok = Decompress(h, src, stored, dst, raw, &got) && got == raw;
  CloseDecompressor(h);
  return ok;
}

}  // namespace bg3::hair_groom
