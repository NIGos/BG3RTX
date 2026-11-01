#pragma once
// D-EM emissive POLICY (docs/TRACK_D_RT.md 13): the per-draw record (radiance
// formula per material family) and the rt_em_calib calibration rule. Pure
// functions: no Vulkan, no globals. They run in the hot-reloaded core (core/em_policy.cpp,
// Bg3CoreApi::em_record, ABI 24), whose Reader reads the draw's bound constant buffers and
// views through the host; the layer (layer/emissive.cpp) keeps every Vulkan object, the
// views and the luminance-copy bookkeeping, and uses only the EmRec layout and flags.
// tests/emissive_policy_test.cpp pins the records on captured-like inputs;
// tests/emissive_core_test.cpp the core path against the former layer path, bit for bit.
//
// Reader concept (all members required):
//   bool read(uint32_t set, uint32_t binding, uint64_t offset, void* dst, size_t n);
//   bool view(EmView which, uint32_t set, uint32_t binding);  // binds it; true if non-null
//   void drop_view(EmView which);
//   uint32_t luminance_slot();   // set 2 b0 copy slot, UINT32_MAX when not bound
//   float lava_heat();           // rt_em_lava_heat
//   bool fx_pulse();             // rt_em_fx_pulse: capture family 304be1c7 (off: excluded)
#include "assetdb_api.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string_view>

namespace bg3::em {

// Must match shaders/rt/em_common.hlsli.
struct EmRec {
  float radiance[3];
  uint32_t mode;
  uint32_t tex, flags, fs_lo, fs_hi;
  float uv[4];
  uint32_t tex2, vt_tex, vt_cache, draw;
  float uv2[4];
  float vt_p0[4], vt_p1[4];
  float vt_t[32];
  uint32_t cls, glob, pad[2];   // glob: luminance-scale slot in g_globals (UINT32_MAX: none)
};
static_assert(sizeof(EmRec) == 256, "EmRec must match em_common.hlsli");
enum : uint32_t { kModeConst = 0, kModeMaskR = 1, kModeMaskRGB = 2, kModeBlackbody = 3 };
enum : uint32_t { kFlagLuminance = 1, kFlagApprox = 2, kFlagPulse = 4, kFlagBaseMap = 8, kFlagVtAlbedo = 16,
                  kFlagForward = 32, kFlagCalibrated = 64, kFlagExcluded = 128 };
enum class EmView : uint8_t { Mask, Base, VtIndirection, VtCache };

// The material as the layer classified it (asset DB info, template name).
struct EmMaterial {
  const Bg3MaterialInfo* info = nullptr;
  std::string_view name;
  bool forward = false;   // kMatForward
  bool decal = false;     // kMatDecal
};
// Side outputs of one record: census counters and the diagnostics the layer logs.
struct EmRecordOut {
  uint32_t cb_unreadable = 0, vt_missing = 0, pulse_no_time = 0;
  uint32_t excluded = 0;    // the draw's family is excluded (kFlagExcluded): not captured
  bool dbg_m = false;       // m (and cb = true) were read: store them
  bool dbg_pulse = false;   // m45, time and pulse too
  float m[6] = {}, m45[2] = {}, time = 0, pulse = -1;
  EmRec dbg_rec{};          // the record before the non-finite clamp
};

inline bool finite(const float* f, size_t n) {
  for (size_t i = 0; i < n; ++i)
    if (!(f[i] > -1e30f && f[i] < 1e30f)) return false;
  return true;
}
inline bool contains(std::string_view s, std::string_view part) { return s.find(part) != std::string_view::npos; }

// Families whose radiance is more than the generic colour x intensity x glow map (x base
// colour for BGM), decompiled from the captures (offsets in the material CB, set 1 b0):
//   222986b8 Base_GM_VT: mix(c * glow.r, glow.rgb, floor(m1)) * m2
//   159a6211 Base_GM_Pulse_VT: mix(c * glow.r, glow.rgb, floor(m4)) * m5
//            * mix(m2, m3, cos(time * m1) * 0.5 + 0.5) * global[0]   (time: set 0 b0 [0])
//   aab75fed ENV_Tileset_MultipleTextures_BGM_MaskedShadows:
//            mix(mix(b2(uv m0), b3(uv m1), vc.x), b4(uv m2), vc.y).rgb * glow.r * m3 * (1 - max(vc.x, vc.y))
//            The vertex colour vc is not captured: vc = 0 (the base map b2 at uv * m0) is taken,
//            the upper bound of the game's value (flagged approximate).
//   GM_Pulse (159a6211 _VT, 304be1c7 _FX, their aliases): the same constant buffer; only
//            the luminance-emissive ones (_VT) multiply by the global scale.
//   1a81f41b FLUID_Lava_Plane_Flowmap: blackbody(T) * m23 * (1, 2.5, 0), the LUT at set 0
//            b11 at u = (m0 + (m1 - m0) * h * m22) / 12000, h a heat field from flow noise,
//            vertex colour and distance. h is not reproduced: a constant rt_em_lava_heat
//            (0.5) is taken and the scale is left to the on-screen calibration.
//   The Pulse templates differ in where their constants are: 159a6211 _VT / 304be1c7 _FX at
//   set 1 b0 with the texture-colour switch at +24 and the intensity at +28; a201d825
//   _AlphaBlend at set 1 b1, intensity at +24 and no switch. The pulse terms are +12/+16/+20
//   of the material CB in all of them: the DB's material CB binding and intensity offset
//   say which layout a shader has.
template <class Reader>
void family_fixup(Reader& rd, uint64_t fs, const EmMaterial& mat, EmRec& r, EmRecordOut& o) {
  const uint32_t fam = uint32_t(fs >> 32);
  const std::string_view name = mat.name;
  const Bg3MaterialInfo& mi = *mat.info;
  const uint32_t cbs = mi.material_cb_set, cbb = mi.material_cb_binding;
  const bool cb_ok = cbs < 8 && cbb < 64;
  float m[6] = {};
  const bool pulse = fam == 0x159a6211u || contains(name, "_GM_Pulse");
  if (fam == 0x1a81f41bu) {
    float lv[24] = {};
    if (rd.read(1, 0, 0, lv, 96) && finite(lv, 24)) {
      const bool lut = rd.view(EmView::Mask, 0, 11);   // the blackbody LUT
      r.mode = lut ? kModeBlackbody : kModeConst;
      r.uv[0] = lv[0] / 12000.0f;
      r.uv[1] = (lv[1] - lv[0]) / 12000.0f * lv[22];
      r.uv[2] = rd.lava_heat();
      r.uv[3] = 0.0f;
      r.radiance[0] = lv[23];
      r.radiance[1] = 2.5f * lv[23];
      r.radiance[2] = 0.0f;
    }
    r.flags |= kFlagApprox;
    return;
  }
  if (fam == 0x222986b8u && rd.read(1, 0, 0, m, 24)) {
    std::memcpy(o.m, m, sizeof m);
    o.dbg_m = true;
    if (std::floor(m[3]) >= 1.0f) {
      r.mode = kModeMaskRGB;
      r.radiance[0] = r.radiance[1] = r.radiance[2] = m[4];
    }
  } else if (pulse && cb_ok && rd.read(cbs, cbb, 0, m, 24)) {
    float m45[2] = {}, time = 0.0f;
    const bool has_switch = mi.slot[BG3_MAT_EMISSIVE_INTENSITY].offset == 28;
    if (has_switch && rd.read(cbs, cbb, 24, m45, 8) && std::floor(m45[0]) >= 1.0f) {
      r.mode = kModeMaskRGB;
      for (float& c : r.radiance) c = m45[1];
    }
    // The pulse at the draw's time (set 0 b0 [0]); when that constant buffer is not
    // host-readable, its time average (m2 + m3) / 2: without it the radiance missed the
    // factor altogether (a201d825 read a stable 1.41).
    float p = 0.5f * (m[4] + m[5]);
    if (rd.read(0, 0, 0, &time, 4) && std::isfinite(time)) {
      p = m[4] + (m[5] - m[4]) * (std::cos(time * m[3]) * 0.5f + 0.5f);
    } else {
      ++o.pulse_no_time;
    }
    std::memcpy(o.m, m, sizeof m);
    std::memcpy(o.m45, m45, sizeof m45);
    o.time = time;
    o.pulse = p;
    o.dbg_m = o.dbg_pulse = true;
    if (std::isfinite(p) && p >= 0.0f) {
      for (float& c : r.radiance) c *= p;
      r.flags |= kFlagPulse;
    }
  } else if (contains(name, "MutipleUV") || contains(name, "MultipleUV")) {
    // f298bf4d ENV_Base_MutipleUV: glow(uv1).rgb * noise(scrolled uv0) * 1.5 * m2 * global. The
    // glow map reads the second UV, which XFB does not capture: the map is dropped (its mean
    // and the noise's (0.5 x 1.5) go to the calibration).
    // Its intensity is the material CB's +16 (_m2; the DB's slot read 0: em8 dump), its
    // colour the glow map alone.
    float k2 = 0.0f;
    if (rd.read(cb_ok ? cbs : 1, cb_ok ? cbb : 0, 16, &k2, 4) && std::isfinite(k2) && k2 >= 0.0f)
      for (float& c : r.radiance) c = 0.75f * k2;
    else
      for (float& c : r.radiance) c *= 0.75f;
    r.mode = kModeConst;
    rd.drop_view(EmView::Mask);
    r.flags |= kFlagApprox;
  } else if (fam == 0xaab75fedu && rd.read(1, 0, 0, m, 4) && std::isfinite(m[0])) {
    r.uv2[0] = r.uv2[1] = m[0];
    r.flags |= kFlagApprox;   // vertex colour
  }
}

// Families left out of the capture until their exact gate passes (TRACK_D 13.22):
//   304be1c7 Base_GM_Pulse_FX: its record reproduces the fragment shader term for term
//   (captures/alpha_cache/304be1c71df194cb.glsl: mix(c * glow.r, glow.rgb, floor(m4)) * m5 *
//   mix(m2, m3, cos(time * m1) * 0.5 + 0.5), no global scale, blend off, depth EQUAL), and
//   it passes in Grymforge (0.999), yet Adamantine Forge reads 0.36 on its bright pixels with
//   no captured emitter overlapping it. Until the AF cause is measured (em_forge live batch)
//   its draws are not captured: rt_em_fx_pulse=1 captures it again (the A/B).
inline constexpr uint32_t kFamPulseFx = 0x304be1c7u;
template <class Reader>
bool excluded_family(Reader& rd, uint64_t fs) {
  return uint32_t(fs >> 32) == kFamPulseFx && !rd.fx_pulse();
}

// The record of an emissive draw: radiance from the asset DB roles read from the
// game's constant buffers at record time, then the family fixups. An excluded family
// gets an empty record flagged kFlagExcluded (radiance 0, nothing read or bound): the
// layer counts the draw and does not capture it.
template <class Reader>
EmRec make_record(Reader& rd, uint64_t fs, const EmMaterial& mat, EmRecordOut& o) {
  const Bg3MaterialInfo& mi = *mat.info;
  EmRec r{};
  if (excluded_family(rd, fs)) {
    r.fs_lo = uint32_t(fs);
    r.fs_hi = uint32_t(fs >> 32);
    r.cls = mi.cls;
    r.glob = UINT32_MAX;
    r.flags = kFlagExcluded;
    o.excluded = 1;
    o.dbg_rec = r;
    return r;
  }
  r.radiance[0] = r.radiance[1] = r.radiance[2] = 1.0f;
  r.uv[0] = r.uv[1] = 1.0f;
  r.uv2[0] = r.uv2[1] = 1.0f;
  r.fs_lo = uint32_t(fs);
  r.fs_hi = uint32_t(fs >> 32);
  r.cls = mi.cls;
  auto slot = [&](const Bg3MatSlot& s, void* dst, size_t n) {
    return s.kind >= BG3_MAT_KIND_SCALAR && s.offset != 0xFFFF && rd.read(s.set, s.binding, s.offset, dst, n);
  };
  const Bg3MatSlot& col = mi.slot[BG3_MAT_EMISSIVE_COLOR];
  const Bg3MatSlot& base = mi.slot[BG3_MAT_BASE_COLOR];
  const bool bgm = contains(mat.name, "BGM");   // base colour x glow map templates
  float c[3] = {1, 1, 1};
  if (col.kind == BG3_MAT_KIND_VEC3 || col.kind == BG3_MAT_KIND_VEC4) {
    if (!slot(col, c, 12)) std::memcpy(c, col.value, 12), r.flags |= kFlagApprox, ++o.cb_unreadable;
  } else if ((mi.flags & BG3_MAT_F_UNLIT) && (base.kind == BG3_MAT_KIND_VEC3 || base.kind == BG3_MAT_KIND_VEC4)) {
    if (!slot(base, c, 12)) std::memcpy(c, base.value, 12), r.flags |= kFlagApprox;
  } else if (!bgm) {
    r.flags |= kFlagApprox;   // the colour comes from elsewhere
  }
  float k = 1.0f;
  const Bg3MatSlot& in = mi.slot[BG3_MAT_EMISSIVE_INTENSITY];
  if (in.kind == BG3_MAT_KIND_SCALAR && !slot(in, &k, 4)) k = in.value[0], r.flags |= kFlagApprox, ++o.cb_unreadable;
  for (int i = 0; i < 3; ++i) r.radiance[i] = c[i] * k;
  const Bg3MatSlot& map = mi.slot[BG3_MAT_EMISSIVE_MAP];
  if (map.kind == BG3_MAT_KIND_TEXTURE) {
    if (rd.view(EmView::Mask, map.set, map.binding)) r.mode = kModeMaskR;
    else r.flags |= kFlagApprox;
  }
  // The game's luminance scale (set 2 b0 [0], a storage buffer in device memory). The game
  // rewrites that buffer later in the same batch, so the value is copied on the GPU right
  // after this draw's pass (em_pass_done, em_copy.hlsl) into a slot of our own buffer.
  r.glob = UINT32_MAX;
  if (mi.flags & BG3_MAT_F_LUMINANCE_EMISSIVE) {
    r.flags |= kFlagLuminance;
    const uint32_t g = rd.luminance_slot();
    if (g != UINT32_MAX) r.glob = g;
    else r.flags |= kFlagApprox;
  }
  if (bgm) {
    if (mi.flags & BG3_MAT_F_VIRTUAL_TEXTURE) {
      // The VT albedo: uv transform and tile-set index from the engine CB, the tile-set
      // constants from the SSBO at set 0 b12, indirection and cache from the DB's slots.
      const Bg3MatSlot& xf = mi.slot[BG3_MAT_VT_UV_TRANSFORM];
      const Bg3MatSlot& ix = mi.slot[BG3_MAT_VT_TILESET_INDEX];
      const Bg3MatSlot& tb = mi.slot[BG3_MAT_VT_TABLE];
      const Bg3MatSlot& al = mi.slot[BG3_MAT_VT_ALBEDO];
      float p[8] = {};
      uint32_t idx = 0;
      bool ok = xf.kind && ix.kind && tb.kind && al.kind && slot(xf, p, 32) && slot(ix, &idx, 4) && idx < (1u << 20) &&
                rd.read(0, 12, uint64_t(idx) * 128, r.vt_t, 128) && finite(p, 8) && finite(r.vt_t, 32);
      if (ok) {
        std::memcpy(r.vt_p0, p, 16);
        std::memcpy(r.vt_p1, p + 4, 16);
        const bool ind = rd.view(EmView::VtIndirection, tb.set, tb.binding);
        const bool cache = rd.view(EmView::VtCache, al.set, al.binding);
        ok = ind && cache;
      }
      if (ok) r.flags |= kFlagVtAlbedo;
      else ++o.vt_missing, r.flags |= kFlagApprox;
    } else {
      const Bg3MatSlot& bm = mi.slot[BG3_MAT_BASE_COLOR_MAP];
      const bool have = bm.kind == BG3_MAT_KIND_TEXTURE && rd.view(EmView::Base, bm.set, bm.binding);
      if (have) r.flags |= kFlagBaseMap;
      else r.flags |= kFlagApprox;
    }
  }
  family_fixup(rd, fs, mat, r, o);
  if (mat.forward) r.flags |= kFlagForward | kFlagApprox;
  // E: vertex-colour-cut templates (VertCut) gate their emission by a vertex-colour mask that
  // is not captured; decals are box volumes, not the surfaces they light.
  // Fresnel templates (7123ab1d Base_Fresnel_GM_VT) scale their emission by a view-angle
  // term that the records do not model.
  if (contains(mat.name, "VertCut") || contains(mat.name, "Fresnel") || mat.decal) r.flags |= kFlagApprox;
  o.dbg_rec = r;
  if (!finite(r.radiance, 3) || r.radiance[0] < 0 || r.radiance[1] < 0 || r.radiance[2] < 0) {
    r.radiance[0] = r.radiance[1] = r.radiance[2] = 0.0f;
    r.flags |= kFlagApprox;
  }
  return r;
}

// ---- rt_em_calib: per-family calibration against the game's effects target ----
// For families whose formula is not fully reproduced (kFlagApprox), the radiance is scaled
// by game/ours as the projected check measures it on screen (damped, clamped to [1/64, 64]);
// exact families are never scaled (their ratio is the validation). Forward-pass families
// are not in the effects target and are not calibrated.
struct Calib { float scale = 1.0f; uint32_t updates = 0; double log_u = 0.0; };
inline constexpr double kCalibMinRatio = 0.02, kCalibMaxRatio = 50.0;
inline constexpr double kCalibBrightPx = 200.0;   // bright hits per frame that gate a family
inline constexpr uint32_t kCalibFirstFrames = 4;  // checks before the first calibration
inline float calib_clamp(double log_u) { return float((std::min)((std::max)(std::exp(-log_u), 1.0 / 64), 64.0)); }
inline bool calib_family(uint32_t flags) { return (flags & kFlagApprox) && !(flags & kFlagForward); }
inline float calib_scale(const Calib& cal) { return cal.updates ? cal.scale : 1.0f; }
// A family never calibrated is calibrated as soon as 4 checks have seen it on enough
// bright pixels, not at the end of its first window. ratio = ours / game, bright = the
// summed bright hits. True when it set the scale (the caller restarts the family's sums).
inline bool calib_first(Calib& cal, double ratio, uint32_t flags, uint64_t frames, double bright) {
  if (cal.updates || !calib_family(flags) || frames < kCalibFirstFrames || !(bright >= kCalibBrightPx * kCalibFirstFrames) ||
      !(ratio > kCalibMinRatio && ratio < kCalibMaxRatio))
    return false;
  cal.log_u = std::log(ratio);
  cal.scale = calib_clamp(cal.log_u);
  cal.updates = 1;
  return true;
}
// End of a window: the family's own ratio at scale 1 is ratio / scale. The scale is 1 / the
// exponential average of that ratio in log space (weight 1/2; the first window sets it).
// px = bright hits per frame. True when it updated.
inline bool calib_window(Calib& cal, double ratio, uint32_t flags, double px) {
  if (!calib_family(flags) || !(px >= kCalibBrightPx) || !(ratio > kCalibMinRatio && ratio < kCalibMaxRatio)) return false;
  const double lu = std::log(ratio / double(calib_scale(cal)));
  cal.log_u = cal.updates ? cal.log_u + 0.5 * (lu - cal.log_u) : lu;
  cal.scale = calib_clamp(cal.log_u);
  ++cal.updates;
  return true;
}
// rt_em_check_slices (core/emissive.cpp): a run of the projected check covers 1/slices of the
// pixels, so the host counts runs and bright hits per run; `slices` runs are one full-screen
// check, the unit of the gates above (kCalibFirstFrames checks, kCalibBrightPx per check).
inline uint64_t calib_checks(uint64_t runs, uint32_t slices) { return runs / (slices ? slices : 1u); }
inline double calib_px_per_check(double px_per_run, uint32_t slices) { return px_per_run * double(slices ? slices : 1u); }
// At the build: an approximate record of a calibrated family is scaled.
inline void calib_apply(EmRec& r, const Calib* cal) {
  if (!(r.flags & kFlagApprox) || !cal || !cal->updates) return;
  for (float& c : r.radiance) c *= cal->scale;
  r.flags |= kFlagCalibrated;
}

}  // namespace bg3::em
