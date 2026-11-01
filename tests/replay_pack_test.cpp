// common/replay_pack.h writer side (layer/replay.cpp, tools/frame_replay --synthesize): the
// manifest writer round-trips through the parser, the InstInfo spans and their merge, the budget.
// The GPU end of the same writer is frame_replay_test (the synthetic pack is written with it).
#include "../common/replay_pack.h"

#include <cstdio>

namespace rp = bg3::replay;
static int g_fail = 0;
static void check(bool ok, const char* what) {
  std::printf("%-64s %s\n", what, ok ? "PASS" : "FAIL");
  if (!ok) ++g_fail;
}

int main() {
  // 1. serialize -> parse keeps every directive.
  rp::Manifest m;
  m.version = rp::kVersion; m.pass = "gi_hit_direct"; m.defines = ""; m.frame = 123456789012ull;
  m.notes = {" live: frame_index 7, 2 instances", " s0 b20 (rtStats) not captured"};
  m.groups[0] = 160; m.groups[1] = 90; m.groups[2] = 1;
  m.push = "push.bin";
  m.buffers = {{0, 0, 400, "s0b0.bin"}, {1, 6, 1476395008ull, ""}, {1, 8, 256, "s1b8.bin"}};
  m.ranges = {{1, 6, 64, "s1b6.ranges.bin", 0, 192}, {1, 6, 8192, "s1b6.ranges.bin", 192, 384}, {1, 1, 0, "legacy.bin"}};
  m.images = {{0, 5, 0, rp::kR32Sfloat, 2560, 1440, 1, 1, "img0.bin"}, {1, 3, 2, 146, 4096, 4096, 4, 13, "img1.bin"}};
  m.omitted = {{1, 2, 17}, {1, 14, 3}};
  m.samplers = {{1, 15, 0, 1, 1, 1, 0, 0, 2, 16.0f, 1000.0f}, {1, 4, 0, 1, 1, 1, 0, 0, 0, 0.0f, 16.0f}};
  m.instances = "instances.bin"; m.instance_count = 61234;
  m.outputs = {{0, 8, rp::kRgba16Sfloat, 1280, 720, "s0b8.live"}, {0, 13, rp::kR32Sfloat, 1280, 720, ""}};
  const std::string text = rp::serialize(m);
  rp::Manifest b;
  std::string error;
  const bool parsed = rp::parse(text, b, error);
  check(parsed, ("manifest parses (" + error + ")").c_str());
  bool same = parsed && b.version == m.version && b.pass == m.pass && b.defines.empty() && b.frame == m.frame && b.push == m.push &&
              b.groups[0] == 160 && b.groups[1] == 90 && b.groups[2] == 1 && b.instances == m.instances &&
              b.instance_count == m.instance_count && b.notes == m.notes;
  check(same, "header, dispatch, push, instances, notes round-trip");
  bool lists = parsed && b.buffers.size() == 3 && b.ranges.size() == 3 && b.images.size() == 2 && b.omitted.size() == 2 &&
               b.samplers.size() == 2 && b.outputs.size() == 2;
  for (size_t i = 0; lists && i < 3; ++i)
    lists = b.buffers[i].set == m.buffers[i].set && b.buffers[i].binding == m.buffers[i].binding && b.buffers[i].bytes == m.buffers[i].bytes &&
            b.buffers[i].file == m.buffers[i].file && b.ranges[i].offset == m.ranges[i].offset && b.ranges[i].file == m.ranges[i].file &&
            b.ranges[i].file_offset == m.ranges[i].file_offset && b.ranges[i].bytes == m.ranges[i].bytes;
  for (size_t i = 0; lists && i < 2; ++i)
    lists = b.images[i].format == m.images[i].format && b.images[i].w == m.images[i].w && b.images[i].layers == m.images[i].layers &&
            b.images[i].mips == m.images[i].mips && b.images[i].file == m.images[i].file && b.omitted[i].element == m.omitted[i].element &&
            b.samplers[i].aniso == m.samplers[i].aniso && b.samplers[i].max_lod == m.samplers[i].max_lod && b.samplers[i].aw == m.samplers[i].aw &&
            b.outputs[i].format == m.outputs[i].format && b.outputs[i].reference == m.outputs[i].reference;
  check(lists, "buffers, sparse ranges (file slices), images, omitted, samplers, outputs");
  check(rp::serialize(b) == text, "serialize(parse(text)) == text");
  {
    rp::Manifest bad;
    check(!rp::parse("bg3rtx_replay_pack 1\npass gi_hit_direct\npush p\ndispatch 1 1 1\nrange 1 6 0 f 0\n", bad, error), "a range slice without BYTES is rejected");
  }

  // 2. InstInfo spans: soup vs pool, legacy / hair / empty entries name nothing.
  const uint32_t info[] = {
      0, 64, 0, 2,                                   // soup, 2 tris of stride 64: [0, 384)
      384, 64, 0, 1,                                 // soup, adjacent: [384, 576)
      100000, 32 | rp::kInfoPool, 0, 4,              // pool: [100000, 100384)
      0, 64 | rp::kInfoLegacy, 0, 9,                 // legacy soup: nothing
      0, 64 | rp::kInfoHair, 0, 9,                   // hair AABBs: nothing
      9000, 64, 0, 0,                                // no triangles
      5000, 64 | rp::kInfoAlpha, 0, 1,               // alpha-tested soup: [5000, 5192)
      100000, 32 | rp::kInfoPool, 0, 4};             // the same pool mesh again (another instance)
  std::vector<rp::Span> soup, pool;
  rp::instinfo_spans(info, 8, soup, pool);
  check(soup.size() == 3 && pool.size() == 2 && soup[0].bytes == 384 && soup[2].offset == 5000 && pool[0].bytes == 384, "instinfo_spans: soup / pool / skipped entries");
  rp::merge_spans(soup, 0);
  rp::merge_spans(pool, 0);
  check(soup.size() == 2 && soup[0].offset == 0 && soup[0].bytes == 576 && soup[1].offset == 5000 && soup[1].bytes == 192,
        "merge_spans: adjacent spans join, a far one stays");
  check(pool.size() == 1 && pool[0].bytes == 384, "merge_spans: one mesh named twice is copied once");
  std::vector<rp::Span> gap = {{4672, 192}, {0, 576}, {9000, 8}};
  rp::merge_spans(gap, 4096);
  check(gap.size() == 2 && gap[0].offset == 0 && gap[0].bytes == 4864 && gap[1].offset == 9000 && rp::span_bytes(gap) == 4872, "merge_spans: a gap of 4096 bytes is copied with them, 4136 is not");

  // 3. Budget: the largest droppable images go first; fixed bytes never.
  bool over = false;
  auto dropped = rp::drop_over_budget(600, {100, 300, 50, 200}, 1000, over);
  check(!over && dropped.size() == 1 && dropped[0] == 1, "drop_over_budget: 1250 > 1000 drops the 300 only");
  dropped = rp::drop_over_budget(600, {100, 300, 50, 200}, 700, over);
  check(!over && dropped.size() == 3 && dropped[0] == 0 && dropped[1] == 1 && dropped[2] == 3, "drop_over_budget: to 700 keeps the 50");
  dropped = rp::drop_over_budget(1200, {100}, 1000, over);
  check(over && dropped.size() == 1, "drop_over_budget: fixed over the budget is reported");
  dropped = rp::drop_over_budget(10, {100, 200}, 1000, over);
  check(!over && dropped.empty(), "drop_over_budget: under the budget drops nothing");

  std::printf("%s: %d failure(s)\n", g_fail ? "FAIL" : "PASS", g_fail);
  return g_fail ? 1 : 0;
}
