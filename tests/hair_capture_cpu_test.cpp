// common/hair_capture.h (the layer capture's CPU side) and its round trip into bg3rtx_hairconv:
// - the full-mask rule (128x128 one-mip placeholders never feed the converter; upgrades re-capture);
// - the card stretch statistic;
// - a capture built the way layer/hair_capture.cpp builds it (per-corner soup in index order, indices with any base)
//   encodes to .hcap v2, which hairconv::read_hcap reads back to the v1 fixture's mesh and mask, and converts to the
//   same groom (tests/fixtures/hairconv/cards.hcap, synthetic, no game data);
// - a BC3 mask (raw blocks, as the layer copies mip 0) decodes to the same mask and the same groom as its RGBA8 copy.
#include "../common/hair_capture.h"
#include "../tools/hairconv/hairconv.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <random>
#include <string>

using namespace bg3::hair_capture;
#define CHECK(x) do { if (!(x)) { std::printf("FAIL %d: %s\n", __LINE__, #x); std::exit(1); } } while (0)

static std::string save(const std::vector<uint8_t>& bytes, const char* name) {
  const auto p = std::filesystem::temp_directory_path() / name;
  std::ofstream(p, std::ios::binary).write(reinterpret_cast<const char*>(bytes.data()), std::streamsize(bytes.size()));
  return p.string();
}
static bool same(const hairconv::Groom& a, const hairconv::Groom& b) {
  return a.offsets == b.offsets && a.triangle == b.triangle && a.bary == b.bary && a.radii == b.radii && a.root == b.root;
}
static hairconv::Groom groom_of(const hairconv::Input& in) {
  int grid[2];
  double fit;
  hairconv::detect_tile_grid(in.mesh, grid, &fit);
  hairconv::Report rep;
  int dropped = 0;
  return hairconv::dedupe(hairconv::convert(in.mesh, in.mask, grid, rep, 2), &dropped, 2);
}
static Capture capture_of(const hairconv::Mesh& m, uint32_t base) {
  Capture c;
  for (uint32_t i : m.tris) {
    c.indices.push_back(i + base);
    for (int k = 0; k < 3; ++k) c.corner_pos.push_back(m.pos[size_t(i) * 3 + k]);
    for (int k = 0; k < 2; ++k) c.corner_uv.push_back(m.uv[size_t(i) * 2 + k]);
  }
  return c;
}

int main(int argc, char** argv) {
  CHECK(argc >= 2);
  // Full-mask rule.
  CHECK(!full_mask({128, 128, 1, 0}, 0, 0));                 // the game's streaming placeholder
  CHECK(!full_mask({128, 128, 8, 0}, 0, 0));                 // too small even with mips
  CHECK(!full_mask({2048, 2048, 1, 0}, 0, 0));               // one mip: not the full texture
  CHECK(full_mask({2048, 2048, 12, 0}, 0, 0));
  CHECK(!full_mask({1024, 1024, 11, 0}, 2048, 2048));        // smaller than seen before
  CHECK(upgrade({1024, 1024, 11, 0}, {2048, 2048, 12, 0}) && !upgrade({2048, 2048, 12, 0}, {2048, 2048, 12, 0}));
  // Retry state machine (hcap2-capture 2026-11-08: placeholders seen first were never looked at again).
  {
    Retry r;
    CHECK(should_look(r, 10, 0));                         // first sight
    CHECK(!looked(r, false, false, 0));                   // a placeholder
    CHECK(!should_look(r, 10, 1) && !should_look(r, 10, kRetryFrames - 1));   // same image: throttled
    CHECK(should_look(r, 10, kRetryFrames));              // periodic look
    CHECK(should_look(r, 11, kRetryFrames + 1));          // a new image (the upgrade): at once
    looked(r, true, true, kRetryFrames + 1, {2048, 2048, 12, 0});
    CHECK(r.state == RetryState::Captured && r.captured.width == 2048);
    CHECK(!should_look(r, 11, 100000));                   // captured: never again for the same image
    CHECK(should_look(r, 12, 100001));                    // a new image may be an upgrade (want() checks upgrade())
    Retry far;                                            // a distant NPC whose mask never leaves 128 x 128
    uint64_t now = 0;
    int looks = 0, never = 0;
    for (; now < 400 * kRetryFrames; ++now)
      if (should_look(far, 7, now)) { ++looks; never += looked(far, false, false, now); }
    CHECK(looks == int(kNeverFullChecks) && never == 1 && far.state == RetryState::NeverFull);
    CHECK(should_look(far, 8, now) && far.state == RetryState::Waiting);   // its image changed: a fresh chance
  }
  // Stretch.
  const float a[9] = {0, 0, 0, 1, 0, 0, 0, 1, 0}, b[9] = {0, 0, 0, 2, 0, 0, 0, 1, 0};
  Stretch s0 = stretch(a, a, 3), s1 = stretch(a, b, 3);
  CHECK(s0.min == 1 && s0.max == 1 && std::fabs(s1.max - 2) < 1e-6 && s1.min == 1 && std::fabs(s1.median - std::sqrt(2.5f)) < 1e-6);
  // Round trip of the fixture through the layer's capture layout (RGBA8 mask).
  hairconv::Input v1;
  std::string error;
  CHECK(hairconv::read_hcap((std::filesystem::path(argv[1]) / "cards.hcap").string(), v1, &error));
  Capture c = capture_of(v1.mesh, 1000);
  c.mask = {uint32_t(v1.mask.w), uint32_t(v1.mask.h), 10, 0};
  c.mask_format = kMaskRgba8;
  c.mask_bytes = v1.mask.rgba;
  c.stretch = {0.98f, 1.0f, 1.03f};
  const std::vector<uint8_t> bytes = encode_hcap(c);
  CHECK(!bytes.empty());
  hairconv::Input v2;
  CHECK(hairconv::read_hcap(save(bytes, "hair_capture_rgba.hcap"), v2, &error));
  CHECK(v2.mesh.tris == v1.mesh.tris && v2.mesh.pos == v1.mesh.pos && v2.mesh.uv == v1.mesh.uv && v2.mask.rgba == v1.mask.rgba);
  CHECK(v2.stretch[0] == 0.98f && v2.stretch[2] == 1.03f && !v2.has_head);
  CHECK(hairconv::index_id(v2.mesh) == hairconv::index_id(v1.mesh));
  const hairconv::Groom g1 = groom_of(v1), g2 = groom_of(v2);
  CHECK(g1.strands() > 0 && same(g1, g2));
  // A BC3 mask as the layer copies it: random blocks, decoded once for the RGBA8 twin.
  const uint32_t W = 512, H = 128;
  std::vector<uint8_t> blocks(bc3_bytes(W, H));
  std::mt19937 rng(5);
  for (size_t i = 0; i < blocks.size(); i += 16) {
    blocks[i] = 255; blocks[i + 1] = uint8_t(rng() % 64);            // alpha endpoints: mostly covered
    for (int k = 2; k < 8; ++k) blocks[i + k] = uint8_t(rng() & 0x49);   // index bits biased to the opaque end
    for (int k = 8; k < 16; ++k) blocks[i + k] = uint8_t(rng());
  }
  std::vector<uint8_t> rgba;
  decode_bc3(blocks.data(), W, H, rgba);
  Capture cb = capture_of(v1.mesh, 0), cr = cb;
  cb.mask = cr.mask = {W, H, 10, 0};
  cb.mask_format = kMaskBc3; cb.mask_bytes = blocks;
  cr.mask_format = kMaskRgba8; cr.mask_bytes = rgba;
  CHECK(encode_hcap(Capture{}).empty());
  hairconv::Input ib, ir;
  CHECK(hairconv::read_hcap(save(encode_hcap(cb), "hair_capture_bc3.hcap"), ib, &error));
  CHECK(hairconv::read_hcap(save(encode_hcap(cr), "hair_capture_bc3_rgba.hcap"), ir, &error));
  CHECK(ib.mask.rgba == ir.mask.rgba && ib.mask.rgba == rgba);
  CHECK(same(groom_of(ib), groom_of(ir)));
  // Truncated or mislabelled files are refused.
  std::vector<uint8_t> bad = encode_hcap(cb);
  bad.pop_back();
  CHECK(!hairconv::read_hcap(save(bad, "hair_capture_bad.hcap"), ib, &error));
  std::printf("hair capture: full-mask rule, stretch, layer layout -> hairconv round trip (%u strands), BC3 mask\n", g1.strands());
  return 0;
}
