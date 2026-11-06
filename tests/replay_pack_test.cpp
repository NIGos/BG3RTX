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
  m.images[1].components = {3, 3, 3, 2}; // R R R ONE
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
            b.images[i].components == m.images[i].components && b.images[i].mips == m.images[i].mips && b.images[i].file == m.images[i].file && b.omitted[i].element == m.omitted[i].element &&
            b.samplers[i].aniso == m.samplers[i].aniso && b.samplers[i].max_lod == m.samplers[i].max_lod && b.samplers[i].aw == m.samplers[i].aw &&
            b.outputs[i].format == m.outputs[i].format && b.outputs[i].reference == m.outputs[i].reference;
  check(lists, "buffers, sparse ranges (file slices), images, omitted, samplers, outputs");
  check(rp::serialize(b) == text, "serialize(parse(text)) == text");
  {
    rp::Manifest bad;
    check(!rp::parse("bg3rtx_replay_pack 1\npass gi_hit_direct\npush p\ndispatch 1 1 1\nrange 1 6 0 f 0\n", bad, error), "a range slice without BYTES is rejected");
  }

  {
    rp::Manifest ls=m;ls.pass="local_shadows";
    ls.specs={{1000,1},{1008,0}};ls.counter_initial="before.bin";ls.counters="after.bin";
    ls.outputs={{0,6,122,128,128,"live.bin","initial.bin"},{0,7,122,128,128,"","initial.bin"}};
    rp::Manifest back;
    const auto text=rp::serialize(ls);
    check(rp::parse(text,back,error) && rp::serialize(back)==text,"LS specs, counter snapshots and output initial round-trip");
    check(back.outputs[1].reference.empty() && back.outputs[1].initial=="initial.bin","LS initial without live reference round-trip");
    rp::Manifest bad;
    check(!rp::parse(text+"spec 1000 0\n",bad,error),"duplicate specialization ID rejected");
    bad={};check(!rp::parse(text+"spec 1009\n",bad,error),"truncated specialization rejected");
    bad={};check(!rp::parse(text+"counter_initial\n",bad,error),"truncated counter initial rejected");
    const auto* pass=rp::find_pass("local_shadows");
    bool unique=true;
    for(uint32_t i=0;i<pass->slot_count;++i)for(uint32_t j=0;j<i;++j)
      unique &= pass->slots[i].set != pass->slots[j].set || pass->slots[i].binding != pass->slots[j].binding;
    check(unique && rp::find_slot(*pass,3,45)->kind==rp::kSampled2D && rp::find_slot(*pass,0,6)->storage_format==122,
          "LS unique descriptor contract, 2D cookies and native packed colour");
  }

  {
    const auto* pass=rp::find_pass("reflections");
    bool unique=pass && pass->set_count==3;
    for(uint32_t i=0;pass && i<pass->slot_count;++i)for(uint32_t j=0;j<i;++j)
      unique &= pass->slots[i].set!=pass->slots[j].set || pass->slots[i].binding!=pass->slots[j].binding;
    check(unique && rp::find_slot(*pass,2,45)->kind==rp::kSampledCube &&
          rp::find_slot(*pass,0,30)->storage_format==76 && rp::find_slot(*pass,0,7)->storage_format==122,
          "RF unique contract, cube probes, R16 hit distance and packed colour");
    check(!rp::find_slot(*pass,1,14) && !rp::find_slot(*pass,3,0),"default RF excludes hit caches and hit sun set");
    rp::Manifest rf=m;rf.pass="reflections";rf.specs={{1000,1}};
    rf.outputs={{0,7,122,128,128,"live.bin","initial.bin"},{0,30,76,128,128,"hit.live","hit.initial"}};
    rf.counter_initial="before.bin";rf.counters="after.bin";rp::Manifest back;
    const auto text=rp::serialize(rf);
    check(rp::parse(text,back,error) && rp::serialize(back)==text,"RF initial/reference/spec/counter round trip");
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

  // 3. Budget: smallest first, each image that still fits; fixed bytes never dropped.
  bool over = false;
  auto dropped = rp::fill_budget(600, {100, 300, 50, 200}, 1000, over);
  check(!over && dropped.size() == 1 && dropped[0] == 1, "fill_budget: 1250 > 1000 drops the 300 only");
  dropped = rp::fill_budget(600, {100, 300, 50, 200}, 700, over);
  check(!over && dropped.size() == 3 && dropped[0] == 0 && dropped[1] == 1 && dropped[2] == 3, "fill_budget: to 700 keeps the 50 only");
  dropped = rp::fill_budget(600, {100, 300, 50, 200}, 950, over);
  check(!over && dropped.size() == 1 && dropped[0] == 1, "fill_budget: the 300 does not fit after 50 + 100 + 200, the smaller ones stay");
  dropped = rp::fill_budget(600, {100, 300, 50, 200}, 1250, over);
  check(!over && dropped.empty(), "fill_budget: exactly at the budget drops nothing");
  dropped = rp::fill_budget(1200, {100}, 1000, over);
  check(over && dropped.size() == 1, "fill_budget: fixed over the budget is reported, everything dropped");
  dropped = rp::fill_budget(10, {100, 200}, 1000, over);
  check(!over && dropped.empty(), "fill_budget: under the budget drops nothing");

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
    // Budget with priority: the hit material's bindings (2) are kept before the alpha caches (1).
    bool over = false;
    auto d = rp::fill_budget(600, {100, 300, 50, 200}, 1050, over, {2, 2, 1, 1});
    check(!over && d.size() == 1 && d[0] == 3, "fill_budget: priority 2 kept first, then the smaller priority-1 image that still fits");
    d = rp::fill_budget(600, {100, 300, 50, 200}, 1000, over, {2, 2, 1, 1});
    check(!over && d.size() == 2 && d[0] == 2 && d[1] == 3, "fill_budget: lower priority dropped first, even when smaller");
    d = rp::fill_budget(600, {100, 300, 50, 200}, 950, over, {2, 2, 1, 1});
    check(!over && d.size() == 1 && d[0] == 1, "fill_budget: a priority-2 image that cannot fit leaves the room to the priority-1 ones");
    // S8 2026-11-01 (MB): 906 fixed, a hit cache larger than the 1142 MB left, a 300 MB alpha
    // cache, the 4 x 4 dummy and 20 table images of 67 MB. The old drop order (lowest priority,
    // largest first) emptied the pack; the fill keeps everything except the cache that cannot fit.
    std::vector<uint64_t> s8 = {1200ull << 20, 300ull << 20, 64};
    std::vector<uint32_t> s8p = {rp::texture_priority(1, 14), rp::texture_priority(1, 3), rp::texture_priority(1, 14)};
    for (int i = 0; i < 20; ++i) { s8.push_back((67ull << 20) / 20); s8p.push_back(rp::texture_priority(1, 2)); }
    d = rp::fill_budget(906ull << 20, s8, rp::kPackHardCapBytes, over, s8p);
    check(!over && d.size() == 1 && d[0] == 0, "fill_budget: S8, only the cache larger than the free space is dropped");
    // replay_pack_cap_mb (rp::pack_cap_bytes): default and clamps; the S8 pack of 2026-11-01
    // (976 MB fixed, the 2054 MB b14 hit cache, the 2 x 1 MB rest) needs 3300 MB to keep the cache.
    check(rp::pack_cap_bytes(2048) == rp::kPackHardCapBytes && rp::pack_cap_bytes(0) == rp::kPackHardCapBytes &&
              rp::pack_cap_bytes(-5) == rp::kPackHardCapBytes && rp::pack_cap_bytes(1) == (uint64_t(rp::kPackCapMinMb) << 20) &&
              rp::pack_cap_bytes(1 << 30) == (uint64_t(rp::kPackCapMaxMb) << 20) && rp::pack_cap_bytes(3300) == (3300ull << 20),
          "pack_cap_bytes: 2048 and <= 0 are the default 2 GB, clamped to [256, 16384] MB");
    {
      const std::vector<uint64_t> live = {2053ull << 20, 1ull << 20, 1ull << 20};
      const std::vector<uint32_t> livep = {rp::texture_priority(1, 14), rp::texture_priority(1, 14), rp::texture_priority(1, 3)};
      d = rp::fill_budget(976ull << 20, live, rp::pack_cap_bytes(2048), over, livep);
      check(!over && d.size() == 1 && d[0] == 0, "fill_budget: S8 at the default cap drops the 2054 MB hit cache");
      d = rp::fill_budget(976ull << 20, live, rp::pack_cap_bytes(3300), over, livep);
      check(!over && d.empty(), "fill_budget: S8 at replay_pack_cap_mb=3300 keeps every image");
      d = rp::fill_budget(976ull << 20, live, rp::pack_cap_bytes(900), over, livep);
      check(over && d.size() == 3, "fill_budget: fixed part over a smaller cap is over budget (pack not written)");
      // That cache through the replay's 256 MB staging (tools/frame_replay.cpp fill_image_pieces):
      // block-row pieces of one layer for mip 0, contiguous, each under the staging, covering the file.
      const uint64_t bytes = rp::image_bytes(138, 32688, 26352, 2, 2);
      const auto pieces = rp::transfer_pieces(138, 32688, 26352, 2, 2, 256ull << 20);
      uint64_t at = 0, largest = 0;
      bool contiguous = true, aligned = true;
      for (const auto& p : pieces) {
        contiguous &= p.file_offset == at;
        aligned &= p.row % 4 == 0 && (p.rows % 4 == 0 || p.row + p.rows == ((p.mip ? 26352u >> p.mip : 26352u)));
        at += p.bytes; largest = (std::max)(largest, p.bytes);
      }
      check((bytes >> 20) == 2053 && at == bytes && contiguous && aligned && largest <= (256ull << 20) && pieces.size() == 10,
            "transfer_pieces: the 2054 MB S8 hit cache in 10 staged pieces (4 row bands per mip-0 layer, 1 per mip-1 layer) of <= 256 MB");
    }
    check(rp::texture_priority(1, 14) == rp::kPriorityHit && rp::texture_priority(1, 2) == rp::kPriorityHit &&
              rp::texture_priority(1, 3) == rp::kPriorityAlphaCache && rp::texture_priority(0, 14) == 0,
          "texture_priority: hit cache and table above the alpha caches; set 0 never droppable anyway");
    check(rp::texture_priority(2, 34) == rp::kPriorityLighting && rp::texture_priority(3, 1) == rp::kPriorityLighting &&
              rp::texture_priority(2, 45) == rp::kPriorityProbes && rp::kPriorityLighting > rp::kPriorityHit &&
              rp::kPriorityHit > rp::kPriorityProbes && rp::kPriorityProbes > rp::kPriorityAlphaCache,
          "texture_priority: lighting inputs first, probe cubes below the hit material, above the alpha caches");
    {   // The S8 pack under replay_pack_cap_mb=3300 with lighting inputs: 976 MB fixed, the 2054 MB hit
        // cache, 60 MB of lighting images and 400 MB of probe cubes (more than the 211 MB left): the
        // cache and the lighting images stay, the probe cubes take what is left, smallest first.
      std::vector<uint64_t> sz = {2053ull << 20, 30ull << 20, 30ull << 20};
      std::vector<uint32_t> pr = {rp::texture_priority(1, 14), rp::texture_priority(2, 2), rp::texture_priority(3, 1)};
      for (int i = 0; i < 100; ++i) { sz.push_back(4ull << 20); pr.push_back(rp::texture_priority(2, 45)); }
      d = rp::fill_budget(976ull << 20, sz, rp::pack_cap_bytes(3300), over, pr);
      bool cache_kept = true, lighting_kept = true;
      for (size_t i : d) { cache_kept &= i != 0; lighting_kept &= i != 1 && i != 2; }
      check(!over && cache_kept && lighting_kept && d.size() == 100 - (3300 - 976 - 2053 - 60) / 4,
            "fill_budget: S8 + lighting inputs keeps the hit cache and the lighting images, probe cubes fill the rest");
    }
    {   // Offline water adapter keeps native format and extends, rather than changes, GI.
      const auto* water = rp::find_pass("water_compose_fixture");
      const auto* gi = rp::find_pass("gi_hit_direct");
      const auto* target = water ? rp::find_slot(*water, 0, 67) : nullptr;
      check(water && target && target->kind == rp::kStorage && target->storage_format == 122 &&
            water->set_count == gi->set_count && water->push_bytes == gi->push_bytes &&
            !rp::find_slot(*gi, 0, 67) && rp::find_slot(*water, 0, 61)->kind == rp::kSsbo,
            "water fixture adapter: native target and side-effect buffers, ordinary GI contract unchanged");
    }
    {   // Lighting contract: every set 2-3 slot but the declared sampler has a game source, sources name slots.
      const rp::Pass& gp = *rp::find_pass("gi_hit_direct");
      bool ok = gp.set_count == 4;
      uint32_t slots = 0;
      for (uint32_t i = 0; i < gp.slot_count; ++i) {
        const rp::Slot& s = gp.slots[i];
        if (s.set < 2) continue;
        ++slots;
        ok &= (s.set == 3 && s.binding == 6) ? rp::lighting_source(3, 6) == nullptr : rp::lighting_source(s.set, s.binding) != nullptr;
      }
      for (const auto& src : rp::kLightingSources) ok &= rp::find_slot(gp, src.set, src.binding) != nullptr;
      const rp::Slot* cubes = rp::find_slot(gp, 2, 45);
      ok &= slots == std::size(rp::kLightingSources) + 1 && cubes && cubes->kind == rp::kSampledCube && rp::find_slot(gp, 2, 34)->kind == rp::kSampledCube &&
            rp::lighting_source(3, 2)->game_set == 5 && rp::lighting_source(3, 2)->game_binding == 4 && rp::lighting_source(3, 4)->game_binding == 42;
      check(ok, "lighting contract: sets 2-3 slots, game sources (set 3 = set 5 b0 b1 b4 b5, set 1 b42 b43), cube kinds");
    }
    {   // Probe records -> b45 elements: diffuse pair (m5 bit 1, bits 3-16 / 17-30), specular pair (m5 bit 2, m6).
      std::vector<uint8_t> rec(3 * rp::kProbeStride + 7, 0);   // a trailing partial record is ignored
      auto put = [&](size_t at, uint32_t v) { for (int k = 0; k < 4; ++k) rec[at + k] = uint8_t(v >> (8 * k)); };
      put(0 * 144 + 136, 2u | (1u << 3) | (3u << 17)); put(0 * 144 + 140, 5u);      // diffuse 1, 3; m6 ignored (no bit 2)
      put(1 * 144 + 136, 4u | 1u);  put(1 * 144 + 140, 2u | (2049u << 16));          // box, specular 2 and 2049 (beyond)
      put(2 * 144 + 136, 6u | (1u << 3)); put(2 * 144 + 140, 1u);                    // both: diffuse 1 again, specular 1
      uint32_t beyond = 0;
      const auto e = rp::probe_cube_elements(rec.data(), rec.size(), 4096, beyond);
      const std::vector<uint32_t> want = {0, 1, 2, 3, 4, 5};
      check(e == want && beyond == 2, "probe_cube_elements: pairs 2i, 2i+1 per field, unique, sorted; past the slot counted");
      const auto none = rp::probe_cube_elements(nullptr, 0, 4096, beyond);
      check(none.empty() && beyond == 0, "probe_cube_elements: no records, no elements");
    }
    {   // lighting_inputs: what a pack carries (older packs: nothing).
      rp::Manifest l;
      check(!rp::lighting_inputs(l).ambient && !rp::lighting_inputs(l).sun_lut, "lighting_inputs: an older pack has none");
      l.images = {{2, 34, 0, 97, 32, 32, 6, 6, "a"}, {2, 38, 0, 83, 128, 128, 1, 1, "b"}, {2, 35, 0, 97, 128, 128, 6, 8, "c"},
                  {2, 45, 0, 97, 64, 64, 6, 7, "d"}, {2, 45, 1, 97, 64, 64, 6, 7, "e"}, {3, 1, 0, 97, 256, 64, 1, 1, "f"}};
      l.buffers = {{3, 0, 16, "atmo"}, {2, 33, 288, "probes"}};
      const auto li = rp::lighting_inputs(l);
      check(li.ambient && li.sky && li.sun_lut && li.probes && li.images == 6 && li.probe_cubes == 2,
            "lighting_inputs: irradiance + DFG = ambient, specular cube = sky, LUT + constants = sun, probe records and cubes");
      l.buffers.clear();
      check(!rp::lighting_inputs(l).sun_lut, "lighting_inputs: a LUT without the atmosphere constants is no LUT sun");
    }
    check(rp::raw_compatible(145, 146) && rp::raw_compatible(137, 138) && rp::raw_compatible(37, 43) && !rp::raw_compatible(131, 145) &&
              !rp::raw_compatible(37, 97) && !rp::raw_compatible(145, 107) && !rp::raw_compatible(0, 0),
          "raw_compatible: BC7/BC3 UNORM-SRGB and RGBA8 share bytes; BC1 vs BC7, RGBA8 vs RGBA16F, BC7 vs RGBA32UI do not");
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

  // 6. rt_cascade_casters: the casters directive, its InstInfo patch, the variant defines a pack keeps.
  {
    rp::Manifest c;
    c.version = rp::kVersion; c.pass = "gi_hit_direct"; c.push = "push.bin"; c.groups[0] = c.groups[1] = c.groups[2] = 1;
    c.instances = "instances.bin"; c.instance_count = 3;
    c.casters = {"casters.bin", 2, 759945, 16};
    rp::Manifest cb;
    const std::string ct = rp::serialize(c);
    check(rp::parse(ct, cb, error) && cb.casters.file == "casters.bin" && cb.casters.instance == 2 && cb.casters.tris == 759945 &&
              cb.casters.stride == 16 && rp::serialize(cb) == ct && ct.find("casters casters.bin 2 759945 16\n") != std::string::npos,
          "manifest: the casters directive round-trips");
    rp::Manifest none;
    check(rp::parse("bg3rtx_replay_pack 1\npass gi_hit_direct\npush p\ndispatch 1 1 1\n", none, error) && none.casters.file.empty() &&
              rp::serialize(none).find("casters") == std::string::npos,
          "manifest: a pack without casters has none and writes none");
    rp::Manifest bad;
    check(!rp::parse("bg3rtx_replay_pack 1\npass gi_hit_direct\npush p\ndispatch 1 1 1\ncasters c.bin 0 0 16\n", bad, error) &&
              !rp::parse("bg3rtx_replay_pack 1\npass gi_hit_direct\npush p\ndispatch 1 1 1\ncasters c.bin 0 5 16\ncasters d.bin 0 5 16\n", bad, error),
          "manifest: casters with 0 triangles or twice is rejected");
    const uint64_t bytes = rp::caster_bytes(759945, 16);
    check(bytes == 759945ull * 48 && rp::casters_valid(c.casters, 3, bytes) && !rp::casters_valid(c.casters, 2, bytes) &&
              !rp::casters_valid(c.casters, 3, bytes - 4) && !rp::casters_valid({"c", 0, 1, 8}, 1, 24) && !rp::casters_valid({"c", 0, 1, 18}, 1, 54) &&
              !rp::casters_valid({"", 0, 1, 16}, 1, 48),
          "casters_valid: instance in the TLAS, file of tris x 3 x stride, stride >= 12 and a multiple of 4");
    // The live layout (layer/bvhsplit.cpp): instance 2 is the cascade one, {0, stride, 0, 0}.
    uint32_t inf[12] = {0, 64, 0, 2, 4096, 32 | rp::kInfoPool, 0, 4, 0, 16, 0, 0};
    const uint64_t end = rp::caster_patch(inf, 3, {"c", 2, 10, 16}, 1000);
    check(end == 1008 + 480 && inf[8] == 1008 && inf[9] == 16 && inf[10] == 0 && inf[11] == 10 && inf[0] == 0 && inf[3] == 2 && inf[7] == 4,
          "caster_patch: the entry points past the soup (16-aligned) with its triangles; others untouched");
    std::vector<rp::Span> soup_s, pool_s;
    rp::instinfo_spans(inf, 3, soup_s, pool_s);
    check(soup_s.size() == 2 && soup_s[1].offset == 1008 && soup_s[1].bytes == 480, "caster_patch: the patched entry names its span in the soup");
    uint32_t again[12]; std::memcpy(again, inf, sizeof inf);
    uint32_t wrong[12] = {0, 64, 0, 2, 4096, 32 | rp::kInfoPool, 0, 4, 0, 24, 0, 0};
    uint32_t alpha[12] = {0, 64, 0, 2, 4096, 32 | rp::kInfoPool, 0, 4, 0, 16 | rp::kInfoAlpha, 0, 0};
    check(rp::caster_patch(again, 3, {"c", 2, 10, 16}, 1000) == 0 && rp::caster_patch(wrong, 3, {"c", 2, 10, 16}, 1000) == 0 &&
              rp::caster_patch(alpha, 3, {"c", 2, 10, 16}, 1000) == 0 && rp::caster_patch(inf, 2, {"c", 2, 10, 16}, 1000) == 0 &&
              wrong[8] == 0 && wrong[11] == 0,
          "caster_patch: refuses an entry with triangles, another stride, alpha, or past the TLAS");
    uint32_t big[4] = {0, 16, 0, 0};
    check(rp::caster_patch(big, 1, {"c", 0, 100, 16}, 0xFFFFF000ull) == 0 && big[3] == 0, "caster_patch: refuses a soup past 4 GB");
    std::string dropped;
    check(rp::contract_defines("RT_OMM_SCENE=1;RT_NATIVE_SCALP=1;RT_NATIVE_HAIR=1;RT_HIT_INSTANCED=1;RT_CASCADE_MASKS=1", &dropped) == "RT_CASCADE_MASKS=1" &&
              dropped == "RT_OMM_SCENE=1;RT_NATIVE_SCALP=1;RT_NATIVE_HAIR=1;RT_HIT_INSTANCED=1" && rp::contract_defines("", &dropped).empty() &&
              dropped.empty() && rp::contract_defines("RT_CASCADE_MASKS=1") == "RT_CASCADE_MASKS=1" && rp::contract_defines("RT_OMM_SCENE=1").empty(),
          "contract_defines: RT_CASCADE_MASKS kept, bindings the contract lacks dropped and named");
  }

  std::printf("%s: %d failure(s)\n", g_fail ? "FAIL" : "PASS", g_fail);
  return g_fail ? 1 : 0;
}
