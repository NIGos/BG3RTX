// common/cascade_casters.h: cascade pass detection, the camera/cascade dedupe key, the
// orthographic clip -> world inversion (CPU mirror of xfb_to_world.hlsl XFB_ORTHO), the
// rebase translation and the fragment-discard scan.
#include "../common/cascade_casters.h"

#include <cstdio>
#include <cstring>
#include <vector>

using namespace cascade;

static int fail(int code, const char* what) {
  std::printf("cascade_casters_test: FAIL %d %s\n", code, what);
  return code;
}

// Forward: what the cascade VS writes for camera-relative world W (y negated).
static void project(const float m[4][4], const float w[3], float clip[4]) {
  for (int j = 0; j < 4; ++j) clip[j] = w[0] * m[0][j] + w[1] * m[1][j] + w[2] * m[2][j] + m[3][j];
  clip[1] = -clip[1];
}

int main() {
  // The old opaque stream must retain EXACTLY its legacy policy even when
  // the current material stream also records cards the old path skipped.
  for(int mode=0;mode<=2;++mode) {
    if(legacy_caster(false,mode,false,true,false,false)!=LegacyCaster::keep) return fail(60,"opaque retained");
    if(legacy_caster(false,mode,false,true,true,false)!=LegacyCaster::keep) return fail(61,"opaque ignores alpha mesh dedupe");
    if(legacy_caster(false,mode,false,true,false,true)!=LegacyCaster::duplicate) return fail(62,"placed duplicate removed");
  }
  if(legacy_caster(true,0,false,false,false,false)!=LegacyCaster::alpha_skip) return fail(63,"default unknown discard stays skipped");
  if(legacy_caster(true,0,true,false,false,false)!=LegacyCaster::alpha_skip) return fail(64,"default cutout stays skipped");
  if(legacy_caster(true,1,true,false,false,false)!=LegacyCaster::keep) return fail(65,"explicit solid experiment retained");
  if(legacy_caster(true,2,true,false,false,false)!=LegacyCaster::alpha_skip) return fail(66,"texture cutout never solid in mode2");
  if(legacy_caster(true,2,false,false,true,false)!=LegacyCaster::keep) return fail(67,"scalar mode2 allowed without dedupe");
  if(legacy_caster(true,2,false,true,true,false)!=LegacyCaster::duplicate) return fail(68,"scalar duplicate removed");
  // ---- pass detection ----
  if (pass_layer(0, true, 2048, 2048, 4, 1) != 1) return fail(1, "cascade layer 1");
  if (pass_layer(0, true, 2048, 2048, 4, 0) != 0) return fail(2, "cascade layer 0");
  if (pass_layer(1, true, 2048, 2048, 4, 1) != -1) return fail(3, "colour attachment");
  if (pass_layer(0, false, 2048, 2048, 4, 1) != -1) return fail(4, "not D16");
  if (pass_layer(0, true, 2560, 1067, 4, 1) != -1) return fail(5, "not square");
  if (pass_layer(0, true, 2048, 2048, 2, 1) != -1) return fail(6, "far cache (2 layers)");
  if (pass_layer(0, true, 512, 512, 4, 1) != -1) return fail(7, "too small");
  if (pass_layer(0, true, 2048, 2048, 4, 4) != -1) return fail(8, "layer out of range");

  // ---- dedupe key ----
  DrawId a;
  a.kind = 1, a.count = 300, a.instances = 4, a.first = 12, a.vertex_offset = 7;
  a.vb = 0x1000, a.vb_offset = 64, a.ib = 0x2000, a.ib_offset = 128, a.ib_type = 1;
  a.place = 0x3000, a.place_offset = 256;
  DrawId b = a;
  if (draw_key(a) != draw_key(b)) return fail(10, "equal draws, equal keys");
  b.place_offset = 512;   // the same mesh placed elsewhere (another instance stream range)
  if (draw_key(a) == draw_key(b)) return fail(11, "placement must count");
  b = a, b.instances = 5;
  if (draw_key(a) == draw_key(b)) return fail(12, "instance count must count");
  b = a, b.ib_offset = 0;
  if (draw_key(a) == draw_key(b)) return fail(13, "index range must count");
  b = a, b.vertex_offset = 0;
  if (draw_key(a) == draw_key(b)) return fail(14, "vertex offset must count");
  b = a, b.kind = 0;
  DrawId c = b;
  c.ib = 0x9999;   // non-indexed: the bound index buffer is irrelevant
  if (draw_key(b) != draw_key(c)) return fail(15, "non-indexed ignores the index buffer");

  KeySets ks;
  ks.insert(100, 1), ks.insert(100, 2);
  if (!ks.contains(100, 1) || !ks.contains(101, 2)) return fail(20, "this frame and the next see the keys");
  if (ks.contains(102, 1)) return fail(21, "two frames later: stale");
  ks.insert(101, 3);
  if (!ks.contains(101, 1) || !ks.contains(101, 3)) return fail(22, "union of two frames");
  ks.insert(102, 4);   // reuses frame 100's set
  if (ks.contains(102, 1) || !ks.contains(102, 3) || !ks.contains(102, 4)) return fail(23, "ring of two");
  if (ks.size(102) != 1) return fail(24, "fresh set per frame");

  // ---- orthographic inversion ----
  // A sun cascade: orthonormal light axes, 60 m x 60 m x 400 m deep, off-centre.
  const float s = 0.70710678f;
  const float ax[3] = {s, 0, -s}, ay[3] = {0.40824829f, 0.81649658f, 0.40824829f},
              az[3] = {0.57735027f, -0.57735027f, 0.57735027f};
  const float ex = 60, ey = 60, ez = 400;
  float m[4][4] = {};
  for (int k = 0; k < 3; ++k) {
    m[k][0] = ax[k] * 2 / ex;
    m[k][1] = ay[k] * 2 / ey;
    m[k][2] = az[k] / ez;
    m[k][3] = 0;
  }
  m[3][0] = 0.1f, m[3][1] = -0.2f, m[3][2] = 0.5f, m[3][3] = 1;
  Ortho o = ortho_check(m);
  if (!o.ok) return fail(30, "orthographic matrix accepted");
  if (std::fabs(o.extent[0] - ex) > 1e-3f || std::fabs(o.extent[1] - ey) > 1e-3f || std::fabs(o.extent[2] - ez) > 1e-2f)
    return fail(31, "extents");
  const float pts[4][3] = {{0, 0, 0}, {12.5f, -3.25f, 7}, {-25, 40, -18.5f}, {150.f, 2.f, -90.f}};
  for (const auto& w : pts) {
    float clip[4], back[3];
    project(m, w, clip);
    if (!clip_to_world(m, clip, back)) return fail(32, "valid vertex rejected");
    for (int k = 0; k < 3; ++k)
      if (std::fabs(back[k] - w[k]) > 1e-3f * (1.0f + std::fabs(w[k]))) return fail(33, "round trip");
  }
  {
    float clip[4], back[3];
    project(m, pts[1], clip);
    clip[3] = 0.5f;   // not this matrix's output (perspective w)
    if (clip_to_world(m, clip, back)) return fail(34, "wrong w accepted");
    clip[3] = NAN;
    if (clip_to_world(m, clip, back)) return fail(35, "NaN accepted");
  }
  float persp[4][4] = {};
  std::memcpy(persp, m, sizeof m);
  persp[2][3] = -1, persp[3][3] = 0;   // a perspective w row
  if (ortho_check(persp).ok) return fail(36, "perspective matrix rejected");
  float flat[4][4] = {};
  flat[3][3] = 1;
  if (ortho_check(flat).ok) return fail(37, "singular matrix rejected");

  // ---- rebase ----
  const float prev[3] = {100.f, 5.f, -40.f}, cur[3] = {101.5f, 5.f, -39.f};
  float t[3];
  rebase(prev, cur, t);
  // A world point P: relative to prev it was P - prev; translated, it must be P - cur.
  const float P[3] = {120.f, 9.f, -30.f};
  for (int k = 0; k < 3; ++k)
    if (std::fabs((P[k] - prev[k]) + t[k] - (P[k] - cur[k])) > 1e-5f) return fail(40, "rebase translation");

  // ---- fragment discard scan ----
  // Minimal modules: header, OpEntryPoint <model> %1 "m", then the opcode under test.
  auto module = [](uint32_t model, uint32_t op) {
    std::vector<uint32_t> w = {0x07230203u, 0x00010000u, 0, 10, 0};
    w.push_back((4u << 16) | 15u), w.push_back(model), w.push_back(1), w.push_back(0x6d);   // OpEntryPoint
    if (op) w.push_back((1u << 16) | op);
    return w;
  };
  auto fs_kill = module(4, 252), fs_demote = module(4, 5380), fs_term = module(4, 4416), fs_none = module(4, 0),
       vs_kill = module(0, 252);
  if (!spirv_fragment_discards(fs_kill.data(), fs_kill.size())) return fail(50, "OpKill");
  if (!spirv_fragment_discards(fs_demote.data(), fs_demote.size())) return fail(51, "OpDemoteToHelperInvocation");
  if (!spirv_fragment_discards(fs_term.data(), fs_term.size())) return fail(52, "OpTerminateInvocation");
  if (spirv_fragment_discards(fs_none.data(), fs_none.size())) return fail(53, "empty fragment shader");
  if (spirv_fragment_discards(vs_kill.data(), vs_kill.size())) return fail(54, "not a fragment shader");
  uint32_t junk[6] = {0x07230203u, 0, 0, 0, 0, 0};   // a zero-length instruction ends the scan
  if (spirv_fragment_discards(junk, 6)) return fail(55, "malformed module");

  std::printf("cascade_casters_test: ok\n");
  return 0;
}
