// D-EM policy in the core (docs/TRACK_D_RT.md 13.21, ABI 24): the records of
// core/em_policy.cpp (Bg3CoreApi::em_record, over a fake host's read_bound and
// query_binding) against the former layer path (layer/emissive.cpp DrawReader +
// make_record before the move, copied below verbatim apart from the fake bindings), bit
// for bit, on captured-like inputs for every family: fixed cases and a fuzz over which
// constant buffers, ranges, mappings and views are bound and what the buffers hold
// (NaN, infinities, negatives included). Also the calibration entries against em::.
#include <windows.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include "../core/core.h"
#include "../common/emissive_policy.h"

const Bg3HostApi* host = nullptr;

namespace {

using namespace bg3::em;
unsigned checks;
#define CHECK(x) do{++checks;if(!(x)){std::printf("FAIL %d (case %u): %s\n",__LINE__,g_case,#x);std::exit(1);}}while(0)
unsigned g_case;

// ---- the fake game state: what is bound at (set, binding), the host mapping ----
struct FakeBind { Bg3Binding b{}; std::vector<uint8_t> mem; bool mapped = true; };
std::map<std::pair<uint32_t, uint32_t>, FakeBind> g_bind;
float g_heat = 0.5f;

bool query_binding(VkCommandBuffer, VkPipelineBindPoint bp, uint32_t set, uint32_t binding, uint32_t element,
                   Bg3Binding* out) {
  if (bp != VK_PIPELINE_BIND_POINT_GRAPHICS || element) return false;
  auto it = g_bind.find({set, binding});
  if (it == g_bind.end()) return false;
  *out = it->second.b;
  return true;
}
bool read_mapped(VkBuffer b, VkDeviceSize offset, void* dst, size_t n) {
  for (auto& [k, f] : g_bind)
    if (f.b.buffer == b) {
      if (!f.mapped) return false;
      if (offset + n > f.mem.size()) { std::printf("FAIL: read past the fake mapping\n"); std::exit(1); }
      std::memcpy(dst, f.mem.data() + offset, n);
      return true;
    }
  return false;
}
float cfg_float(const char* key, float fallback) { return std::strcmp(key, "rt_em_lava_heat") ? fallback : g_heat; }
int g_fx_pulse = 0;   // rt_em_fx_pulse
int cfg_int(const char* key, int fallback) { return std::strcmp(key, "rt_em_fx_pulse") ? fallback : g_fx_pulse; }

// ---- the former layer path (layer/emissive.cpp at 9d94ff3), test-only copy ----
namespace old {
constexpr uint32_t kGlobSlots = 64;
uint32_t g_glob_next = 0;
struct View { VkImageView v = VK_NULL_HANDLE; VkImageLayout l = VK_IMAGE_LAYOUT_UNDEFINED; };
struct Census { uint32_t global_conflicts = 0, vt_missing = 0, cb_unreadable = 0, pulse_no_time = 0; };
struct PDraw { EmRec rec; View mask, base, vt_ind, vt_cache; uint32_t pulse_no_time = 0; };
struct PCmd { Census census; std::vector<std::pair<Bg3Binding, uint32_t>> copies; };
struct RecDbg { EmRec r{}; float m[6] = {}; float m45[2] = {}; float time = 0, pulse = -1; bool cb = false; };
std::map<uint64_t, RecDbg> g_rec_dbg;
struct Mat { Bg3MaterialInfo info{}; std::string name; bool forward = false, decal = false; };

bool read_bound(VkCommandBuffer cmd, uint32_t set, uint32_t binding, VkDeviceSize extra, void* dst, size_t n) {
  Bg3Binding b;
  if (!query_binding(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, set, binding, 0, &b) || !b.buffer) return false;
  if (b.range != VK_WHOLE_SIZE && extra + n > b.range) return false;
  return read_mapped(b.buffer, b.offset + extra, dst, n);
}
View bound_view(VkCommandBuffer cmd, uint32_t set, uint32_t binding) {
  Bg3Binding t;
  if (query_binding(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, set, binding, 0, &t) && t.view) return View{t.view, t.layout};
  return View{};
}
struct DrawReader {
  VkCommandBuffer cmd;
  PCmd& P;
  PDraw& d;
  bool read(uint32_t set, uint32_t binding, uint64_t offset, void* dst, size_t n) {
    return read_bound(cmd, set, binding, VkDeviceSize(offset), dst, n);
  }
  View& slot(EmView which) {
    switch (which) {
      case EmView::Mask: return d.mask;
      case EmView::Base: return d.base;
      case EmView::VtIndirection: return d.vt_ind;
      default: return d.vt_cache;
    }
  }
  bool view(EmView which, uint32_t set, uint32_t binding) {
    View& v = slot(which);
    v = bound_view(cmd, set, binding);
    return v.v != VK_NULL_HANDLE;
  }
  void drop_view(EmView which) { slot(which) = View{}; }
  uint32_t luminance_slot() {
    Bg3Binding g{};
    if (!query_binding(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, 2, 0, 0, &g) || !g.buffer) return UINT32_MAX;
    uint32_t slot = UINT32_MAX;
    for (const auto& c : P.copies)
      if (c.first.buffer == g.buffer && c.first.offset == g.offset) slot = c.second;
    if (slot == UINT32_MAX) {
      slot = g_glob_next++ % kGlobSlots;
      P.copies.push_back({g, slot});
      if (P.copies.size() > 1) ++P.census.global_conflicts;
    }
    return slot;
  }
  float lava_heat() { return cfg_float("rt_em_lava_heat", 0.5f); }
  bool fx_pulse() { return cfg_int("rt_em_fx_pulse", 0) != 0; }   // (added with the 304be1c7 exclusion)
};
void make_record(VkCommandBuffer cmd, uint64_t fs, const Mat& mat, PCmd& P, PDraw& d) {
  DrawReader rd{cmd, P, d};
  EmMaterial m{&mat.info, mat.name, mat.forward, mat.decal};
  EmRecordOut o;
  d.rec = bg3::em::make_record(rd, fs, m, o);
  P.census.cb_unreadable += o.cb_unreadable;
  P.census.vt_missing += o.vt_missing;
  d.pulse_no_time += o.pulse_no_time;
  if (o.dbg_m) {
    RecDbg& g = g_rec_dbg[fs];
    std::memcpy(g.m, o.m, sizeof g.m);
    g.cb = true;
    if (o.dbg_pulse) {
      std::memcpy(g.m45, o.m45, sizeof g.m45);
      g.time = o.time;
      g.pulse = o.pulse;
    }
  }
  P.census.pulse_no_time += d.pulse_no_time;
  g_rec_dbg[fs].r = o.dbg_rec;
}
}  // namespace old

// ---- the new layer glue (layer/emissive.cpp make_record over Bg3CoreApi::em_record) ----
namespace neu {
using old::kGlobSlots;
uint32_t g_glob_next = 0;
std::map<uint64_t, old::RecDbg> g_rec_dbg;
bool make_record(const Bg3CoreApi* core, VkCommandBuffer cmd, uint64_t fs, const old::Mat& mat, old::PCmd& P,
                 old::PDraw& d) {
  const Bg3EmRecordIn in{cmd, fs, &mat.info, mat.name.c_str(), mat.forward, mat.decal};
  Bg3EmRecordOut o;
  if (!core->em_record(&in, &o)) return false;
  static_assert(sizeof o.rec == sizeof d.rec);
  std::memcpy(&d.rec, o.rec, sizeof d.rec);
  old::View* v[4] = {&d.mask, &d.base, &d.vt_ind, &d.vt_cache};
  for (int i = 0; i < 4; ++i) *v[i] = old::View{o.views[i].view, o.views[i].layout};
  old::RecDbg& g = g_rec_dbg[fs];
  std::memcpy(&g.r, o.dbg_rec, sizeof g.r);
  if (o.lum_wanted) {
    uint32_t slot = UINT32_MAX;
    for (const auto& c : P.copies)
      if (c.first.buffer == o.luminance.buffer && c.first.offset == o.luminance.offset) slot = c.second;
    if (slot == UINT32_MAX) {
      slot = g_glob_next++ % kGlobSlots;
      P.copies.push_back({o.luminance, slot});
      if (P.copies.size() > 1) ++P.census.global_conflicts;
    }
    d.rec.glob = g.r.glob = slot;
  }
  P.census.cb_unreadable += o.cb_unreadable;
  P.census.vt_missing += o.vt_missing;
  d.pulse_no_time += o.pulse_no_time;
  if (o.dbg_m) {
    std::memcpy(g.m, o.m, sizeof g.m);
    g.cb = true;
    if (o.dbg_pulse) {
      std::memcpy(g.m45, o.m45, sizeof g.m45);
      g.time = o.time;
      g.pulse = o.pulse;
    }
  }
  P.census.pulse_no_time += d.pulse_no_time;
  return true;
}
}  // namespace neu

// ---- inputs ----
uint64_t g_rng = 0x9e3779b97f4a7c15ull;
uint32_t rnd() { g_rng ^= g_rng << 13; g_rng ^= g_rng >> 7; g_rng ^= g_rng << 17; return uint32_t(g_rng >> 11); }
uint32_t rnd(uint32_t n) { return rnd() % n; }
float rnd_float() {
  static const float special[] = {0.0f, -0.0f, 1.0f, 0.5f, 0.99f, 1.0f, 1.5f, 2.0f, -1.0f, 12000.0f, 1e31f, -1e31f,
                                  INFINITY, -INFINITY, NAN, 3.0f};
  if (rnd(3) == 0) return special[rnd(16)];
  return (float(rnd(20001)) - 5000.0f) / 1000.0f;
}
uint64_t g_handle = 0x1000;
template <class T> T handle() { return reinterpret_cast<T>(uintptr_t(g_handle += 0x10)); }

void bind_cb(uint32_t set, uint32_t binding, const std::vector<float>& v, VkDeviceSize range = VK_WHOLE_SIZE,
             VkDeviceSize offset = 0, bool mapped = true) {
  FakeBind& f = g_bind[{set, binding}];
  f.b = Bg3Binding{};
  f.b.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
  f.b.buffer = handle<VkBuffer>();
  f.b.offset = offset;
  f.b.range = range;
  f.mapped = mapped;
  f.mem.assign(size_t(offset) + 4096, 0);
  std::memcpy(f.mem.data() + offset, v.data(), (std::min)(v.size() * 4, size_t(4096)));
}
void bind_view(uint32_t set, uint32_t binding, bool null_view = false) {
  FakeBind& f = g_bind[{set, binding}];
  f.b = Bg3Binding{};
  f.b.type = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
  f.b.view = null_view ? VK_NULL_HANDLE : handle<VkImageView>();
  f.b.layout = VkImageLayout(1 + rnd(5));
}
Bg3MatSlot cb_slot(uint8_t kind, uint8_t set, uint8_t binding, uint16_t offset, float d0 = 0, float d1 = 0, float d2 = 0) {
  Bg3MatSlot s{};
  s.kind = kind; s.set = set; s.binding = binding; s.offset = offset;
  s.value[0] = d0; s.value[1] = d1; s.value[2] = d2;
  return s;
}
Bg3MatSlot tex_slot(uint8_t set, uint8_t binding, uint8_t kind = BG3_MAT_KIND_TEXTURE) {
  Bg3MatSlot s{};
  s.kind = kind; s.set = set; s.binding = binding; s.offset = 0xFFFF;
  return s;
}

// One material per family the policy treats differently (common/emissive_policy.h).
struct Family { uint32_t fam; const char* name; Bg3MaterialInfo mi; bool forward = false, decal = false; };
std::vector<Family> families() {
  Bg3MaterialInfo g{};
  g.cls = BG3_MAT_CLASS_EMISSIVE;
  g.material_cb_set = 1; g.material_cb_binding = 0;
  g.slot[BG3_MAT_EMISSIVE_COLOR] = cb_slot(BG3_MAT_KIND_VEC3, 1, 0, 0, 0.3f, 0.2f, 0.1f);
  g.slot[BG3_MAT_EMISSIVE_INTENSITY] = cb_slot(BG3_MAT_KIND_SCALAR, 1, 0, 12, 2.f);
  g.slot[BG3_MAT_EMISSIVE_MAP] = tex_slot(2, 5);
  std::vector<Family> f;
  f.push_back({0x11223344u, "Base/Base_GM", g});
  Bg3MaterialInfo lum = g; lum.flags = BG3_MAT_F_LUMINANCE_EMISSIVE;
  f.push_back({0x11223345u, "Base/Base_GM_Lum", lum});
  Bg3MaterialInfo unlit = g; unlit.slot[BG3_MAT_EMISSIVE_COLOR] = {}; unlit.flags = BG3_MAT_F_UNLIT;
  unlit.slot[BG3_MAT_BASE_COLOR] = cb_slot(BG3_MAT_KIND_VEC4, 1, 0, 16, 0.7f, 0.6f, 0.5f);
  f.push_back({0x11223346u, "Unlit/Unlit", unlit});
  Bg3MaterialInfo nocol = g; nocol.slot[BG3_MAT_EMISSIVE_COLOR] = {};
  f.push_back({0x11223347u, "Base/NoColour", nocol});
  Bg3MaterialInfo bgm = nocol; bgm.slot[BG3_MAT_BASE_COLOR_MAP] = tex_slot(2, 1);
  f.push_back({0x11223348u, "Base/Base_BGM", bgm});
  Bg3MaterialInfo vt = nocol; vt.flags = BG3_MAT_F_VIRTUAL_TEXTURE | BG3_MAT_F_LUMINANCE_EMISSIVE;
  vt.slot[BG3_MAT_VT_UV_TRANSFORM] = cb_slot(BG3_MAT_KIND_VEC4, 1, 1, 0);
  vt.slot[BG3_MAT_VT_TILESET_INDEX] = cb_slot(BG3_MAT_KIND_SCALAR, 1, 1, 32);
  vt.slot[BG3_MAT_VT_TABLE] = tex_slot(0, 20, BG3_MAT_KIND_VT_TABLE);
  vt.slot[BG3_MAT_VT_ALBEDO] = tex_slot(0, 21, BG3_MAT_KIND_VT_CACHE);
  f.push_back({0x11223349u, "Base/Base_BGM_VT", vt});
  f.push_back({0x222986b8u, "Base/Base_GM_VT", g});
  Bg3MaterialInfo pulse = g; pulse.flags = BG3_MAT_F_LUMINANCE_EMISSIVE;
  pulse.slot[BG3_MAT_EMISSIVE_INTENSITY] = cb_slot(BG3_MAT_KIND_SCALAR, 1, 0, 28, 1.f);
  f.push_back({0x159a6211u, "Base/Base_GM_Pulse_VT", pulse});
  Bg3MaterialInfo fx = pulse; fx.flags = 0;
  f.push_back({0x304be1c7u, "FX/Base_GM_Pulse_FX", fx});
  Bg3MaterialInfo ab = g; ab.material_cb_binding = 1;
  ab.slot[BG3_MAT_EMISSIVE_COLOR] = cb_slot(BG3_MAT_KIND_VEC3, 1, 1, 0, 1, 1, 1);
  ab.slot[BG3_MAT_EMISSIVE_INTENSITY] = cb_slot(BG3_MAT_KIND_SCALAR, 1, 1, 24, 1.f);
  f.push_back({0xa201d825u, "FX/Base_GM_Pulse_AlphaBlend", ab});
  Bg3MaterialInfo badcb = pulse; badcb.material_cb_set = 9;
  f.push_back({0x159a6211u, "Base/Base_GM_Pulse_VT (bad cb)", badcb});
  Bg3MaterialInfo tile = bgm; tile.slot[BG3_MAT_BASE_COLOR_MAP] = tex_slot(2, 2);
  f.push_back({0xaab75fedu, "ENV/ENV_Tileset_MultipleTextures_BGM_MaskedShadows", tile});
  f.push_back({0x1a81f41bu, "FLUID/FLUID_Lava_Plane_Flowmap", g});
  f.push_back({0xf298bf4du, "ENV/ENV_Base_MutipleUV", g});
  Bg3MaterialInfo muv = g; muv.material_cb_set = 200;
  f.push_back({0xf298bf4eu, "ENV/ENV_Base_MultipleUV", muv});
  f.push_back({0x11223350u, "Base/Base_GM_VertCut", g});
  f.push_back({0x7123ab1du, "Base/Base_Fresnel_GM_VT", g});
  f.push_back({0x11223351u, "Forward/Base_GM_Fwd", g, true});
  Bg3MaterialInfo dec = g; dec.cls = BG3_MAT_CLASS_DECAL;
  f.push_back({0x11223352u, "Decal/Decal_GM", dec, false, true});
  return f;
}

// A random game state: each relevant binding present or not, its range and mapping, its
// contents; each view bound or null; the luminance buffer; the lava heat.
void random_state() {
  g_bind.clear();
  g_heat = rnd(4) ? 0.5f : rnd_float();
  g_fx_pulse = int(rnd(2));
  const std::pair<uint32_t, uint32_t> cbs[] = {{1, 0}, {1, 1}, {0, 0}};
  for (auto [s, b] : cbs) {
    if (rnd(5) == 0) continue;
    std::vector<float> v(64);
    for (float& x : v) x = rnd_float();
    if (s == 1 && b == 1 && rnd(2)) {   // a sane tile-set index at +32
      const uint32_t idx = rnd(4) ? rnd(8) : rnd();
      std::memcpy(&v[8], &idx, 4);
    }
    static const VkDeviceSize ranges[] = {4, 8, 16, 24, 28, 32, 36, 96, 128, 256};
    bind_cb(s, b, v, rnd(3) ? VK_WHOLE_SIZE : ranges[rnd(10)], rnd(2) ? 0 : 256, rnd(8) != 0);
  }
  if (rnd(4)) {   // the tile-set SSBO: 8 entries
    std::vector<float> v(8 * 32);
    for (float& x : v) x = rnd(6) ? float(rnd(1000)) / 7.0f : rnd_float();
    bind_cb(0, 12, v, rnd(4) ? VkDeviceSize(8 * 128) : VkDeviceSize(3 * 128));
  }
  const std::pair<uint32_t, uint32_t> views[] = {{2, 5}, {2, 1}, {2, 2}, {0, 11}, {0, 20}, {0, 21}};
  for (auto [s, b] : views)
    if (rnd(4)) bind_view(s, b, rnd(6) == 0);
  if (rnd(3)) {
    FakeBind& f = g_bind[{2, 0}];
    f.b = Bg3Binding{};
    f.b.type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    f.b.buffer = rnd(5) ? reinterpret_cast<VkBuffer>(uintptr_t(0x77 + rnd(3) * 16)) : VK_NULL_HANDLE;
    f.b.offset = rnd(2) * 64;
    f.b.range = 16;
  }
}

uint32_t g_seen_modes = 0, g_seen_flags = 0, g_seen_glob = 0, g_seen_conflicts = 0;
bool same_view(const old::View& a, const old::View& b) { return a.v == b.v && a.l == b.l; }

// Runs both paths on the current state for `n` draws of one command buffer (the luminance
// slots carry across draws of a pass) and compares everything the layer keeps.
void compare(const Bg3CoreApi* core, const Family& fam, uint64_t fs_lo, int draws = 1) {
  old::Mat mat{fam.mi, fam.name, fam.forward, fam.decal};
  const uint64_t fs = (uint64_t(fam.fam) << 32) | fs_lo;
  old::PCmd Po, Pn;
  old::g_rec_dbg.clear();
  neu::g_rec_dbg.clear();
  old::g_glob_next = neu::g_glob_next = rnd(100);
  for (int i = 0; i < draws; ++i) {
    old::PDraw a, b;
    old::make_record(VK_NULL_HANDLE, fs, mat, Po, a);
    CHECK(neu::make_record(core, VK_NULL_HANDLE, fs, mat, Pn, b));
    CHECK(std::memcmp(&a.rec, &b.rec, sizeof a.rec) == 0);
    CHECK(same_view(a.mask, b.mask) && same_view(a.base, b.base) && same_view(a.vt_ind, b.vt_ind) &&
          same_view(a.vt_cache, b.vt_cache));
    CHECK(a.pulse_no_time == b.pulse_no_time);
    g_seen_modes |= 1u << (a.rec.mode & 31);
    g_seen_flags |= a.rec.flags;
    g_seen_glob += a.rec.glob != UINT32_MAX;
    // The next draw of the pass may bind another luminance buffer range (a second slot).
    auto lum = g_bind.find({2, 0});
    if (lum != g_bind.end() && rnd(2)) lum->second.b.offset += 64;
  }
  g_seen_conflicts += Po.census.global_conflicts;
  CHECK(Po.copies.size() == Pn.copies.size());
  for (size_t i = 0; i < Po.copies.size(); ++i)
    CHECK(std::memcmp(&Po.copies[i].first, &Pn.copies[i].first, sizeof(Bg3Binding)) == 0 &&
          Po.copies[i].second == Pn.copies[i].second);
  CHECK(Po.census.cb_unreadable == Pn.census.cb_unreadable && Po.census.vt_missing == Pn.census.vt_missing &&
        Po.census.pulse_no_time == Pn.census.pulse_no_time && Po.census.global_conflicts == Pn.census.global_conflicts);
  CHECK(old::g_glob_next == neu::g_glob_next);
  const old::RecDbg &x = old::g_rec_dbg[fs], &y = neu::g_rec_dbg[fs];
  CHECK(std::memcmp(&x.r, &y.r, sizeof x.r) == 0);
  CHECK(std::memcmp(x.m, y.m, sizeof x.m) == 0 && std::memcmp(x.m45, y.m45, sizeof x.m45) == 0);
  CHECK(std::memcmp(&x.time, &y.time, 4) == 0 && std::memcmp(&x.pulse, &y.pulse, 4) == 0 && x.cb == y.cb);
}

}  // namespace

int main() {
  Bg3HostApi h{};
  h.abi_version = BG3RTX_ABI_VERSION;
  h.query_binding = &query_binding;
  h.cfg_float = &cfg_float;
  h.cfg_int = &cfg_int;
  h.read_bound = [](VkCommandBuffer cmd, VkPipelineBindPoint bp, uint32_t set, uint32_t binding, uint64_t offset,
                    void* dst, size_t n) {
    // layer/hotreload.cpp host_read_bound (the former emissive.cpp read_bound).
    Bg3Binding b;
    if (!query_binding(cmd, bp, set, binding, 0, &b) || !b.buffer) return false;
    if (b.range != VK_WHOLE_SIZE && offset + n > b.range) return false;
    return read_mapped(b.buffer, b.offset + offset, dst, n);
  };
  host = &h;
  Bg3CoreApi core{};
  core.abi_version = BG3RTX_ABI_VERSION;
  core.em_record = &core_em_record;
  core.em_calib_update = &core_em_calib_update;
  core.em_calib_apply = &core_em_calib_apply;

  const std::vector<Family> fams = families();
  // 1. Every family with everything readable and bound (304be1c7 excluded and captured).
  for (int fx = 0; fx < 2; ++fx)
  for (const Family& f : fams) {
    ++g_case;
    g_fx_pulse = fx;
    g_bind.clear();
    std::vector<float> m(64);
    for (int i = 0; i < 64; ++i) m[i] = 0.25f * float(i + 1);
    bind_cb(1, 0, m);
    std::vector<float> e(64, 1.0f);
    const uint32_t idx = 2;
    std::memcpy(&e[8], &idx, 4);
    bind_cb(1, 1, e);
    bind_cb(0, 0, {3.0f});
    std::vector<float> ss(8 * 32);
    for (size_t i = 0; i < ss.size(); ++i) ss[i] = float(i);
    bind_cb(0, 12, ss, 8 * 128);
    for (auto [s, b] : {std::pair{2u, 5u}, {2u, 1u}, {2u, 2u}, {0u, 11u}, {0u, 20u}, {0u, 21u}}) bind_view(s, b);
    g_bind[{2, 0}].b.buffer = reinterpret_cast<VkBuffer>(uintptr_t(0x77));
    compare(&core, f, 0xabcdef01u, 3);
  }
  // 2. Fuzz: random bindings and contents, every family, several draws per pass.
  unsigned per_family = 0;
  for (int it = 0; it < 4000; ++it)
    for (const Family& f : fams) {
      ++g_case;
      random_state();
      Family v = f;
      if (rnd(4) == 0) v.mi.flags ^= (rnd(2) ? BG3_MAT_F_LUMINANCE_EMISSIVE : BG3_MAT_F_UNLIT);
      compare(&core, v, rnd(), 1 + int(rnd(3)));
      ++per_family;
    }
  // The fuzz reached every mode, every record flag but Calibrated (the build's; Excluded included), shared
  // and conflicting luminance slots.
  std::printf("coverage: modes %x flags %x glob %u conflicts %u\n", g_seen_modes, g_seen_flags, g_seen_glob, g_seen_conflicts);
  CHECK(g_seen_modes == 0xFu && g_seen_flags == 0xBFu && g_seen_glob > 1000 && g_seen_conflicts > 100);
  // 3. Calibration through the core entries equals em:: directly.
  for (int it = 0; it < 20000; ++it) {
    ++g_case;
    Calib a{};
    if (rnd(2)) a = Calib{rnd_float(), rnd(3), double(rnd_float())};
    Bg3EmCalib b{a.scale, a.updates, a.log_u};
    const double ratio = rnd(5) ? std::exp((double(rnd(2001)) - 1000.0) / 200.0) : double(rnd_float());
    const uint32_t flags = rnd(128);
    const uint64_t frames = rnd(10);
    const double bright = double(rnd(4000));
    const bool first = rnd(2);
    const bool ra = first ? calib_first(a, ratio, flags, frames, bright) : calib_window(a, ratio, flags, bright);
    const bool rb = core.em_calib_update(&b, first, ratio, flags, frames, bright);
    CHECK(ra == rb && std::memcmp(&a.scale, &b.scale, 4) == 0 && a.updates == b.updates &&
          std::memcmp(&a.log_u, &b.log_u, 8) == 0);
    EmRec r{};
    for (float& c : r.radiance) c = rnd_float();
    r.flags = rnd(128);
    uint32_t words[64];
    std::memcpy(words, &r, sizeof words);
    const bool use = rnd(4) != 0;
    calib_apply(r, use ? &a : nullptr);
    core.em_calib_apply(words, use ? &b : nullptr);
    CHECK(std::memcmp(words, &r, sizeof r) == 0);
  }
  std::printf("emissive_core_test: %u checks passed (%zu families, %u fuzzed passes), records bit-identical\n", checks,
              fams.size(), per_family);
  return 0;
}
