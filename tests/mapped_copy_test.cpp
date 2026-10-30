#include "../common/mapped_copy.h"
#include <windows.h>
#include <array>

int main() {
  // Exhaust every alignment and tail used by material constants; canaries
  // detect writes outside the range, and the protected page catches overreads.
  auto* p = static_cast<uint8_t*>(VirtualAlloc(nullptr, 8192, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
  if (!p) return 1;
  for (size_t i = 0; i < 4096; ++i) p[i] = uint8_t(i * 37 + 11);
  DWORD old = 0;
  if (!VirtualProtect(p + 4096, 4096, PAGE_NOACCESS, &old)) return 2;
  std::array<uint8_t, 320> got, want;
  for (size_t n = 0; n <= 256; ++n)
    for (size_t off = 0; off < 16; ++off)
      for (size_t dest = 0; dest < 16; ++dest) {
        got.fill(0xcd); want.fill(0xcd);
        const auto* src = p + 4096 - n - off;
        std::memcpy(want.data() + 16 + dest, src, n);
        bg3::copy_mapped_stream(got.data() + 16 + dest, src, n);
        if (got != want) return 3;
      }
  VirtualFree(p, 0, MEM_RELEASE);
  return 0;
}
