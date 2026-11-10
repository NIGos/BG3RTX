#include "host.h"
#include "hair_groom.h"
#include "assetdb.h"
#include "../common/hair_groom_v3.h"
#include "../common/hair_groom_lzms.h"
#include <windows.h>
#include <cstring>
#include <mutex>
#include <set>
#include <thread>
#include <unordered_map>
#include <unordered_set>

namespace bg3::hair_groom {
namespace {
std::mutex g_mu;
bool g_requested = false, g_suppressed = false, g_enabled = false;
VkPhysicalDeviceRayTracingLinearSweptSpheresFeaturesNV g_feature{};

const VkBaseInStructure* find_struct(const void* next, VkStructureType type) {
  for (auto p = static_cast<const VkBaseInStructure*>(next); p; p = p->pNext)
    if (p->sType == type) return p;
  return nullptr;
}

bool g_loaded = false;
std::unordered_map<uint64_t, std::unique_ptr<Loaded>> g_assets;
std::unordered_set<uint32_t> g_counts;

std::string groom_dir() {
  std::string db = assetdb_file();
  const size_t s = db.find_last_of("\\/");
  return (s == std::string::npos ? std::string(".") : db.substr(0, s)) + "\\hair_grooms";
}

std::unordered_set<std::string> g_paths;   // every file looked at (indexed or refused): a re-scan skips them

// Caller holds g_mu. Indexes the groom files not looked at yet; returns how many were added.
uint32_t scan(bool initial) {
  const ULONGLONG t0 = GetTickCount64();
  const std::string dir = groom_dir();
  WIN32_FIND_DATAA fd{};
  HANDLE h = FindFirstFileA((dir + "\\*.hgrm").c_str(), &fd);
  if (h == INVALID_HANDLE_VALUE) {
    if (initial) log("[hair groom] no groom files in %s (bg3rtx_hairconv writes them): the card path stays", dir.c_str());
    return 0;
  }
  uint32_t ok = 0, bad = 0;
  do {
    const std::string path = dir + "\\" + fd.cFileName;
    if (!g_paths.insert(path).second) continue;
    auto l = std::make_unique<Loaded>();
    l->path = path;
    FILE* f = nullptr;
    if (fopen_s(&f, path.c_str(), "rb") != 0 || !f) { log("[hair groom] %s: unreadable, ignored", path.c_str()); ++bad; continue; }
    uint8_t head[40 + kSamples * 8] = {};
    const size_t got = fread(head, 1, sizeof head, f);
    uint64_t at = 0;
    bool lods_ok = parse_header(head, got, l->asset, &at);
    if (lods_ok && l->asset.version == kV3) {
      // v3 (common/hair_groom_v3.h): the LOD table follows the header; the arrays are decoded at upload.
      std::vector<uint8_t> table(size_t(kV3HeaderBytes + 16ull * l->asset.lod_count));
      std::memcpy(table.data(), head, size_t(kV3HeaderBytes));
      V3Table t;
      lods_ok = _fseeki64(f, int64_t(kV3HeaderBytes), SEEK_SET) == 0 &&
                fread(table.data() + kV3HeaderBytes, 1, table.size() - size_t(kV3HeaderBytes), f) == table.size() - kV3HeaderBytes &&
                v3_table(table.data(), table.size(), l->asset.lod_count, t) && _fseeki64(f, 0, SEEK_END) == 0;
      for (uint32_t k = 0; lods_ok && k < t.lods; ++k) l->asset.lod_segments[k] = t.segments[k], l->asset.lod_points[k] = t.points[k];
      l->size = lods_ok ? uint64_t(_ftelli64(f)) : 0;
      lods_ok = lods_ok && l->size > t.streams_at;
      at = l->size;   // skips the v2 LOD walk below
      l->lod_at[0] = l->size;
    }
    // The LOD headers only (three 16-byte reads): the segment counts the crowd plan needs before any upload.
    for (uint32_t k = 0; lods_ok && l->asset.version != kV3 && k < l->asset.lod_count && k < 8; ++k) {
      uint8_t h16[16];
      uint32_t points = 0;
      l->lod_at[k] = at;
      lods_ok = _fseeki64(f, int64_t(at), SEEK_SET) == 0 && fread(h16, 1, 16, f) == 16 &&
                lod_header(h16, at, &points, &l->asset.lod_segments[k], &at);
      l->asset.lod_points[k] = points;
    }
    if (lods_ok && l->asset.version != kV3) {   // the LODs end exactly at the end of the file (as parse() requires)
      l->lod_at[l->asset.lod_count] = at;
      lods_ok = _fseeki64(f, 0, SEEK_END) == 0 && uint64_t(_ftelli64(f)) == at;
      l->size = at;
    }
    fclose(f);
    if (!lods_ok || g_assets.count(l->asset.index_id)) {
      log("[hair groom] %s: malformed or duplicate index id, ignored", path.c_str());
      ++bad;
      continue;
    }
    // One summary line below (5m: 871 per-file lines at every launch were log spam); failures keep their own line.
    g_counts.insert(l->asset.indices);
    g_assets[l->asset.index_id] = std::move(l);
    ++ok;
  } while (FindNextFileA(h, &fd));
  FindClose(h);
  const double ms = double(GetTickCount64() - t0);
  if (initial || ok || bad)
    log("[hair groom] %u groom files %s in %.0f ms, %u ignored (%s)", ok, initial ? "loaded" : "added at a level load", ms, bad,
        dir.c_str());
  return ok;
}

void load_all() {
  if (g_loaded) return;
  g_loaded = true;
  scan(true);
}
}  // namespace

// docs/RTX_HAIR_ROADMAP.md 2d: grooms the launcher's converter wrote while the game ran become usable at the next level
// load (never mid-level). Only new files are added; an indexed entry is never replaced or removed (draws may hold it):
// a groom rewritten in place is refused by read_lod's size check until the next game start.
uint32_t rescan() {
  std::lock_guard lock(g_mu);
  if (!g_loaded) return 0;   // the first use indexes everything anyway
  return scan(false);
}

bool read_lod(const Loaded& l, uint32_t index, std::vector<uint8_t>& bytes, Lod& lod) {
  bytes.clear();
  if (index >= l.asset.lod_count || index >= 8 || !l.size) return false;
  if (l.asset.version == kV3) {
    // The whole file (LOD 0's streams), decoded to this LOD's v2 bytes, then the v2 checks. Logged per decode: one
    // per groom upload (round 3 M6 uploads a LOD once), the cost line of docs/RTX_HAIR_ROADMAP.md 2b.
    if (l.size >= (uint64_t(1) << 31)) return false;
    FILE* f = nullptr;
    if (fopen_s(&f, l.path.c_str(), "rb") != 0 || !f) return false;
    std::vector<uint8_t> file;
    bool ok = _fseeki64(f, 0, SEEK_END) == 0 && uint64_t(_ftelli64(f)) == l.size && _fseeki64(f, 0, SEEK_SET) == 0;
    if (ok) {
      file.resize(size_t(l.size));
      ok = fread(file.data(), 1, file.size(), f) == file.size();
    }
    fclose(f);
    Asset h;
    ok = ok && parse_header(file.data(), file.size(), h) && h.index_id == l.asset.index_id && h.indices == l.asset.indices &&
         h.lod_count == l.asset.lod_count && h.version == kV3;
    const ULONGLONG t0 = GetTickCount64();
    ok = ok && decode_v3_lod(file.data(), file.size(), index, lzms_inflate, bytes);
    uint64_t at = 0;
    ok = ok && parse_lod(bytes.data(), bytes.size(), at, l.asset.indices / 3, lod) && at == bytes.size();
    if (ok) log("[hair groom] %016llx LOD %u decoded (v3): %u points in %llu ms", (unsigned long long)l.asset.index_id, index,
                lod.points, (unsigned long long)(GetTickCount64() - t0));
    else log("[hair groom] %s: v3 LOD %u malformed, the cards stay", l.path.c_str(), index);
    if (!ok) bytes.clear();
    return ok;
  }
  const uint64_t begin = l.lod_at[index], end = l.lod_at[index + 1];
  if (end <= begin || end - begin >= (uint64_t(1) << 31)) return false;
  FILE* f = nullptr;
  if (fopen_s(&f, l.path.c_str(), "rb") != 0 || !f) return false;
  // The index entry must still describe this file (it may have been replaced since startup): size and header.
  uint8_t head[40 + kSamples * 8] = {};
  bool ok = _fseeki64(f, 0, SEEK_END) == 0 && uint64_t(_ftelli64(f)) == l.size && _fseeki64(f, 0, SEEK_SET) == 0;
  const size_t got = ok ? fread(head, 1, sizeof head, f) : 0;
  Asset h;
  ok = ok && parse_header(head, got, h) && h.index_id == l.asset.index_id && h.indices == l.asset.indices &&
       h.lod_count == l.asset.lod_count;
  if (ok) {
    bytes.resize(size_t(end - begin));
    ok = _fseeki64(f, int64_t(begin), SEEK_SET) == 0 && fread(bytes.data(), 1, bytes.size(), f) == bytes.size();
  }
  fclose(f);
  uint64_t at = 0;
  ok = ok && parse_lod(bytes.data(), bytes.size(), at, l.asset.indices / 3, lod) && at == bytes.size();
  if (!ok) bytes.clear();
  return ok;
}
namespace {
}  // namespace

const void* device_features(InstanceData* id, VkPhysicalDevice phys, const void* next, std::vector<const char*>& extensions) {
  g_requested = false;
  // Prepare optional supported LSS hardware even while the live effect is off.
  if (g_suppressed || !id || !id->GetPhysicalDeviceFeatures2) return next;
  uint32_t n = 0;
  id->EnumerateDeviceExtensionProperties(phys, nullptr, &n, nullptr);
  std::vector<VkExtensionProperties> avail(n);
  if (id->EnumerateDeviceExtensionProperties(phys, nullptr, &n, avail.data()) != VK_SUCCESS) return next;
  auto has = [&](const char* e) {
    for (const auto& a : avail)
      if (!strcmp(a.extensionName, e)) return true;
    return false;
  };
  if (!has(VK_NV_RAY_TRACING_LINEAR_SWEPT_SPHERES_EXTENSION_NAME) || !has(VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME)) {
    log("[hair groom] VK_NV_ray_tracing_linear_swept_spheres unavailable: the card path stays");
    return next;
  }
  VkPhysicalDeviceRayTracingLinearSweptSpheresFeaturesNV f{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_LINEAR_SWEPT_SPHERES_FEATURES_NV};
  VkPhysicalDeviceFeatures2 all{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2, &f};
  id->GetPhysicalDeviceFeatures2(phys, &all);
  if (!f.linearSweptSpheres) {
    log("[hair groom] linearSweptSpheres feature unsupported: the card path stays");
    return next;
  }
  auto* existing = reinterpret_cast<const VkPhysicalDeviceRayTracingLinearSweptSpheresFeaturesNV*>(find_struct(next, f.sType));
  if (existing && !existing->linearSweptSpheres) {
    log("[hair groom] the application disabled linearSweptSpheres: the card path stays");
    return next;
  }
  if (!existing) {
    g_feature = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_LINEAR_SWEPT_SPHERES_FEATURES_NV};
    g_feature.linearSweptSpheres = VK_TRUE;
    g_feature.pNext = const_cast<void*>(next);
    next = &g_feature;
  }
  // The NV extension depends on VK_KHR_ray_tracing_pipeline: enabled as an extension only
  // (no rayTracingPipeline feature, nothing of it is used).
  for (const char* e : {VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME, VK_NV_RAY_TRACING_LINEAR_SWEPT_SPHERES_EXTENSION_NAME}) {
    bool listed = false;
    for (auto x : extensions) listed |= !strcmp(x, e);
    if (!listed) extensions.push_back(e);
  }
  g_requested = true;
  log("[hair groom] + VK_NV_ray_tracing_linear_swept_spheres (linearSweptSpheres), VK_KHR_ray_tracing_pipeline");
  return next;
}
bool device_requested() { return g_requested; }
void disable_device_request() { g_requested = false; g_suppressed = true; }
void device_created(DeviceData*, bool rt_enabled) {
  {
    std::lock_guard lock(g_mu);
    g_enabled = g_requested && rt_enabled;
    // Always: RTX Hair can be switched on live (5m), so jobs that start cards-only still need the device verdict.
    log("[hair groom] device %s linear swept spheres", g_enabled ? "with" : "WITHOUT (card path stays)");
    if (!g_enabled || g_loaded) return;
  }
  // docs/RTX_HAIR_PRIMARY.md 5m: index the groom files at startup on a below-normal thread, not at the first use. A
  // live switch-on otherwise indexed all of them on the render thread (rhl1 2026-11-09: 871 files, 3.6 s stall).
  // Header reads only; the render thread's first find() waits for the lock if it comes first.
  HMODULE self = nullptr;   // keep the layer loaded while the thread runs
  GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
                     reinterpret_cast<LPCSTR>(&device_created), &self);
  std::thread([] {
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);
    std::lock_guard lock(g_mu);
    load_all();
  }).detach();
}
bool lss_enabled() { return g_enabled; }

const Loaded* find(uint64_t index_id, uint32_t index_count) {
  std::lock_guard lock(g_mu);
  load_all();
  auto it = g_assets.find(index_id);
  return it != g_assets.end() && it->second->asset.indices == index_count ? it->second.get() : nullptr;
}
std::vector<const Loaded*> with_count(uint32_t index_count) {
  std::lock_guard lock(g_mu);
  load_all();
  std::vector<const Loaded*> out;
  for (const auto& [id, l] : g_assets)
    if (l->asset.indices == index_count) out.push_back(l.get());
  return out;
}
bool count_known(uint32_t index_count) {
  std::lock_guard lock(g_mu);
  load_all();
  return g_counts.count(index_count) != 0;
}

namespace {
std::mutex g_seen_mu;
std::set<std::pair<uint32_t, uint64_t>> g_seen_pending;
constexpr size_t kSeenMax = 4096;   // lines kept in hair_seen.txt (one per unconverted mesh: ~1200 exist)
}  // namespace

std::string seen_path() {
  std::string db = assetdb_file();
  const size_t s = db.find_last_of("\\/");
  return (s == std::string::npos ? std::string(".") : db.substr(0, s)) + "\\hair_seen.txt";
}

void note_seen(uint32_t index_count, uint64_t index_id) {
  if (!index_count) return;
  std::lock_guard lock(g_seen_mu);
  if (g_seen_pending.size() < kSeenMax) g_seen_pending.insert({index_count, index_id});
}

void flush_seen() {
  std::set<std::pair<uint32_t, uint64_t>> add;
  {
    std::lock_guard lock(g_seen_mu);
    add.swap(g_seen_pending);
  }
  if (add.empty()) return;
  const std::string path = seen_path();
  std::set<std::pair<uint32_t, uint64_t>> all;
  FILE* f = nullptr;
  if (fopen_s(&f, path.c_str(), "rb") == 0 && f) {
    char line[128];
    while (fgets(line, sizeof line, f) && all.size() < kSeenMax) {
      unsigned count = 0;
      unsigned long long id = 0;
      if (line[0] != '#' && sscanf_s(line, "%u %llx", &count, &id) == 2 && count) all.insert({count, uint64_t(id)});
    }
    fclose(f);
  }
  const size_t before = all.size();
  for (const auto& e : add)
    if (all.size() < kSeenMax) all.insert(e);
  if (all.size() == before) return;
  std::string text = "# hair drawn without a strand groom (BG3RTX layer): index count, index id (0 = unknown)\n";
  char buf[64];
  for (const auto& [count, id] : all) {
    snprintf(buf, sizeof buf, "%u %016llx\n", count, (unsigned long long)id);
    text += buf;
  }
  const std::string tmp = path + "." + std::to_string(GetCurrentProcessId()) + ".tmp";
  if (write_file(tmp, text.data(), text.size()) && MoveFileExA(tmp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING))
    log("[hair groom] seen list: %zu new, %zu styles without a groom in %s", all.size() - before, all.size(), path.c_str());
  else
    DeleteFileA(tmp.c_str());
}
}  // namespace bg3::hair_groom
