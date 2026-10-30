#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <smmintrin.h>

namespace bg3 {
// Read write-combined game uploads in whole aligned vectors, as bvhsplit does
// for instance matrices. Never read before or beyond the requested range.
inline void copy_mapped_stream(void* dst, const void* src, size_t n) {
  auto* d = static_cast<uint8_t*>(dst);
  auto* s = static_cast<const uint8_t*>(src);
  const size_t prefix = (16 - (reinterpret_cast<uintptr_t>(s) & 15)) & 15;
  const size_t head = prefix < n ? prefix : n;
  if (head) { std::memcpy(d, s, head); d += head; s += head; n -= head; }
  while (n >= 16) {
    const __m128i v = _mm_stream_load_si128(reinterpret_cast<__m128i*>(const_cast<uint8_t*>(s)));
    _mm_storeu_si128(reinterpret_cast<__m128i*>(d), v);
    d += 16; s += 16; n -= 16;
  }
  if (n) std::memcpy(d, s, n);
}
}  // namespace bg3
