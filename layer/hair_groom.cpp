#include "host.h"
#include "hair_groom.h"
#include "assetdb.h"
#include <windows.h>
#include <cstring>
#include <mutex>
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

// Caller holds g_mu.
void load_all() {
  if (g_loaded) return;
  g_loaded = true;
  const std::string dir = groom_dir();
  WIN32_FIND_DATAA fd{};
  HANDLE h = FindFirstFileA((dir + "\\*.hgrm").c_str(), &fd);
  if (h == INVALID_HANDLE_VALUE) {
    log("[hair groom] no groom files in %s (python -m tools.hair_groom.asset): the card path stays", dir.c_str());
    return;
  }
  uint32_t ok = 0, bad = 0;
  do {
    const std::string path = dir + "\\" + fd.cFileName;
    auto l = std::make_unique<Loaded>();
    l->path = path;
    FILE* f = nullptr;
    if (fopen_s(&f, path.c_str(), "rb") != 0 || !f) { ++bad; continue; }
    _fseeki64(f, 0, SEEK_END);
    const int64_t size = _ftelli64(f);
    _fseeki64(f, 0, SEEK_SET);
    if (size > 0 && size < (int64_t(1) << 31)) {
      l->bytes.resize(size_t(size));
      if (fread(l->bytes.data(), 1, l->bytes.size(), f) != l->bytes.size()) l->bytes.clear();
    }
    fclose(f);
    if (l->bytes.empty() || !parse(l->bytes.data(), l->bytes.size(), l->asset) || g_assets.count(l->asset.index_id)) {
      log("[hair groom] %s: malformed or duplicate index id, ignored", path.c_str());
      ++bad;
      continue;
    }
    log("[hair groom] loaded %s: index id %016llx, %u indices, %zu LODs, LOD0 %u points %u segments", path.c_str(),
        (unsigned long long)l->asset.index_id, l->asset.indices, l->asset.lods.size(), l->asset.lods[0].points,
        l->asset.lods[0].segments);
    g_counts.insert(l->asset.indices);
    g_assets[l->asset.index_id] = std::move(l);
    ++ok;
  } while (FindNextFileA(h, &fd));
  FindClose(h);
  log("[hair groom] %u groom files loaded, %u ignored", ok, bad);
}
}  // namespace

const void* device_features(InstanceData* id, VkPhysicalDevice phys, const void* next, std::vector<const char*>& extensions) {
  g_requested = false;
  if (g_suppressed || !cfg_int("rt_hair_groom", 0) || !id || !id->GetPhysicalDeviceFeatures2) return next;
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
  std::lock_guard lock(g_mu);
  g_enabled = g_requested && rt_enabled;
  if (cfg_int("rt_hair_groom", 0))
    log("[hair groom] device %s linear swept spheres", g_enabled ? "with" : "WITHOUT (card path stays)");
}
bool lss_enabled() { return g_enabled; }

const Loaded* find(uint64_t index_id, uint32_t index_count) {
  std::lock_guard lock(g_mu);
  load_all();
  auto it = g_assets.find(index_id);
  return it != g_assets.end() && it->second->asset.indices == index_count ? it->second.get() : nullptr;
}
bool count_known(uint32_t index_count) {
  std::lock_guard lock(g_mu);
  load_all();
  return g_counts.count(index_count) != 0;
}
}  // namespace bg3::hair_groom
