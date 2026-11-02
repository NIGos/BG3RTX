// common/vt_alpha_masks.h: index lines, .vtalpha layout validation, record matching (rt_vt_alpha_masks).
#include "../common/vt_alpha_masks.h"
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <cstring>
#include <vector>

using namespace bg3::vt_alpha;
static unsigned checks = 0;
static void check(bool ok, const char* why) {
  ++checks;
  if (!ok) { std::fprintf(stderr, "FAIL: %s\n", why); std::exit(1); }
}

static std::vector<uint8_t> file(uint32_t w, uint32_t h, uint32_t n, int extra = 0) {
  std::vector<uint8_t> d(20);
  std::memcpy(d.data(), "BG3VTA1\0", 8);
  std::memcpy(d.data() + 8, &w, 4); std::memcpy(d.data() + 12, &h, 4); std::memcpy(d.data() + 16, &n, 4);
  for (uint32_t l = 0; l < n; ++l) {
    const uint32_t lw = (w >> l) ? (w >> l) : 1, lh = (h >> l) ? (h >> l) : 1;
    d.resize(d.size() + size_t((lw + 3) / 4) * ((lh + 3) / 4) * 8, uint8_t(l));
  }
  d.resize(size_t(int(d.size()) + extra));
  return d;
}

int main() {
  IndexEntry e;
  // The real camp line (data/assetdb/vt_alpha/vt_alpha_index.txt, 2026-11-02).
  check(parse_index_line("10 3e840000 3db00000 3c000000 3c000000 a_a821cfc6b5605b21f6fbf8fe93b8ae64.vtalpha", &e), "index line parses");
  check(e.tileset == 10 && e.p0[0] == 0x3e840000u && e.p0[3] == 0x3c000000u && e.file == "a_a821cfc6b5605b21f6fbf8fe93b8ae64.vtalpha", "index fields");
  check(!parse_index_line("# comment", &e) && !parse_index_line("", &e), "comments and blank lines skipped");
  check(!parse_index_line("10 3e840000 3db00000 3c000000", &e), "short line rejected");
  check(!parse_index_line("10 1 2 3 4 ..\\x.vtalpha", &e) && !parse_index_line("10 1 2 3 4 c:x.vtalpha", &e), "paths rejected");

  Mask m;
  auto d = file(512, 512, 3);
  check(parse_mask(d.data(), d.size(), &m) && m.levels.size() == 3, "512x512 three levels");
  check(m.levels[0].bytes == 128u * 128 * 8 && m.levels[1].offset == 20 + 128u * 128 * 8 && m.levels[2].width == 128, "level table");
  auto np = file(1644, 1756, 2);
  check(parse_mask(np.data(), np.size(), &m) && m.levels[0].bytes == uint64_t(411) * 439 * 8 && m.levels[1].width == 822, "non-power-of-two rounds blocks up");
  auto full = file(4, 2, 3);
  check(parse_mask(full.data(), full.size(), &m) && m.levels[2].width == 1 && m.levels[2].height == 1, "full chain to 1x1");
  auto toolong = file(2, 2, 3);
  check(!parse_mask(toolong.data(), toolong.size(), &m), "levels past 1x1 rejected");
  auto shortf = file(512, 512, 3, -1), longf = file(512, 512, 3, 8);
  check(!parse_mask(shortf.data(), shortf.size(), &m) && !parse_mask(longf.data(), longf.size(), &m), "size must match exactly");
  auto bad = d; bad[0] = 'X';
  check(!parse_mask(bad.data(), bad.size(), &m) && !parse_mask(nullptr, 0, &m), "magic and null rejected");
  auto zero = file(0, 512, 1);
  check(!parse_mask(zero.data(), zero.size(), &m), "zero width rejected");

  parse_index_line("10 3e840000 3db00000 3c000000 3c000000 a.vtalpha", &e);
  float p0[4];
  const uint32_t bits[4] = {0x3e840000u, 0x3db00000u, 0x3c000000u, 0x3c000000u};
  std::memcpy(p0, bits, 16);
  check(matches(e, 1, p0, 10.0f), "same tile set and uv transform bits match");
  check(!matches(e, 2, p0, 10.0f), "only VT records (kind 1)");
  check(!matches(e, 1, p0, 9.0f) && !matches(e, 1, p0, 10.5f) && !matches(e, 1, p0, -1.0f), "tile set must be the same integer");
  p0[2] = std::nextafter(p0[2], 1.0f);
  check(!matches(e, 1, p0, 10.0f), "one ulp of uv transform does not match");
  std::printf("vt_alpha_masks_test: %u checks passed\n", checks);
  return 0;
}
