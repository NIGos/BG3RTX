// D-EM emissive policy in the hot-reloaded core (docs/TRACK_D_RT.md 13.21, ABI 24): the
// per-draw record and the rt_em_calib rule of common/emissive_policy.h, behind
// Bg3CoreApi::em_record / em_calib_update / em_calib_apply. A formula change needs only a
// core rebuild. The host (layer/emissive.cpp) classifies the draw, keeps the views it is
// handed back, allocates the luminance-copy slot and builds the scene.
// tests/emissive_core_test.cpp compares these records bit for bit with the former layer path.
// The same entry carries the M5.1 water policy (common/water_policy.h): forward ==
// BG3_EM_REQ_WATER returns a water::WaterRec (layer/water.cpp captures by its class).
// It also notes the command buffer of each supported water draw: core/water.cpp finds the
// forward pass that holds water by it (core_water_pass_take), whatever draws open or close it.
#include <atomic>
#include <cstring>
#include <mutex>
#include <vector>

#include "core.h"
#include "../common/emissive_policy.h"
#include "../common/water_policy.h"

namespace {

using namespace bg3::em;
static_assert(sizeof(Bg3EmRecordOut::rec) == sizeof(EmRec), "Bg3EmRecordOut::rec holds an EmRec");
static_assert(sizeof(Bg3EmRecordOut::rec) >= sizeof(bg3::water::WaterRec), "Bg3EmRecordOut::rec holds a WaterRec");
static_assert(sizeof(Bg3EmCalib) == sizeof(Calib) && offsetof(Bg3EmCalib, log_u) == offsetof(Calib, log_u),
              "Bg3EmCalib mirrors em::Calib");

// The em::make_record reader over the draw's current bindings (graphics bind point).
struct CoreReader {
  VkCommandBuffer cmd;
  Bg3EmRecordOut& out;
  bool read(uint32_t set, uint32_t binding, uint64_t offset, void* dst, size_t n) {
    return host->read_bound(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, set, binding, offset, dst, n);
  }
  bool view(EmView which, uint32_t set, uint32_t binding) {
    Bg3EmView& v = out.views[uint32_t(which)];
    Bg3Binding t;
    if (host->query_binding(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, set, binding, 0, &t) && t.view) v = {t.view, t.layout};
    else v = {VK_NULL_HANDLE, VK_IMAGE_LAYOUT_UNDEFINED};
    return v.view != VK_NULL_HANDLE;
  }
  void drop_view(EmView which) { out.views[uint32_t(which)] = {VK_NULL_HANDLE, VK_IMAGE_LAYOUT_UNDEFINED}; }
  // The slot itself is the host's (one per distinct binding in the pass): glob 0 here, the
  // host writes the slot it allocates for `luminance`.
  uint32_t luminance_slot() {
    Bg3Binding g{};
    if (!host->query_binding(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, 2, 0, 0, &g) || !g.buffer) return UINT32_MAX;
    out.lum_wanted = 1;
    out.luminance = g;
    return 0;
  }
  float lava_heat() { return host->cfg_float("rt_em_lava_heat", 0.5f); }
  bool fx_pulse() { return host->cfg_int && host->cfg_int("rt_em_fx_pulse", 0) != 0; }
};

Calib to_calib(const Bg3EmCalib& c) { return Calib{c.scale, c.updates, c.log_u}; }

// Command buffers whose open render pass recorded a supported water draw. Bounded: an entry
// leaves at that buffer's next pass begin or end; a buffer abandoned without one is dropped
// when the list is full (its only effect: one pass end of a reused handle looks at water).
// M5.4b: with rt_water_ls_shadows, the first water draw's set 2 light bindings ride along.
struct WaterPassNote {
  VkCommandBuffer cmd = VK_NULL_HANDLE;
  Bg3Binding lights[7] = {};
  bool lights_ok = false;
  Bg3Binding copy = {};   // M5.5: the draw's set 2 b4 (the colour copy its FS refracts through)
  bool copy_ok = false;
};
std::mutex g_wp_mu;
std::vector<WaterPassNote> g_wp;
std::atomic<uint32_t> g_wp_n{0};
constexpr size_t kWpMax = 64;

bool is_storage(VkDescriptorType t) {
  return t == VK_DESCRIPTOR_TYPE_STORAGE_BUFFER || t == VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC;
}
bool is_uniform(VkDescriptorType t) {
  return t == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER || t == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
}
// The FS's froxel light list as the D-LS structures (shaders/rt/bg3_lights.hlsli): b0 exposure float[],
// b10 froxels (ivec4), b11 index list (int), b12 lights (80 B), b26 constants (std140, 544 B), b27
// shadow views (96 B), b28 the shadow atlas.
bool capture_lights(VkCommandBuffer cmd, Bg3Binding* b) {
  for (uint32_t i = 0; i < 7; ++i)
    if (!host->query_binding(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, 2, kWaterLsBindings[i], 0, &b[i])) return false;
  for (uint32_t i : {0u, 1u, 2u, 3u, 5u})
    if (!is_storage(b[i].type) || !b[i].buffer || !b[i].range) return false;
  return is_uniform(b[4].type) && b[4].buffer && b[4].range >= 544 && b[3].range >= 80 &&
         b[6].type == VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE && b[6].view;
}

void water_draw_note(VkCommandBuffer cmd) {
  std::lock_guard<std::mutex> lock(g_wp_mu);
  for (const WaterPassNote& n : g_wp)
    if (n.cmd == cmd) return;
  if (g_wp.size() >= kWpMax) g_wp.erase(g_wp.begin());
  WaterPassNote n;
  n.cmd = cmd;
  if (host->cfg_int("rt_water_ls_shadows", 0) != 0) n.lights_ok = capture_lights(cmd, n.lights);
  if (host->cfg_int("rt_water_refraction", 0) != 0)
    n.copy_ok = host->query_binding(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, 2, 4, 0, &n.copy) &&
                n.copy.type == VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE && n.copy.view;
  g_wp.push_back(n);
  g_wp_n.store(uint32_t(g_wp.size()), std::memory_order_relaxed);
}

}  // namespace

bool core_water_pass_take(VkCommandBuffer cmd, Bg3Binding* lights, bool* lights_ok, Bg3Binding* copy, bool* copy_ok) {
  if (lights_ok) *lights_ok = false;
  if (copy_ok) *copy_ok = false;
  if (!g_wp_n.load(std::memory_order_relaxed)) return false;   // the common case: no lock
  std::lock_guard<std::mutex> lock(g_wp_mu);
  for (size_t i = 0; i < g_wp.size(); ++i)
    if (g_wp[i].cmd == cmd) {
      if (lights) std::memcpy(lights, g_wp[i].lights, sizeof g_wp[i].lights);
      if (lights_ok) *lights_ok = g_wp[i].lights_ok;
      if (copy) *copy = g_wp[i].copy;
      if (copy_ok) *copy_ok = g_wp[i].copy_ok;
      g_wp[i] = g_wp.back();
      g_wp.pop_back();
      g_wp_n.store(uint32_t(g_wp.size()), std::memory_order_relaxed);
      return true;
    }
  return false;
}

bool core_em_record(const Bg3EmRecordIn* in, Bg3EmRecordOut* out) {
  std::memset(out, 0, sizeof *out);
  if (!in || !in->info || !host->read_bound || !host->query_binding) return false;
  CoreReader rd{in->cmd, *out};
  if (in->forward == BG3_EM_REQ_WATER) {   // M5.1: the water class and record of a forward draw
    const bg3::water::WaterRec w = bg3::water::make_record(rd, in->fs, in->decal, in->info, in->name ? in->name : "");
    std::memcpy(out->rec, &w, sizeof w);
    if (w.cls == bg3::water::kSupported) water_draw_note(in->cmd);
    return true;
  }
  const EmMaterial mat{in->info, in->name ? in->name : "", in->forward != 0, in->decal != 0};
  EmRecordOut o;
  const EmRec r = make_record(rd, in->fs, mat, o);
  std::memcpy(out->rec, &r, sizeof r);
  std::memcpy(out->dbg_rec, &o.dbg_rec, sizeof o.dbg_rec);
  out->cb_unreadable = o.cb_unreadable;
  out->vt_missing = o.vt_missing;
  out->pulse_no_time = o.pulse_no_time;
  out->dbg_m = o.dbg_m;
  out->dbg_pulse = o.dbg_pulse;
  std::memcpy(out->m, o.m, sizeof o.m);
  std::memcpy(out->m45, o.m45, sizeof o.m45);
  out->time = o.time;
  out->pulse = o.pulse;
  return true;
}

// Set by core/emissive.cpp at each run of the check (here, so that emissive_core_test links
// this file alone); read on the host's thread at the calibration.
static std::atomic<uint32_t> g_check_slices{1};
void em_note_check_slices(uint32_t slices) { g_check_slices.store(slices ? slices : 1u, std::memory_order_relaxed); }
uint32_t em_check_slices() { return g_check_slices.load(std::memory_order_relaxed); }

bool core_em_calib_update(Bg3EmCalib* cal, uint32_t first, double ratio, uint32_t flags, uint64_t frames,
                          double bright) {
  Calib c = to_calib(*cal);
  // The host counts runs of the check, each over 1/slices of the pixels (rt_em_check_slices).
  const uint32_t slices = em_check_slices();
  const bool changed = first ? calib_first(c, ratio, flags, calib_checks(frames, slices), bright)
                             : calib_window(c, ratio, flags, calib_px_per_check(bright, slices));
  *cal = Bg3EmCalib{c.scale, c.updates, c.log_u};
  return changed;
}

void core_em_calib_apply(uint32_t* rec, const Bg3EmCalib* cal) {
  EmRec r;
  std::memcpy(&r, rec, sizeof r);
  Calib c{};
  if (cal) c = to_calib(*cal);
  calib_apply(r, cal ? &c : nullptr);
  std::memcpy(rec, &r, sizeof r);
}
