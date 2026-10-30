#include "../common/spirv_visibility.h"
#include <vector>

static std::vector<uint32_t> module() {
  return {0x07230203, 0x00010300, 0, 8, 0,
          (5u << 16) | 15, 4, 1, 0x6e69616d, 0,
          (3u << 16) | 16, 1, 7, // OriginUpperLeft
          (5u << 16) | 54, 2, 1, 0, 3,
          (2u << 16) | 248, 4,
          (1u << 16) | 252,
          (1u << 16) | 56};
}
int main() {
  auto accept = [](const std::vector<uint32_t>& s) { return bg3::spirv_fragment_always_discards(s.data(), s.size()); };
  auto s = module();
  if (!accept(s)) return 1;
  s[12] = 9; // EarlyFragmentTests: kill need not suppress depth writes
  if (accept(s)) return 2;
  s = module(); s[20] = (1u << 16) | 253; // Return, ordinary opaque FS
  if (accept(s)) return 3;
  s = module(); s[6] = 0; // vertex stage
  if (accept(s)) return 4;
  s = module(); s.insert(s.begin() + 20, {(4u << 16) | 250, 5, 6, 7}); // conditional kill is not unconditional
  if (accept(s)) return 5;
  s = module(); s.pop_back();
  if (accept(s)) return 6;
  s = module(); s[20] = 0; // malformed instruction
  if (accept(s)) return 7;
  s = module(); s.insert(s.begin() + 20, {(2u << 16) | 249, 4}); // branch into a loop
  if (accept(s)) return 8;
  s = module(); s.insert(s.begin() + 10, {(5u << 16) | 15, 4, 6, 0x6e69616d, 0}); // multiple entry points
  if (accept(s)) return 9;
  s = module(); s[20] = (1u << 16) | 253;
  // A kill in an unrelated helper function cannot prove the fragment entry discards.
  s.insert(s.end(), {(5u << 16) | 54, 2, 6, 0, 3, (2u << 16) | 248, 7,
                     (1u << 16) | 252, (1u << 16) | 56});
  if (accept(s)) return 10;
  return 0;
}
