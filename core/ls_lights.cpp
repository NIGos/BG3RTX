// D-LS per-light data: the upload side (core/ls_lights.h, docs/SE_BRIDGE.md).
#include "ls_lights.h"
#include "completion_lease.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <vector>

namespace {

constexpr uint32_t kRing = 8;   // frames in flight are far fewer
// Header: uint count, float3 camera (written by the match pass), uint generation of the records,
// uint force (the slot image is new: recompute every slot). rt_local_shadows_match.hlsl kKnownHeader.
constexpr VkDeviceSize kHeader = 32;
constexpr VkDeviceSize kSlotBytes = 65536;   // header + kLslMaxKnown records, 64 KiB aligned
static_assert(kHeader + kLslMaxKnown * kLslRecord <= kSlotBytes, "upload slot too small");

VkBuffer g_buf = VK_NULL_HANDLE;
VkDeviceMemory g_mem = VK_NULL_HANDLE;
uint8_t* g_map = nullptr;
Bg3Image g_slots{};
bool g_slots_init = false, g_failed = false, g_logged = false;
uint32_t g_ring = 0;
CoreCompletionLease g_lease[kRing];
bool g_busy_logged = false;
uint64_t g_calls = 0;
bool g_have_cam = false;
float g_cam[3] = {};
std::vector<uint8_t> g_records;   // the current list, kLslRecord bytes each
uint32_t g_count = 0, g_se_seq = ~0u, g_total = 0;
uint32_t g_gen = 1;   // changes whenever the uploaded records do (the match pass keeps unchanged slots)
uint32_t g_n_se = 0, g_n_db = 0, g_n_owner[5] = {};

// ---- join tolerances (docs/SE_BRIDGE.md "Join tolerances") ----
// The bridge's constants, as in layer/sebridge.h (the core does not include the layer):
constexpr double kSePeriodS = 0.25;      // Lua PERIOD_US: an export every 250 ms (the next tick after)
constexpr double kSePollS = 0.1;         // layer/sebridge.cpp reader: polls the export every 100 ms
constexpr float kSePosQuant = 0.5e-4f;   // positions written "%.4f"
constexpr float kSeRadiusQuant = 0.5e-3f;   // radius written "%.3f"
constexpr float kMoveSpeedMax = 8.0f;    // ls_dyn_speed default: layer/sebridge.h kSeMoveSpeedMax

// Float ulp at magnitude m: for m in [2^(e-1), 2^e), 2^(e-24).
float ulp(float m) {
  int e = 0;
  std::frexp(std::max(std::fabs(m), 1e-30f), &e);
  return std::ldexp(1.0f, e - 24);
}

// Lights that stay put (layer/sebridge.h se_static_tol): per coordinate 4 ulp at the largest
// coordinate (the light's, + ls_lights_radius for the camera the shader adds) - the game's
// camera-relative position and the shader's + camera (2), the bridge's parse (1), the
// engine's transform composition (1; measured 2.9 ulp, captures/assetdb_join_20261026-*) -
// + the export's %.4f rounding, sqrt(3) of that for the distance, + the authored jitter.
float static_tol(const float p[3], float reach, float move_amount) {
  float m = std::max(std::max(std::fabs(p[0]), std::fabs(p[1])), std::fabs(p[2])) + std::max(reach, 0.0f);
  return 1.7320508f * (4.0f * ulp(m) + kSePosQuant) + std::max(move_amount, 0.0f);
}

// Radius, lights that stay put: the export's %.3f + 4 relative ulp (the game's 1/r 0.5, the
// shader's 1/inv_r division 2.5 by Vulkan's OpFDiv precision, the parse 0.5, rounded up).
float static_radius_tol(float r) { return kSeRadiusQuant + 4.0f * ulp(r); }
// Radius, lights that move: not derived (a VFX light's radius may be animated between
// exports); the old 2% + 1 cm. "[se] join" logs the largest disagreement it accepts.
float moving_radius_tol(float r) { return 0.02f * std::max(r, 0.1f) + 0.01f; }

// Moving lights: displacement <= v_max x (age of the export when its records are last used).
// An export's records are in use from its arrival until the next arrival; its position was
// sampled age0 before its arrival (info.age_s there: the poll + the parse). The frame that
// uses them renders the game's lights of that frame, up to 2 frame times from the refresh
// (the refresh is at the frame's start; the game's render thread trails its simulation, on
// which the Lua's tick runs, by about a frame). So
//   T_age = max arrival interval + max age0 + 2 x max frame time,
// each the largest of the last kWin samples, before any sample the nominal kSePeriodS,
// kSePollS and 1/30 s.
constexpr int kWin = 16;   // 4 s of exports
struct WinMax {
  double v[kWin] = {};
  int n = 0, at = 0;
  void add(double x) {
    v[at] = x;
    at = (at + 1) % kWin;
    n = std::min(n + 1, kWin);
  }
  double max(double nominal) const {
    if (!n) return nominal;
    double m = 0;
    for (int i = 0; i < n; ++i) m = std::max(m, v[i]);
    return m;
  }
};
WinMax g_arrival, g_age0, g_frame;
std::chrono::steady_clock::time_point g_t_call{}, g_t_arrival{}, g_t_refresh{};
bool g_was_active = false;

double age_window_s() {
  return g_arrival.max(kSePeriodS) + g_age0.max(kSePollS) + 2.0 * g_frame.max(1.0 / 30.0);
}

const Bg3AssetDbApi* api() {
  const Bg3AssetDbApi* a = host->abi_version >= 6 ? host->assetdb : nullptr;
  return a && a->version >= 2 && a->struct_size >= sizeof(Bg3AssetDbApi) ? a : nullptr;
}

bool ensure() {
  if (g_buf && g_slots.image) return true;
  if (g_failed) return false;
  void* m = nullptr;
  if (!host->alloc_host_buffer || !host->alloc_host_buffer(kSlotBytes * kRing, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, &g_buf, &g_mem, &m) ||
      !host->alloc_image(kLslSlots, 3, VK_FORMAT_R32G32B32A32_UINT, VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT, &g_slots)) {
    host->log("ls: per-light data allocation failed; every light uses ls_source_radius");
    g_failed = true;
    lsl_release();
    g_failed = true;
    return false;
  }
  g_map = static_cast<uint8_t*>(m);
  // GPU-written camera position: NaN until the match pass has run in that slot.
  for (uint32_t i = 0; i < kRing; ++i) {
    float nan[4] = {0, NAN, NAN, NAN};
    memcpy(g_map + i * kSlotBytes, nan, sizeof nan);
  }
  g_slots_init = false;
  return true;
}

// Re-reads the runtime lights around the camera (or, before the first camera readback,
// the controlled character) into g_records.
void refresh(const Bg3AssetDbApi* a) {
  Bg3SeInfo info{};
  a->se_info(&info);
  float center[3];
  if (g_have_cam) {
    memcpy(center, g_cam, sizeof center);
  } else if (info.active) {
    memcpy(center, info.host, sizeof center);
  } else {
    if (!g_records.empty()) ++g_gen;
    g_records.clear();
    g_count = 0;
    return;
  }
  const float radius = host->cfg_float("ls_lights_radius", 80.0f);
  const float speed = std::max(host->cfg_float("ls_dyn_speed", kMoveSpeedMax), 0.0f);
  const float moving_tol = float(speed * age_window_s());
  static std::vector<Bg3RtLight> L(kLslMaxKnown);
  g_total = a->runtime_lights(center, radius, L.data(), kLslMaxKnown);
  g_count = g_total < kLslMaxKnown ? g_total : kLslMaxKnown;
  std::vector<uint8_t> prev;
  prev.swap(g_records);
  g_records.assign(size_t(g_count) * kLslRecord, 0);
  g_n_se = g_n_db = 0;
  memset(g_n_owner, 0, sizeof g_n_owner);
  for (uint32_t i = 0; i < g_count; ++i) {
    const Bg3RtLight& l = L[i];
    // Tolerances of the join (rt_local_shadows_match.hlsl reads both from the record).
    // Placed lights and the database's lights stay put: float precision and the export's
    // rounding. Every other light the bridge reports moves between its exports: character
    // lights, item lights, and the visual lights it could not attach to an item (owner
    // unknown or scenery), which include the VFX lights of carried weapons
    // (VFX_LightSources_WPN_DippedInFire, ..._BloodOfLathander: owner 0). Those get
    // ls_dyn_speed x T_age on top (age_window_s); the radius test and the nearest-wins search
    // keep static lamps on their own record.
    const bool moves = (l.flags & BG3_RTL_FROM_SE) && l.owner != BG3_RTL_OWNER_PLACED;
    const float tol = static_tol(l.pos, radius, l.move_amount) + (moves ? moving_tol : 0.0f);
    const float rtol = moves ? moving_radius_tol(l.radius) : static_radius_tol(l.radius);
    float f[8] = {l.pos[0], l.pos[1], l.pos[2], l.radius, l.source_radius, tol, l.intensity, rtol};
    uint32_t u[4] = {l.flags, uint32_t(l.owner) | (uint32_t(l.type) << 8) | (uint32_t(l.channel) << 16), l.db_index, i};
    memcpy(&g_records[size_t(i) * kLslRecord], f, 32);
    memcpy(&g_records[size_t(i) * kLslRecord + 32], u, 16);
    g_n_se += (l.flags & BG3_RTL_FROM_SE) ? 1u : 0u;
    g_n_db += (l.flags & BG3_RTL_FROM_DB) ? 1u : 0u;
    ++g_n_owner[l.owner < 5 ? l.owner : 0];
  }
  if (g_records != prev) ++g_gen;
  if (g_count && (!g_logged || g_calls % 1200 < 8)) {
    g_logged = true;
    log_stats("ls: per-light data: %u runtime lights within %.0f m of (%.1f, %.1f, %.1f) [%s]: bridge %u, database %u; "
              "owners placed %u, item %u, scenery %u, character %u, unknown %u; bridge %s (export %u, level %s); "
              "moving-light tolerance %.2f m = %.1f m/s x %.3f s (export interval %.3f + age at arrival %.3f + 2 "
              "frames of %.4f s)",
              g_count, radius, center[0], center[1], center[2], g_have_cam ? "camera" : "character", g_n_se, g_n_db,
              g_n_owner[1], g_n_owner[2], g_n_owner[3], g_n_owner[4], g_n_owner[0], info.active ? "active" : "off",
              info.seq, info.level ? info.level : "", moving_tol, speed, age_window_s(), g_arrival.max(kSePeriodS),
              g_age0.max(kSePollS), g_frame.max(1.0 / 30.0));
  }
}

}  // namespace

bool lsl_begin(VkCommandBuffer cmd, LslFrame* f) {
  const Bg3AssetDbApi* a = api();
  if (!a || !host->cfg_int("ls_lights", 1) || !ensure()) return false;
  const uint32_t next = (g_ring + 1) % kRing;
  if (!g_lease[next].reusable()) {
    if (!g_busy_logged) host->log("ls: upload slot still owned by recording/GPU; update skipped"), g_busy_logged = true;
    return false;
  }
  g_ring = next;
  const bool readable = g_lease[g_ring].readable();
  g_lease[g_ring].clear();
  g_lease[g_ring].record(cmd);
  uint8_t* slot = g_map + size_t(g_ring) * kSlotBytes;
  // The camera position the match pass wrote into this slot kRing frames ago.
  float cam[4];
  if (readable) memcpy(cam, slot, sizeof cam);
  else cam[1] = cam[2] = cam[3] = NAN;
  if (std::isfinite(cam[1]) && std::isfinite(cam[2]) && std::isfinite(cam[3])) {
    memcpy(g_cam, cam + 1, sizeof g_cam);
    g_have_cam = true;
  }
  Bg3SeInfo info{};
  a->se_info(&info);
  const auto now = std::chrono::steady_clock::now();
  auto secs = [&](std::chrono::steady_clock::time_point t) { return std::chrono::duration<double>(now - t).count(); };
  const bool first = g_calls++ == 0;
  if (!first) g_frame.add(secs(g_t_call));
  g_t_call = now;
  const bool arrival = info.active && info.seq != g_se_seq;
  if (arrival) {
    // Intervals only between exports of a running bridge (one that was fresh in the previous
    // frame too): a pause or a level load is not an export period.
    if (g_was_active && g_se_seq != ~0u) g_arrival.add(secs(g_t_arrival));
    g_age0.add(std::max(double(info.age_s), 0.0));
    g_t_arrival = now;
  }
  g_was_active = info.active != 0;
  // Refresh on every export, and otherwise once per export period (kSePeriodS: nothing the
  // bridge reports changes faster; the camera-centred query and the database-only case
  // follow at the same rate). It was every 8 frames: 4 times per export at 120 fps.
  if (first || arrival || secs(g_t_refresh) >= kSePeriodS) {
    g_se_seq = info.active ? info.seq : ~0u;
    g_t_refresh = now;
    refresh(a);
  }
  float head[4] = {0, NAN, NAN, NAN};
  uint32_t n = g_count;
  memcpy(head, &n, 4);
  memcpy(slot, head, sizeof head);
  const uint32_t tail[4] = {g_gen, g_slots_init ? 0u : 1u, 0u, 0u};
  memcpy(slot + 16, tail, sizeof tail);
  if (g_count) memcpy(slot + kHeader, g_records.data(), g_records.size());
  f->known = {g_buf, VkDeviceSize(g_ring) * kSlotBytes, kSlotBytes};
  f->slots_image = g_slots.image;
  f->slots_view = g_slots.view;
  f->needs_init = !g_slots_init;
  f->count = g_count;
  return true;
}

void lsl_initialized() { g_slots_init = true; }

void lsl_release() {
  if (g_buf) {
    host->defer_destroy(BG3_DEFER_BUFFER, (uint64_t)g_buf);
    host->defer_destroy(BG3_DEFER_MEMORY, (uint64_t)g_mem);
  }
  if (g_slots.image) {
    host->defer_destroy(BG3_DEFER_IMAGE_VIEW, (uint64_t)g_slots.view);
    host->defer_destroy(BG3_DEFER_IMAGE, (uint64_t)g_slots.image);
    host->defer_destroy(BG3_DEFER_MEMORY, (uint64_t)g_slots.memory);
  }
  g_buf = VK_NULL_HANDLE;
  g_mem = VK_NULL_HANDLE;
  g_map = nullptr;
  g_slots = Bg3Image{};
  for (auto& lease : g_lease) lease.clear();
  g_busy_logged = false;
  g_slots_init = g_have_cam = g_logged = g_failed = false;
  g_records.clear();
  g_count = 0;
  g_se_seq = ~0u;
  g_t_refresh = {};   // the next lsl_begin refreshes
}
