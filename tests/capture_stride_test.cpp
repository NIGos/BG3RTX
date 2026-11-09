// rt_capture_wide_split (common/capture_stride.h): per-module stride, wide region placement, cache/refit guards,
// and the patched decorations of a strand-frame VS vs an ordinary one (layer/spirv_xfb.h, the decision the layer makes).
#include "../common/capture_stride.h"
#include "../layer/spirv_xfb.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <vector>
using namespace bg3::capture_stride;
static unsigned checks;
static void check(bool ok) { ++checks; if (!ok) { std::fprintf(stderr, "capture stride check %u failed\n", checks); std::exit(1); } }
static constexpr uint64_t mib = uint64_t(1) << 20;

// A minimal vertex shader: Position (BuiltIn) output, a vec2 UV at location 0 and, optionally, the native strand
// frame (N/T/B/W float3 at locations 1..4, fade vec4 at 5). Only the instructions patch_vs inspects.
static std::vector<uint32_t> vs(bool strand) {
  std::vector<uint32_t> w = {0x07230203u, 0x00010000u, 0u, 100u, 0u};
  auto op = [&](uint32_t code, std::vector<uint32_t> a) { w.push_back(uint32_t(a.size() + 1) << 16 | code); w.insert(w.end(), a.begin(), a.end()); };
  op(17, {1});                       // OpCapability Shader
  op(15, {0, 4, 0x6e69616d, 0});     // OpEntryPoint Vertex %4 "main"
  op(71, {10, 11, 0});               // OpDecorate %10 BuiltIn Position
  op(71, {11, 30, 0});               // OpDecorate %11 Location 0 (UV)
  if (strand) for (uint32_t k = 0; k < 4; ++k) op(71, {20 + k, 30, 1 + k});   // N/T/B/W at 1..4
  op(22, {30, 32});                  // %30 float
  op(23, {31, 30, 4});               // %31 vec4
  op(23, {32, 30, 2});               // %32 vec2
  op(23, {33, 30, 3});               // %33 vec3
  op(32, {40, 3, 31});               // ptr Output vec4
  op(32, {41, 3, 32});               // ptr Output vec2
  op(32, {42, 3, 33});               // ptr Output vec3
  op(59, {40, 10, 3});               // %10 Position
  op(59, {41, 11, 3});               // %11 UV
  if (strand) for (uint32_t k = 0; k < 4; ++k) op(59, {42, 20 + k, 3});
  w[3] = 64;
  return w;
}
static uint32_t xfb_stride(const std::vector<uint32_t>& code) {   // the XfbStride decoration on Position
  for (size_t p = 5; p < code.size() && (code[p] >> 16);) {
    const uint32_t n = code[p] >> 16;
    if ((code[p] & 0xffff) == 71 && n >= 4 && code[p + 1] == 10 && code[p + 2] == 37) return code[p + 3];
    p += n;
  }
  return 0;
}
// What geo_patch_shader does: patch at the full stride; a module without the strand frame again at the module stride.
static uint32_t layer_patch(const std::vector<uint32_t>& in, bool active, uint32_t full, uint32_t base, std::vector<uint32_t>& out) {
  int uv = -1;
  uint32_t hair = 0;
  if (!bg3::patch_vs(in.data(), in.size(), out, full, &uv, &hair)) return 0;
  const uint32_t s = module_stride(active, full, base, hair);
  if (s != full && !bg3::patch_vs(in.data(), in.size(), out, s, &uv, &hair)) return 0;
  return s;
}

int main() {
  // Activation: only a wide layout over a wider-than-24 base in the alpha (prepass) capture.
  check(active(true, true, 88, 64));
  check(!active(false, true, 88, 64));
  check(!active(true, false, 88, 64));
  check(!active(true, true, 64, 64));
  check(!active(true, true, 88, 16));
  check(active(true, true, 88, 24));   // rt_hit_material off: 24-byte base
  check(active(true, true, 88, 80));   // native tileset material frame: 80-byte base
  // Module stride: off -> always the full stride (key off: identical patches).
  check(module_stride(false, 88, 64, 0) == 88 && module_stride(false, 88, 64, 1) == 88);
  check(module_stride(true, 88, 64, 0) == 64 && module_stride(true, 88, 64, 1) == 88 && module_stride(true, 88, 64, 3) == 88);
  check(!wide_draw(false, 88, 88) && !wide_draw(true, 64, 64) && wide_draw(true, 88, 64));
  // The patched modules: the strand-frame VS keeps 88, the ordinary one gets 64; off, both 88 and byte-identical
  // to a plain patch at 88.
  {
    std::vector<uint32_t> out, ref;
    int uv = -1; uint32_t hair = 0;
    check(layer_patch(vs(true), true, 88, 64, out) == 88 && xfb_stride(out) == 88);
    check(bg3::patch_vs(vs(true).data(), vs(true).size(), ref, 88, &uv, &hair) && hair == 1u && ref == out);
    check(layer_patch(vs(false), true, 88, 64, out) == 64 && xfb_stride(out) == 64);
    check(bg3::patch_vs(vs(false).data(), vs(false).size(), ref, 64, &uv, &hair) && ref == out && uv == 0);
    check(layer_patch(vs(false), false, 88, 88, out) == 88 && xfb_stride(out) == 88);
    check(bg3::patch_vs(vs(false).data(), vs(false).size(), ref, 88, &uv, &hair) && ref == out && hair == 0u);
  }
  // Placement: the alpha cursor grows up, the wide cursor down from the buffer end; neither crosses the other.
  {
    const uint64_t size = 2047 * mib, alpha_base = 1229 * mib;
    uint64_t alpha = alpha_base, wide = size, off = 0;
    const uint64_t hair = 3ull * 88 * 1000;   // a 1000-triangle wide draw
    check(place_wide(alpha, &wide, hair, &off) && off == size - hair && wide == off && off % 4 == 0);
    check(alpha_fits(alpha, wide - alpha, wide) && !alpha_fits(alpha, wide - alpha + 1, wide));
    alpha = wide - 64;   // nearly full
    uint64_t before = wide;
    check(!place_wide(alpha, &wide, hair, &off) && wide == before);
    check(place_wide(alpha, &wide, 48, &off) && wide == before - 48 && alpha <= wide);
    // No alpha region (alpha_base == size): nothing wide fits.
    uint64_t w2 = size;
    check(!place_wide(size, &w2, 3 * 88, &off) && w2 == size);
    check(!alpha_fits(5, 10, 4) && !place_wide(0, &w2, size + 1, &off));
  }
  // The wide region's storage binding: every reservation a whole number of triangles and of the device alignment,
  // so the region's lowest byte stays aligned below an aligned top (16 on the 5090; 256 is the largest allowed).
  check(wide_unit(88, 16) == 528 && wide_unit(88, 256) == 8448 && wide_unit(64, 16) == 192 && wide_unit(88, 1) == 264);
  check(wide_reserve(264, 528) == 528 && wide_reserve(528, 528) == 528 && wide_reserve(0, 528) == 0 && wide_reserve(7, 0) == 7);
  for (const uint64_t align : {16ull, 64ull, 256ull}) {
    const uint64_t unit = wide_unit(88, align), top = 2047 * mib;
    uint64_t wide = top, off = 0;
    for (uint64_t tris : {1ull, 2ull, 3ull, 1000ull, 4097ull}) {
      check(place_wide(0, &wide, wide_reserve(tris * 264, unit), &off));
      check(off % align == 0 && (top - off) % 264 == 0 && off % 4 == 0);
    }
  }
  // Mesh cache and per-draw BLAS refit guards.
  check(mesh_stride_matches(64, 64) && !mesh_stride_matches(88, 64) && !mesh_stride_matches(0, 0));
  check(slot_refit_ok(88, 88) && !slot_refit_ok(64, 88) && !slot_refit_ok(0, 64));
  // The S8 view (rv1-s8-release-verify 2026-11-09 12:13): opaque 1613 + alpha 446 MB at 88 bytes for every draw.
  // With every opaque draw at 64 bytes and the alpha draws still all wide (the upper bound): 1173 + 446 = 1619 MB.
  {
    const double opaque = split_bytes(1613.1, 0.0, 88, 64), alpha = split_bytes(445.8, 1.0, 88, 64);
    check(std::fabs(opaque - 1173.2) < 0.1 && std::fabs(alpha - 445.8) < 1e-9);
    check(opaque + alpha < 2047.0 * 0.8);   // fits with the capture's 25% movement reserve
    check(split_bytes(100.0, 0.25, 88, 64) == 25.0 + 75.0 * 64.0 / 88.0);
    check(split_bytes(100.0, 0.5, 0, 64) == 100.0);
  }
  std::printf("capture stride: %u checks passed\n", checks);
  return 0;
}
