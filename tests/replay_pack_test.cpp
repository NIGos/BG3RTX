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

  // 4. Range-only capture: a descriptor's bound bytes, never the whole buffer.
  check(rp::bound_bytes(1000, 200, 300) == 300, "bound_bytes: offset/range inside the buffer");
  check(rp::bound_bytes(1000, 200, rp::kWholeSize) == 800, "bound_bytes: whole size = to the end of the buffer");
  check(rp::bound_bytes(1000, 900, 300) == 100 && rp::bound_bytes(1000, 1000, 16) == 0, "bound_bytes: clipped to the buffer, past it 0");
  {
    // Soup b6 bound [0, 10000) captured as two slices of one file; b1 bound to the same bytes reuses
    // them (no copy); a b1 bound at [5000, 9000) gets them clipped and shifted; one outside is refused.
    const std::vector<rp::Range> soup_slices = {{1, 6, 1000, "s1b6.ranges.bin", 0, 2000}, {1, 6, 6000, "s1b6.ranges.bin", 2000, 4000}};
    std::vector<rp::Range> out;
    bool ok = rp::alias_ranges(soup_slices, 0, 10000, 1, 1, 0, 10000, out);
    check(ok && out.size() == 2 && out[0].binding == 1 && out[0].offset == 1000 && out[0].file_offset == 0 && out[0].bytes == 2000 &&
              out[1].offset == 6000 && out[1].file_offset == 2000 && out[1].bytes == 4000 && out[1].file == "s1b6.ranges.bin",
          "alias_ranges: the same bound range reuses every slice");
    out.clear();
    ok = rp::alias_ranges(soup_slices, 0, 10000, 1, 1, 5000, 4000, out);
    check(ok && out.size() == 1 && out[0].offset == 1000 && out[0].file_offset == 2000 && out[0].bytes == 3000,
          "alias_ranges: a sub-range clips and shifts the slices");
    out.clear();
    ok = rp::alias_ranges(soup_slices, 4096, 10000, 1, 1, 0, 10000, out);
    check(!ok && out.empty(), "alias_ranges: outside the captured range is captured on its own");
    // The aliased manifest round-trips and names no new bytes (the slices stay in the soup's file).
    rp::Manifest a;
    a.version = rp::kVersion; a.pass = "gi_hit_direct"; a.push = "push.bin"; a.groups[0] = a.groups[1] = a.groups[2] = 1;
    a.buffers = {{1, 6, 10000, ""}, {1, 1, 10000, ""}};
    a.ranges = soup_slices;
    rp::alias_ranges(soup_slices, 0, 10000, 1, 1, 0, 10000, a.ranges);
    rp::Manifest ab;
    check(rp::parse(rp::serialize(a), ab, error) && ab.ranges.size() == 4 && ab.ranges[3].binding == 1 && ab.ranges[3].file_offset == 2000,
          "alias_ranges: aliased slices round-trip through the manifest");
  }

  // 5. Shader read of images without TRANSFER_SRC (layer/replay.cpp, shaders/replay_image.hlsl).
  {
    rp::ShaderRead r;
    check(rp::shader_read_format(146, false, r) && r.pack_format == 43 && r.encode == rp::kEncSrgb8 && r.channels == 4,
          "shader_read_format: BC7 SRGB -> RGBA8 SRGB");
    check(rp::shader_read_format(131, false, r) && r.pack_format == 37 && r.encode == rp::kEncUnorm8 && r.channels == 4 &&
              rp::shader_read_format(137, false, r) && r.pack_format == 37,
          "shader_read_format: BC1 / BC3 UNORM -> RGBA8 UNORM");
    check(rp::shader_read_format(139, false, r) && r.pack_format == 9 && r.channels == 1 && rp::shader_read_format(142, false, r) &&
              r.pack_format == 17 && r.encode == rp::kEncSnorm8 && r.channels == 2,
          "shader_read_format: BC4 UNORM -> R8, BC5 SNORM -> RG8 SNORM");
    check(rp::shader_read_format(143, false, r) && r.pack_format == rp::kRgba16Sfloat && r.encode == rp::kEncHalf && r.channels == 4 &&
              rp::shader_read_format(144, false, r) && r.pack_format == rp::kRgba16Sfloat,
          "shader_read_format: BC6H -> RGBA16F");
    check(rp::shader_read_format(44, false, r) && r.pack_format == 37 && rp::shader_read_format(50, false, r) && r.pack_format == 43 &&
              rp::shader_read_format(122, false, r) && r.pack_format == rp::kRgba16Sfloat && rp::shader_read_format(64, false, r) && r.pack_format == 91,
          "shader_read_format: BGRA8 -> RGBA8, B10G11R11 -> RGBA16F, A2B10G10R10 -> RGBA16");
    check(rp::shader_read_format(139, true, r) && r.pack_format == 37 && r.channels == 4 && rp::shader_read_format(83, true, r) &&
              r.pack_format == rp::kRgba16Sfloat && r.channels == 4,
          "shader_read_format: a swizzled view is stored with 4 channels");
    check(!rp::shader_read_format(rp::kRgba32Uint, false, r) && !rp::shader_read_format(126, false, r) && !rp::shader_read_format(147, false, r),
          "shader_read_format: integer, depth and ETC2 formats are not read");
    // Every mapped pack format is one the pack carries, with the texel bytes the encoding writes.
    bool sizes = true;
    for (uint32_t f = 1; f < 200; ++f)
      for (bool sw : {false, true}) {
        if (!rp::shader_read_format(f, sw, r)) continue;
        uint32_t block;
        sizes = sizes && rp::block_bytes(r.pack_format, block) == rp::shader_read_texel_bytes(r) && block == 1 &&
                rp::shader_read_per_thread(r) * rp::shader_read_texel_bytes(r) >= 4;
      }
    check(sizes, "shader_read_format: pack formats carried, texel bytes match the encoding");
    // Pieces: contiguous in the file, aligned to whole thread words, at most max_bytes, sum = image_bytes.
    auto pieces_ok = [&](uint32_t f, uint32_t w, uint32_t h, uint32_t layers, uint32_t mips, uint64_t max_bytes, size_t& n) {
      rp::ShaderRead s;
      if (!rp::shader_read_format(f, false, s)) return false;
      const auto v = rp::shader_read_pieces(s, w, h, layers, mips, max_bytes);
      n = v.size();
      uint64_t at = 0;
      const uint64_t per = rp::shader_read_per_thread(s), tb = rp::shader_read_texel_bytes(s);
      for (const auto& p : v) {
        if (p.file_offset != at || p.first % per || p.bytes != p.count * tb || (p.bytes > max_bytes && p.count > per)) return false;
        at += p.bytes;
      }
      return at == rp::image_bytes(s.pack_format, w, h, layers, mips);
    };
    size_t n1 = 0, n2 = 0, n3 = 0;
    check(pieces_ok(139, 13, 9, 3, 3, 1ull << 30, n1) && n1 == 3, "shader_read_pieces: one piece per mip when they fit (R8, 13x9x3, 3 mips)");
    check(pieces_ok(139, 13, 9, 3, 3, 64, n2) && n2 > 6, "shader_read_pieces: 64-byte pieces cover the image exactly");
    check(pieces_ok(143, 4096, 4096, 2, 13, (256ull << 20) - 32, n3) && n3 == 14, "shader_read_pieces: a 256 MB RGBA16F mip 0 of 2 layers splits in two");
    // Transfer pieces of a BC7 cache array larger than a staging chunk.
    {
      auto tp = rp::transfer_pieces(145, 16384, 8192, 4, 1, 256ull << 20);   // 128 MB per layer: two layers per piece
      uint64_t sum = 0; bool ok = tp.size() == 2;
      for (const auto& p : tp) { ok = ok && p.layers == 2 && p.rows == 8192 && p.file_offset == sum; sum += p.bytes; }
      check(ok && sum == rp::image_bytes(145, 16384, 8192, 4, 1), "transfer_pieces: layer groups of a 512 MB BC7 array");
      tp = rp::transfer_pieces(145, 16384, 16386, 1, 2, 100ull << 20);   // one 256 MB layer: block rows, the last clipped
      sum = 0; ok = !tp.empty();
      for (const auto& p : tp) { ok = ok && p.file_offset == sum && p.bytes <= (100ull << 20) && (p.row % 4) == 0 && (p.rows % 4 == 0 || p.row + p.rows == (p.mip ? 8193u : 16386u)); sum += p.bytes; }
      check(ok && sum == rp::image_bytes(145, 16384, 16386, 1, 2) && tp.back().mip == 1, "transfer_pieces: block rows of one oversized layer, last clipped");
      check(rp::transfer_pieces(37, 1u << 20, 4, 1, 1, 1024).empty(), "transfer_pieces: a row larger than a piece has no plan");
      tp = rp::transfer_pieces(rp::kR32Sfloat, 2560, 1440, 1, 1, 256ull << 20);
      check(tp.size() == 1 && tp[0].bytes == 2560ull * 1440 * 4, "transfer_pieces: a set-0 image is one region");
    }
    // Budget with priority: images the hit material reads (1) go after the alpha-only ones (0).
    bool over = false;
    auto d = rp::drop_over_budget(600, {100, 300, 50, 200}, 1000, over, {1, 1, 0, 0});
    check(!over && d.size() == 2 && d[0] == 2 && d[1] == 3, "drop_over_budget: priority 0 dropped first, even when smaller");
    d = rp::drop_over_budget(600, {100, 300, 50, 200}, 1100, over, {1, 1, 0, 0});
    check(!over && d.size() == 1 && d[0] == 3, "drop_over_budget: within a priority the largest first");
    // The manifest names the source format of a decoded image and round-trips it.
    rp::Manifest s;
    s.version = rp::kVersion; s.pass = "gi_hit_direct"; s.push = "push.bin"; s.groups[0] = s.groups[1] = s.groups[2] = 1;
    s.images = {{1, 14, 3, 43, 4096, 4096, 8, 1, "img4.bin", 146}, {1, 2, 9, 109, 64, 64, 1, 7, "img5.bin", 0}};
    rp::Manifest sb;
    const std::string st = rp::serialize(s);
    check(rp::parse(st, sb, error) && sb.images.size() == 2 && sb.images[0].source_format == 146 && sb.images[1].source_format == 0 &&
              rp::serialize(sb) == st && st.find("img4.bin 146\n") != std::string::npos && st.find("img5.bin\n") != std::string::npos,
          "manifest: image SOURCE format round-trips (absent = copied as is)");
  }

  std::printf("%s: %d failure(s)\n", g_fail ? "FAIL" : "PASS", g_fail);
  return g_fail ? 1 : 0;
}
