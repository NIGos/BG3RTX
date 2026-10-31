// Pins the D-EM emissive records and calibration (common/emissive_policy.h) on
// captured-like inputs: material CB bytes, bound views and luminance slots as
// layer/emissive.cpp hands them over. Expected values are the formulas of the
// layer before the move, evaluated by hand.
#include "../common/emissive_policy.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <set>
#include <utility>
#include <vector>

using namespace bg3::em;
static unsigned checks;
#define CHECK(x) do{++checks;if(!(x)){std::printf("FAIL %d: %s\n",__LINE__,#x);std::exit(1);}}while(0)
static bool near(float a, float b) { return std::fabs(a - b) <= 1e-6f * (std::fabs(b) > 1 ? std::fabs(b) : 1.f); }

struct Fake {
  std::map<std::pair<uint32_t, uint32_t>, std::vector<uint8_t>> cb;
  std::set<std::pair<uint32_t, uint32_t>> views;
  std::map<EmView, std::pair<uint32_t, uint32_t>> bound;
  uint32_t lum = UINT32_MAX;
  float heat = 0.5f;
  bool pulse_fx = false;   // rt_em_fx_pulse
  bool read(uint32_t set, uint32_t binding, uint64_t offset, void* dst, size_t n) {
    auto it = cb.find({set, binding});
    if (it == cb.end() || offset + n > it->second.size()) return false;
    std::memcpy(dst, it->second.data() + offset, n);
    return true;
  }
  bool view(EmView w, uint32_t set, uint32_t binding) {
    if (!views.count({set, binding})) { bound.erase(w); return false; }
    bound[w] = {set, binding};
    return true;
  }
  void drop_view(EmView w) { bound.erase(w); }
  uint32_t luminance_slot() { return lum; }
  float lava_heat() { return heat; }
  bool fx_pulse() { return pulse_fx; }
  void floats(uint32_t set, uint32_t binding, std::vector<float> v) {
    auto& b = cb[{set, binding}];
    b.resize(v.size() * 4);
    std::memcpy(b.data(), v.data(), b.size());
  }
};
static Bg3MatSlot cb_slot(uint8_t kind, uint8_t set, uint8_t binding, uint16_t offset, float d0 = 0, float d1 = 0,
                          float d2 = 0) {
  Bg3MatSlot s{};
  s.kind = kind; s.set = set; s.binding = binding; s.offset = offset;
  s.value[0] = d0; s.value[1] = d1; s.value[2] = d2;
  return s;
}
static Bg3MatSlot tex_slot(uint8_t set, uint8_t binding) {
  Bg3MatSlot s{};
  s.kind = BG3_MAT_KIND_TEXTURE; s.set = set; s.binding = binding; s.offset = 0xFFFF;
  return s;
}
static Bg3MaterialInfo generic_info() {
  Bg3MaterialInfo mi{};
  mi.cls = BG3_MAT_CLASS_EMISSIVE;
  mi.material_cb_set = 1; mi.material_cb_binding = 0;
  mi.slot[BG3_MAT_EMISSIVE_COLOR] = cb_slot(BG3_MAT_KIND_VEC3, 1, 0, 0, 0.3f, 0.2f, 0.1f);
  mi.slot[BG3_MAT_EMISSIVE_INTENSITY] = cb_slot(BG3_MAT_KIND_SCALAR, 1, 0, 12, 2.f);
  mi.slot[BG3_MAT_EMISSIVE_MAP] = tex_slot(2, 5);
  return mi;
}
static EmRec run(Fake& f, uint64_t fs, const Bg3MaterialInfo& mi, const char* name, EmRecordOut& o, bool forward = false,
                 bool decal = false) {
  EmMaterial m{&mi, name, forward, decal};
  return make_record(f, fs, m, o);
}

int main() {
  const uint64_t fs_generic = 0x11223344aabbccddull;
  // 1. Generic: colour x intensity x glow map, all readable.
  {
    Fake f; f.floats(1, 0, {2, 1, 0.5f, 4}); f.views.insert({2, 5});
    auto mi = generic_info(); EmRecordOut o;
    EmRec r = run(f, fs_generic, mi, "Base/Base_GM", o);
    CHECK(near(r.radiance[0], 8) && near(r.radiance[1], 4) && near(r.radiance[2], 2));
    CHECK(r.mode == kModeMaskR && r.flags == 0 && r.glob == UINT32_MAX);
    CHECK(r.fs_lo == 0xaabbccddu && r.fs_hi == 0x11223344u && r.cls == BG3_MAT_CLASS_EMISSIVE);
    CHECK(r.uv[0] == 1 && r.uv[1] == 1 && r.uv[2] == 0 && r.uv2[0] == 1 && r.uv2[1] == 1);
    CHECK(f.bound.count(EmView::Mask) && o.cb_unreadable == 0 && !o.dbg_m);
  }
  // 2. CB unreadable, map unbound: template defaults, approximate.
  {
    Fake f; auto mi = generic_info(); EmRecordOut o;
    EmRec r = run(f, fs_generic, mi, "Base/Base_GM", o);
    CHECK(near(r.radiance[0], 0.6f) && near(r.radiance[1], 0.4f) && near(r.radiance[2], 0.2f));
    CHECK(r.mode == kModeConst && r.flags == kFlagApprox && o.cb_unreadable == 2);
  }
  // 3. Unlit base colour; no colour role and not BGM.
  {
    Fake f; f.floats(1, 0, {0.5f, 0.5f, 0.5f, 3});
    auto mi = generic_info(); mi.slot[BG3_MAT_EMISSIVE_COLOR] = {}; mi.slot[BG3_MAT_EMISSIVE_MAP] = {};
    mi.flags = BG3_MAT_F_UNLIT; mi.slot[BG3_MAT_BASE_COLOR] = cb_slot(BG3_MAT_KIND_VEC4, 1, 0, 0);
    EmRecordOut o; EmRec r = run(f, fs_generic, mi, "Unlit", o);
    CHECK(near(r.radiance[0], 1.5f) && r.flags == 0);
    mi.flags = 0; EmRecordOut o2; r = run(f, fs_generic, mi, "Unlit", o2);
    CHECK(near(r.radiance[0], 3) && r.flags == kFlagApprox);
  }
  // 4. Luminance-emissive: copy slot, or approximate without the binding.
  {
    Fake f; f.floats(1, 0, {1, 1, 1, 1}); f.views.insert({2, 5}); f.lum = 7;
    auto mi = generic_info(); mi.flags = BG3_MAT_F_LUMINANCE_EMISSIVE; EmRecordOut o;
    EmRec r = run(f, fs_generic, mi, "Lum", o);
    CHECK(r.glob == 7 && r.flags == kFlagLuminance);
    f.lum = UINT32_MAX; EmRecordOut o2; r = run(f, fs_generic, mi, "Lum", o2);
    CHECK(r.glob == UINT32_MAX && r.flags == (kFlagLuminance | kFlagApprox));
  }
  // 5. BGM with a base-colour map; and without it.
  {
    Fake f; f.floats(1, 0, {1, 1, 1, 2}); f.views.insert({2, 5}); f.views.insert({2, 1});
    auto mi = generic_info(); mi.slot[BG3_MAT_EMISSIVE_COLOR] = {}; mi.slot[BG3_MAT_BASE_COLOR_MAP] = tex_slot(2, 1);
    EmRecordOut o; EmRec r = run(f, fs_generic, mi, "Base_BGM", o);
    CHECK(near(r.radiance[0], 2) && r.flags == kFlagBaseMap && f.bound.count(EmView::Base));
    f.views.erase({2, 1}); EmRecordOut o2; r = run(f, fs_generic, mi, "Base_BGM", o2);
    CHECK(r.flags == kFlagApprox);
  }
  // 6. BGM virtual texture: uv transform, tile-set index, SSBO entry, indirection + cache.
  {
    Fake f; f.floats(1, 0, {1, 1, 1, 1}); f.views.insert({2, 5});
    std::vector<float> eng(9); for (int i = 0; i < 8; ++i) eng[i] = float(i + 1);
    uint32_t idx = 3; std::memcpy(&eng[8], &idx, 4); f.floats(1, 1, eng);
    std::vector<float> ssbo(4 * 32); for (size_t i = 0; i < ssbo.size(); ++i) ssbo[i] = float(i);
    f.floats(0, 12, ssbo); f.views.insert({0, 20}); f.views.insert({0, 21});
    auto mi = generic_info(); mi.flags = BG3_MAT_F_VIRTUAL_TEXTURE;
    mi.slot[BG3_MAT_VT_UV_TRANSFORM] = cb_slot(BG3_MAT_KIND_VEC4, 1, 1, 0);
    mi.slot[BG3_MAT_VT_TILESET_INDEX] = cb_slot(BG3_MAT_KIND_SCALAR, 1, 1, 32);
    mi.slot[BG3_MAT_VT_TABLE] = tex_slot(0, 20); mi.slot[BG3_MAT_VT_TABLE].kind = BG3_MAT_KIND_VT_TABLE;
    mi.slot[BG3_MAT_VT_ALBEDO] = tex_slot(0, 21); mi.slot[BG3_MAT_VT_ALBEDO].kind = BG3_MAT_KIND_VT_CACHE;
    EmRecordOut o; EmRec r = run(f, fs_generic, mi, "Base_BGM_VT", o);
    CHECK(r.flags == kFlagVtAlbedo && o.vt_missing == 0);
    CHECK(r.vt_p0[0] == 1 && r.vt_p0[3] == 4 && r.vt_p1[0] == 5 && r.vt_p1[3] == 8);
    CHECK(r.vt_t[0] == 96 && r.vt_t[31] == 127);
    f.views.erase({0, 21}); EmRecordOut o2; r = run(f, fs_generic, mi, "Base_BGM_VT", o2);
    CHECK(r.flags == kFlagApprox && o2.vt_missing == 1);
  }
  // 7. Lava flowmap (1a81f41b): blackbody LUT coordinates and radiance, always approximate.
  {
    Fake f; std::vector<float> lv(24, 0); lv[0] = 1200; lv[1] = 7200; lv[22] = 0.5f; lv[23] = 3;
    f.floats(1, 0, lv); f.views.insert({0, 11}); f.heat = 0.25f;
    auto mi = generic_info(); mi.slot[BG3_MAT_EMISSIVE_MAP] = {};
    EmRecordOut o; EmRec r = run(f, 0x1a81f41b00000001ull, mi, "FLUID_Lava_Plane_Flowmap", o);
    CHECK(r.mode == kModeBlackbody && r.flags == kFlagApprox);
    CHECK(near(r.uv[0], 0.1f) && near(r.uv[1], 0.25f) && r.uv[2] == 0.25f && r.uv[3] == 0);
    CHECK(r.radiance[0] == 3 && r.radiance[1] == 7.5f && r.radiance[2] == 0);
    f.views.clear(); EmRecordOut o2; r = run(f, 0x1a81f41b00000001ull, mi, "FLUID_Lava_Plane_Flowmap", o2);
    CHECK(r.mode == kModeConst);
  }
  // 8. Base_GM_VT (222986b8): texture-colour switch and intensity m4.
  {
    Fake f; f.floats(1, 0, {0, 0, 0, 1.5f, 6, 0}); f.views.insert({2, 5});
    auto mi = generic_info(); mi.slot[BG3_MAT_EMISSIVE_COLOR] = {}; mi.slot[BG3_MAT_EMISSIVE_INTENSITY] = {};
    EmRecordOut o; EmRec r = run(f, 0x222986b800000000ull, mi, "Base_GM_VT", o);
    CHECK(r.mode == kModeMaskRGB && r.radiance[0] == 6 && r.radiance[2] == 6 && o.dbg_m && !o.dbg_pulse);
    CHECK(o.m[3] == 1.5f && o.m[4] == 6);
  }
  // 9. GM_Pulse (159a6211): switch at +24, intensity at +28, pulse at the draw's time.
  {
    Fake f; f.floats(1, 0, {0, 0, 0, 2, 1, 3, 1, 5}); f.floats(0, 0, {0}); f.views.insert({2, 5});
    auto mi = generic_info(); mi.slot[BG3_MAT_EMISSIVE_COLOR] = {};
    mi.slot[BG3_MAT_EMISSIVE_INTENSITY] = cb_slot(BG3_MAT_KIND_SCALAR, 1, 0, 28);
    EmRecordOut o; EmRec r = run(f, 0x159a621100000000ull, mi, "Base_GM_Pulse_VT", o);
    CHECK(r.mode == kModeMaskRGB && near(r.radiance[0], 15) && r.flags == (kFlagApprox | kFlagPulse));
    CHECK(o.dbg_pulse && o.pulse == 3 && o.time == 0 && o.m45[1] == 5 && o.pulse_no_time == 0);
    f.floats(0, 0, {1.0f}); EmRecordOut ot; r = run(f, 0x159a621100000000ull, mi, "Base_GM_Pulse_VT", ot);
    CHECK(near(ot.pulse, 1 + 2 * (std::cos(2.f) * 0.5f + 0.5f)) && near(r.radiance[0], 5 * ot.pulse));
    f.cb.erase(std::make_pair(0u, 0u)); EmRecordOut o2; r = run(f, 0x159a621100000000ull, mi, "Base_GM_Pulse_VT", o2);
    CHECK(near(r.radiance[0], 10) && o2.pulse_no_time == 1 && o2.pulse == 2);
    // a201d825 _AlphaBlend by name: CB at set 1 b1, intensity +24, no switch.
    Fake g; g.floats(1, 1, {0, 0, 0, 0, 2, 2, 4}); g.floats(0, 0, {0});
    auto ab = generic_info(); ab.slot[BG3_MAT_EMISSIVE_COLOR] = {}; ab.slot[BG3_MAT_EMISSIVE_MAP] = {};
    ab.material_cb_binding = 1; ab.slot[BG3_MAT_EMISSIVE_INTENSITY] = cb_slot(BG3_MAT_KIND_SCALAR, 1, 1, 24);
    EmRecordOut o3; r = run(g, 0xa201d82500000000ull, ab, "FX_GM_Pulse_AlphaBlend", o3);
    CHECK(r.mode == kModeConst && near(r.radiance[0], 8) && (r.flags & kFlagPulse));
  }
  // 9b. 304be1c7 Base_GM_Pulse_FX, the asset DB's layout (tools.assets shader 304be1c71df194cb:
  // GlowColour s1b0+0, GlowMultiplier s1b0+28, Glowmap s1b1, not luminance-emissive) and its
  // fragment shader (captures/alpha_cache/304be1c71df194cb.glsl):
  //   mix(m0 * glow.r, glow.rgb, clamp(floor(m4))) * m5 * mix(m2, m3, cos(time * m1) * 0.5 + 0.5)
  // Default rt_em_fx_pulse=0: excluded (empty record, nothing read or bound).
  {
    auto fx = generic_info();
    fx.slot[BG3_MAT_EMISSIVE_INTENSITY] = cb_slot(BG3_MAT_KIND_SCALAR, 1, 0, 28, 1.f);
    fx.slot[BG3_MAT_EMISSIVE_MAP] = tex_slot(1, 1);
    const uint64_t fs = 0x304be1c71df194cbull;
    // CB words: colour (0.8, 0.4, 0.2), m1 2 (rad/s), m2 0.5, m3 1.5, m4 0 (switch off), m5 4.
    Fake f; f.floats(1, 0, {0.8f, 0.4f, 0.2f, 2, 0.5f, 1.5f, 0, 4}); f.floats(0, 0, {1.0f}); f.views.insert({1, 1});
    EmRecordOut ox; EmRec r = run(f, fs, fx, "Base/Base_GM_Pulse_FX", ox);
    CHECK(r.flags == kFlagExcluded && ox.excluded == 1 && r.radiance[0] == 0 && r.radiance[1] == 0 && r.radiance[2] == 0);
    CHECK(r.mode == kModeConst && r.tex == 0 && r.glob == UINT32_MAX && r.fs_hi == 0x304be1c7u && !ox.dbg_m && f.bound.empty());
    // A different FS of the same template is not excluded.
    EmRecordOut oo; CHECK(!(run(f, 0x159a621100000000ull, fx, "Base/Base_GM_Pulse_FX", oo).flags & kFlagExcluded));
    // rt_em_fx_pulse=1: the exact record (no approx, no luminance), pulse at time 1.
    f.pulse_fx = true; f.bound.clear();
    const float pulse = 0.5f + (1.5f - 0.5f) * (std::cos(1.0f * 2.0f) * 0.5f + 0.5f);
    EmRecordOut o; r = run(f, fs, fx, "Base/Base_GM_Pulse_FX", o);
    CHECK(r.mode == kModeMaskR && r.flags == kFlagPulse && o.excluded == 0 && r.glob == UINT32_MAX);
    CHECK(near(o.pulse, pulse) && near(r.radiance[0], 0.8f * 4 * pulse) && near(r.radiance[1], 0.4f * 4 * pulse) &&
          near(r.radiance[2], 0.2f * 4 * pulse));
    CHECK(f.bound.count(EmView::Mask) && f.bound[EmView::Mask] == std::make_pair(1u, 1u));
    // Switch on (m4 = 1): glow.rgb x m5 x pulse.
    f.floats(1, 0, {0.8f, 0.4f, 0.2f, 2, 0.5f, 1.5f, 1, 4}); EmRecordOut o2; r = run(f, fs, fx, "Base/Base_GM_Pulse_FX", o2);
    CHECK(r.mode == kModeMaskRGB && near(r.radiance[0], 4 * pulse) && near(r.radiance[2], 4 * pulse) && r.flags == kFlagPulse);
    // The same CB under 159a6211 _VT gives the same record, times the global scale (luminance slot).
    auto vt = fx; vt.flags = BG3_MAT_F_LUMINANCE_EMISSIVE; f.lum = 3;
    EmRecordOut o3; EmRec rv = run(f, 0x159a6211a89e8abdull, vt, "Base/Base_GM_Pulse_VT", o3);
    CHECK(rv.radiance[0] == r.radiance[0] && rv.mode == r.mode && rv.flags == (kFlagPulse | kFlagLuminance) && rv.glob == 3);
  }
  // 10. ENV_Base_MutipleUV: intensity +16 x 0.75, map dropped; else radiance x 0.75.
  {
    Fake f; f.floats(1, 0, {1, 1, 1, 4, 2}); f.views.insert({2, 5});
    auto mi = generic_info(); EmRecordOut o;
    EmRec r = run(f, 0xf298bf4d00000000ull, mi, "ENV_Base_MutipleUV", o);
    CHECK(r.mode == kModeConst && near(r.radiance[0], 1.5f) && r.flags == kFlagApprox && !f.bound.count(EmView::Mask));
    f.floats(1, 0, {1, 1, 1, 4}); EmRecordOut o2; r = run(f, 0xf298bf4d00000000ull, mi, "ENV_Base_MultipleUV", o2);
    CHECK(near(r.radiance[0], 3));
  }
  // 11. aab75fed tileset BGM: uv scale m0, vertex colour approximate.
  {
    Fake f; f.floats(1, 0, {4, 1, 1, 1}); f.views.insert({2, 5});
    auto mi = generic_info(); mi.slot[BG3_MAT_EMISSIVE_COLOR] = {};
    EmRecordOut o; EmRec r = run(f, 0xaab75fed00000000ull, mi, "ENV_Tileset_MultipleTextures_BGM_MaskedShadows", o);
    CHECK(r.uv2[0] == 4 && r.uv2[1] == 4 && (r.flags & kFlagApprox));
  }
  // 12. VertCut / Fresnel / decal / forward flags.
  {
    Fake f; f.floats(1, 0, {1, 1, 1, 1}); f.views.insert({2, 5}); auto mi = generic_info();
    EmRecordOut o; CHECK(run(f, fs_generic, mi, "Base_VertCut", o).flags == kFlagApprox);
    CHECK(run(f, fs_generic, mi, "Base_Fresnel_GM_VT", o).flags == kFlagApprox);
    CHECK(run(f, fs_generic, mi, "Decal", o, false, true).flags == kFlagApprox);
    CHECK(run(f, fs_generic, mi, "ENV_Lava", o, true).flags == (kFlagForward | kFlagApprox));
  }
  // 13. Negative or non-finite radiance is cleared; the diagnostics keep the raw record.
  {
    Fake f; f.floats(1, 0, {1, 1, 1, -2}); f.views.insert({2, 5}); auto mi = generic_info();
    EmRecordOut o; EmRec r = run(f, fs_generic, mi, "Base", o);
    CHECK(r.radiance[0] == 0 && r.radiance[2] == 0 && r.flags == kFlagApprox && o.dbg_rec.radiance[0] == -2);
    f.floats(1, 0, {1, 1, 1, 1e31f}); EmRecordOut o2; r = run(f, fs_generic, mi, "Base", o2);
    CHECK(r.radiance[0] == 0 && (r.flags & kFlagApprox));
  }
  // Calibration.
  {
    Calib c;
    CHECK(!calib_first(c, 2.0, 0, 4, 800));                        // exact: never scaled
    CHECK(!calib_first(c, 2.0, kFlagApprox | kFlagForward, 4, 800));
    CHECK(!calib_first(c, 2.0, kFlagApprox, 3, 800));
    CHECK(!calib_first(c, 2.0, kFlagApprox, 4, 799));
    CHECK(!calib_first(c, 0.02, kFlagApprox, 4, 800) && !calib_first(c, 50.0, kFlagApprox, 4, 800));
    CHECK(calib_scale(c) == 1.0f && c.updates == 0);
    CHECK(calib_first(c, 2.0, kFlagApprox, 4, 800) && c.updates == 1 && near(c.scale, 0.5f));
    CHECK(!calib_first(c, 4.0, kFlagApprox, 4, 800) && near(c.scale, 0.5f));   // only once
    CHECK(calib_window(c, 1.0, kFlagApprox, 200) && c.updates == 2 && near(c.scale, 0.5f));
    CHECK(calib_window(c, 4.0, kFlagApprox, 250) && c.updates == 3 && near(c.scale, 0.25f));
    CHECK(!calib_window(c, 4.0, kFlagApprox, 199) && !calib_window(c, 60.0, kFlagApprox, 300) && c.updates == 3);
    Calib w;
    CHECK(calib_window(w, 4.0, kFlagApprox, 300) && near(w.scale, 0.25f) && w.updates == 1);   // first window sets it
    CHECK(calib_clamp(std::log(100.0)) == float(1.0 / 64) && calib_clamp(-std::log(100.0)) == 64.0f);
    EmRec r{}; r.radiance[0] = r.radiance[1] = r.radiance[2] = 2; r.flags = kFlagApprox;
    calib_apply(r, nullptr); CHECK(r.radiance[0] == 2 && r.flags == kFlagApprox);
    Calib none; calib_apply(r, &none); CHECK(r.radiance[0] == 2);
    calib_apply(r, &w); CHECK(near(r.radiance[0], 0.5f) && r.flags == (kFlagApprox | kFlagCalibrated));
    EmRec exact{}; exact.radiance[0] = 2; calib_apply(exact, &w); CHECK(exact.radiance[0] == 2 && exact.flags == 0);
  }
  std::printf("emissive_policy_test: %u checks passed\n", checks);
  return 0;
}
