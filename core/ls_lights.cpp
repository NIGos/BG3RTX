// D-LS per-light data: the upload side (core/ls_lights.h, docs/SE_BRIDGE.md).
#include "ls_lights.h"

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
uint64_t g_calls = 0;
bool g_have_cam = false;
float g_cam[3] = {};
std::vector<uint8_t> g_records;   // the current list, kLslRecord bytes each
uint32_t g_count = 0, g_se_seq = ~0u, g_total = 0;
uint32_t g_gen = 1;   // changes whenever the uploaded records do (the match pass keeps unchanged slots)
uint32_t g_n_se = 0, g_n_db = 0, g_n_owner[5] = {};

const Bg3AssetDbApi* api() {
  const Bg3AssetDbApi* a = host->abi_version >= 6 ? host->assetdb : nullptr;
  return a && a->version >= 2 && a->struct_size >= sizeof(Bg3AssetDbApi) ? a : nullptr;
}

bool ensure() {
  if (g_buf && g_slots.image) return true;
  if (g_failed) return false;
  void* m = nullptr;
  if (!host->alloc_host_buffer || !host->alloc_host_buffer(kSlotBytes * kRing, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, &g_buf, &g_mem, &m) ||
      !host->alloc_image(kLslSlots, 3, VK_FORMAT_R32G32B32A32_UINT, VK_IMAGE_USAGE_STORAGE_BIT, &g_slots)) {
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
    // Position tolerance of the join: lights that stay put match to float precision;
    // item and character lights move between the bridge's 4 Hz exports.
    float tol = 0.05f + l.move_amount;
    if (l.flags & BG3_RTL_FROM_SE) {
      if (l.owner == BG3_RTL_OWNER_CHARACTER) tol = 4.0f;
      else if (l.owner == BG3_RTL_OWNER_ITEM) tol = 0.5f + l.move_amount;
    }
    float f[8] = {l.pos[0], l.pos[1], l.pos[2], l.radius, l.source_radius, tol, l.intensity, 0.0f};
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
    host->log("ls: per-light data: %u runtime lights within %.0f m of (%.1f, %.1f, %.1f) [%s]: bridge %u, database %u; "
              "owners placed %u, item %u, scenery %u, character %u, unknown %u; bridge %s (export %u, level %s)",
              g_count, radius, center[0], center[1], center[2], g_have_cam ? "camera" : "character", g_n_se, g_n_db,
              g_n_owner[1], g_n_owner[2], g_n_owner[3], g_n_owner[4], g_n_owner[0], info.active ? "active" : "off",
              info.seq, info.level ? info.level : "");
  }
}

}  // namespace

bool lsl_begin(LslFrame* f) {
  const Bg3AssetDbApi* a = api();
  if (!a || !host->cfg_int("ls_lights", 1) || !ensure()) return false;
  g_ring = (g_ring + 1) % kRing;
  uint8_t* slot = g_map + size_t(g_ring) * kSlotBytes;
  // The camera position the match pass wrote into this slot kRing frames ago.
  float cam[4];
  memcpy(cam, slot, sizeof cam);
  if (std::isfinite(cam[1]) && std::isfinite(cam[2]) && std::isfinite(cam[3])) {
    memcpy(g_cam, cam + 1, sizeof g_cam);
    g_have_cam = true;
  }
  Bg3SeInfo info{};
  a->se_info(&info);
  if (g_calls++ % 8 == 0 || (info.active && info.seq != g_se_seq)) {
    g_se_seq = info.active ? info.seq : ~0u;
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
  g_slots_init = g_have_cam = g_logged = g_failed = false;
  g_records.clear();
  g_count = 0;
  g_se_seq = ~0u;
}
