#pragma once
#include <cstddef>
#include <cstdint>

namespace bg3 {
// Conservative proof for the game's invisible helper FS: a single fragment
// entry point whose entire body is Label, Kill. Conditional alpha tests and
// early depth/stencil tests are deliberately not classified here.
inline bool spirv_fragment_always_discards(const uint32_t* code, size_t words) {
  if (words < 5 || code[0] != 0x07230203) return false;
  uint32_t entry = 0, entries = 0;
  for (size_t p = 5; p < words;) {
    const uint32_t n = code[p] >> 16, op = code[p] & 0xffff;
    if (!n || p + n > words) return false;
    if (op == 15) { // OpEntryPoint
      if (n < 4 || code[p + 1] != 4) return false;
      entry = code[p + 2];
      ++entries;
    }
    // EarlyFragmentTests can write depth/stencil before the kill.
    if (op == 16 && n >= 3 && code[p + 2] == 9) return false;
    p += n;
  }
  if (entries != 1 || !entry || entry >= code[3]) return false;
  bool body = false, label = false, kill = false, proven = false;
  for (size_t p = 5; p < words; p += code[p] >> 16) {
    const uint32_t n = code[p] >> 16, op = code[p] & 0xffff;
    if (op == 54 && n == 5 && code[p + 2] == entry) { // OpFunction
      if (body || proven) return false;
      body = true;
      continue;
    }
    if (!body) continue;
    if (op == 8 || op == 317) continue; // debug Line / NoLine
    if (op == 248 && n == 2 && !label && !kill) label = true;
    else if (op == 252 && n == 1 && label && !kill) kill = true;
    else if (op == 56 && n == 1 && kill) body = false, proven = true;
    else return false;
  }
  return proven && !body;
}
} // namespace bg3
