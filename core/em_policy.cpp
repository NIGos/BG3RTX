// D-EM emissive policy in the hot-reloaded core (docs/TRACK_D_RT.md 13.21, ABI 24): the
// per-draw record and the rt_em_calib rule of common/emissive_policy.h, behind
// Bg3CoreApi::em_record / em_calib_update / em_calib_apply. A formula change needs only a
// core rebuild. The host (layer/emissive.cpp) classifies the draw, keeps the views it is
// handed back, allocates the luminance-copy slot and builds the scene.
// tests/emissive_core_test.cpp compares these records bit for bit with the former layer path.
// The same entry carries the M5.1 water policy (common/water_policy.h): forward ==
// BG3_EM_REQ_WATER returns a water::WaterRec (layer/water.cpp captures by its class).
#include <cstring>

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

}  // namespace

bool core_em_record(const Bg3EmRecordIn* in, Bg3EmRecordOut* out) {
  std::memset(out, 0, sizeof *out);
  if (!in || !in->info || !host->read_bound || !host->query_binding) return false;
  CoreReader rd{in->cmd, *out};
  if (in->forward == BG3_EM_REQ_WATER) {   // M5.1: the water class and record of a forward draw
    const bg3::water::WaterRec w = bg3::water::make_record(rd, in->fs, in->decal, in->info, in->name ? in->name : "");
    std::memcpy(out->rec, &w, sizeof w);
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

bool core_em_calib_update(Bg3EmCalib* cal, uint32_t first, double ratio, uint32_t flags, uint64_t frames,
                          double bright) {
  Calib c = to_calib(*cal);
  const bool changed = first ? calib_first(c, ratio, flags, frames, bright) : calib_window(c, ratio, flags, bright);
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
