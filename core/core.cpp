// Hot-reloadable core. Rebuild this DLL while the game runs and the host swaps
// it in at the next present. Everything that must outlive a reload goes through
// host->persist; every GPU object goes back through host->defer_destroy.
//
// What it does today:
//  - a pass census: every distinct pass signature (first fragment shader +
//    attachment formats + size) with how often it ran, logged on request;
//  - lighting, GI and water through the callbacks below.
#include <cstdio>
#include <cstring>
#include <map>
#include <mutex>
#include <string>
#include <tuple>

#include "core.h"

const Bg3HostApi* host = nullptr;

// ---- pass census ----

struct PassKey {
  uint64_t fs;
  uint32_t w, h, colors;
  VkFormat c0, depth;
  bool operator<(const PassKey& o) const {
    return std::tie(fs, w, h, colors, c0, depth) < std::tie(o.fs, o.w, o.h, o.colors, o.c0, o.depth);
  }
};
struct PassStat {
  uint64_t count = 0;
  uint32_t draws = 0;
  uint64_t first_seen = 0;
};
static std::mutex g_census_mu;
static std::map<PassKey, PassStat> g_census;

static void census_dump() {
  std::lock_guard<std::mutex> lock(g_census_mu);
  host->log("pass census: %zu distinct signatures", g_census.size());
  for (auto& [k, s] : g_census)
    host->log("  fs %016llx  %4ux%-4u  colors %u (fmt %d) depth fmt %d  draws %u  runs %llu", (unsigned long long)k.fs,
              k.w, k.h, k.colors, k.c0, k.depth, s.draws, (unsigned long long)s.count);
}

// ---- callbacks ----

static void on_frame(uint64_t frame) {
  gi_on_frame(frame);   // the background pipeline builds' status lines
  int* census_req = static_cast<int*>(host->persist("core.census_request", sizeof(int)));
  int want = host->cfg_int("census_dump", 0);
  if (want != *census_req) {
    *census_req = want;
    if (want) census_dump();
  }
}

static void on_pass_end(VkCommandBuffer cmd, const Bg3PassInfo* p) {
  gi_on_pass_end(cmd, p);
  water_on_pass_end(cmd, p);
  PassKey k{p->first_fs_hash, p->width, p->height, p->color_count, p->color_count ? p->color_formats[0] : VK_FORMAT_UNDEFINED,
            p->depth_format};
  std::lock_guard<std::mutex> lock(g_census_mu);
  PassStat& s = g_census[k];
  if (!s.count) s.first_seen = host->frame_index();
  ++s.count;
  s.draws = p->draw_count;
}

static void on_pass_begin(VkCommandBuffer cmd, const Bg3PassInfo* p) {
  core_water_pass_take(cmd);   // a stale note (a pass end not seen) never names this pass
  gi_on_pass_begin(cmd, p);
}
static void on_dispatch(VkCommandBuffer cmd, const Bg3DispatchInfo* d) { gi_on_dispatch(cmd, d); }
static void on_cmd_end(VkCommandBuffer cmd) { gi_on_cmd_end(cmd); }
static void on_dispatch_pre(VkCommandBuffer cmd, const Bg3DispatchInfo* d) { gi_on_dispatch_pre(cmd, d); }

static void unload() {
  gi_release();
  em_release();
  water_release();
  host->log("core unloading");
}

static const Bg3CoreApi g_api = {BG3RTX_ABI_VERSION, &unload, &on_frame, &on_pass_end, &on_pass_begin, &on_dispatch, &on_cmd_end, &on_dispatch_pre,
                                  &core_em_record, &core_em_calib_update, &core_em_calib_apply};

extern "C" __declspec(dllexport) const Bg3CoreApi* bg3rtx_core_load(const Bg3HostApi* h) {
  host = h;
  uint32_t* loads = static_cast<uint32_t*>(host->persist("core.loads", sizeof(uint32_t)));
  ++*loads;
  host->log("core built %s %s loaded (load #%u)", __DATE__, __TIME__, *loads);
  return &g_api;
}
