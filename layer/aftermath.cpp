// NVIDIA Nsight Aftermath GPU crash dumps (aftermath=1 in bg3rtx.ini, default 0).
//
// The SDK is optional: CMake defines BG3RTX_AFTERMATH when it finds the headers
// (BG3RTX_AFTERMATH_SDK) and copies GFSDK_Aftermath_Lib.x64.dll next to the layer;
// the DLL is loaded at run time (never linked, never packaged).
//   - Before the driver's vkCreateDevice: GFSDK_Aftermath_EnableGpuCrashDumps for
//     Vulkan, with callbacks writing captures/aftermath/<time>.nv-gpudmp, the shader
//     debug info (<identifier>.nvdbg) and a description.
//   - The device gets VK_NV_device_diagnostics_config with shader debug info,
//     resource tracking (the dump names the resource behind a faulting VA, also a
//     destroyed one), automatic checkpoints and shader error reporting.
//   - Every SPIR-V module the device sees is mapped to its Aftermath shader hash in
//     captures/aftermath/shader_map.txt (ours and patched game modules are written
//     to captures/aftermath/shaders/; the game's own are in captures/shaders/ with
//     dump_shaders=1), for tools/aftermath_decode.
//   - On a device loss, aftermath_wait() waits up to 10 s for the dump to be written.
#include "host.h"

#include <cstring>
#include <ctime>

#if BG3RTX_AFTERMATH
#include <GFSDK_Aftermath.h>
#include <GFSDK_Aftermath_GpuCrashDump.h>
#include <GFSDK_Aftermath_GpuCrashDumpDecoding.h>
#endif

namespace bg3 {

#if BG3RTX_AFTERMATH
namespace {

using PFN_Enable = GFSDK_Aftermath_Result(GFSDK_AFTERMATH_CALL*)(GFSDK_Aftermath_Version, uint32_t, uint32_t,
                                                                  PFN_GFSDK_Aftermath_GpuCrashDumpCb,
                                                                  PFN_GFSDK_Aftermath_ShaderDebugInfoCb,
                                                                  PFN_GFSDK_Aftermath_GpuCrashDumpDescriptionCb,
                                                                  PFN_GFSDK_Aftermath_ResolveMarkerCb, void*);
using PFN_Status = GFSDK_Aftermath_Result(GFSDK_AFTERMATH_CALL*)(GFSDK_Aftermath_CrashDump_Status*);
using PFN_DebugId = GFSDK_Aftermath_Result(GFSDK_AFTERMATH_CALL*)(GFSDK_Aftermath_Version, const void*, const uint32_t,
                                                                   GFSDK_Aftermath_ShaderDebugInfoIdentifier*);
using PFN_HashSpirv = GFSDK_Aftermath_Result(GFSDK_AFTERMATH_CALL*)(GFSDK_Aftermath_Version, const GFSDK_Aftermath_SpirvCode*,
                                                                     GFSDK_Aftermath_ShaderBinaryHash*);

bool g_on = false;
PFN_Status p_status = nullptr;
PFN_DebugId p_debug_id = nullptr;
PFN_HashSpirv p_hash = nullptr;
std::mutex g_mu;
std::string g_dir;
std::atomic<int> g_dumps{0};
VkDeviceDiagnosticsConfigCreateInfoNV g_diag{VK_STRUCTURE_TYPE_DEVICE_DIAGNOSTICS_CONFIG_CREATE_INFO_NV};
std::vector<const char*> g_ext_names;

std::string stamp() {
  time_t t = time(nullptr);
  tm lt{};
  localtime_s(&lt, &t);
  char b[32];
  strftime(b, sizeof b, "%Y%m%d-%H%M%S", &lt);
  return b;
}

void GFSDK_AFTERMATH_CALL on_dump(const void* data, const uint32_t size, void*) {
  std::lock_guard<std::mutex> lock(g_mu);
  const std::string path = g_dir + "\\" + stamp() + "-" + std::to_string(g_dumps.load()) + ".nv-gpudmp";
  write_file(path, data, size);
  ++g_dumps;
  log("[aftermath] GPU crash dump written: %s (%u bytes); decode with bin\\aftermath_decode.exe", path.c_str(), size);
}

void GFSDK_AFTERMATH_CALL on_debug_info(const void* data, const uint32_t size, void*) {
  GFSDK_Aftermath_ShaderDebugInfoIdentifier id{};
  if (!p_debug_id || !GFSDK_Aftermath_SUCCEED(p_debug_id(GFSDK_Aftermath_Version_API, data, size, &id))) return;
  char name[64];
  snprintf(name, sizeof name, "%016llx-%016llx.nvdbg", (unsigned long long)id.id[0], (unsigned long long)id.id[1]);
  std::lock_guard<std::mutex> lock(g_mu);
  write_file(g_dir + "\\" + name, data, size);
}

void GFSDK_AFTERMATH_CALL on_description(PFN_GFSDK_Aftermath_AddGpuCrashDumpDescription add, void*) {
  add(GFSDK_Aftermath_GpuCrashDumpDescriptionKey_ApplicationName, "Baldur's Gate 3 + BG3RTX layer");
  add(GFSDK_Aftermath_GpuCrashDumpDescriptionKey_ApplicationVersion, __DATE__ " " __TIME__);
  char b[96];
  snprintf(b, sizeof b, "frame %llu, presents done on the GPU %llu", (unsigned long long)frame_index(),
           (unsigned long long)life_gpu_done());
  add(GFSDK_Aftermath_GpuCrashDumpDescriptionKey_UserDefined, b);
}

const VkBaseInStructure* find_in(const void* chain, VkStructureType t) {
  for (auto p = static_cast<const VkBaseInStructure*>(chain); p; p = p->pNext)
    if (p->sType == t) return p;
  return nullptr;
}

}  // namespace

void aftermath_pre_device() {
  if (g_on || !cfg_int("aftermath", 0)) return;
  const std::string dll = bin_dir() + "\\GFSDK_Aftermath_Lib.x64.dll";
  HMODULE m = LoadLibraryA(dll.c_str());
  if (!m) {
    log("[aftermath] %s not found: aftermath=1 ignored", dll.c_str());
    return;
  }
  auto enable = reinterpret_cast<PFN_Enable>(GetProcAddress(m, "GFSDK_Aftermath_EnableGpuCrashDumps"));
  p_status = reinterpret_cast<PFN_Status>(GetProcAddress(m, "GFSDK_Aftermath_GetCrashDumpStatus"));
  p_debug_id = reinterpret_cast<PFN_DebugId>(GetProcAddress(m, "GFSDK_Aftermath_GetShaderDebugInfoIdentifier"));
  p_hash = reinterpret_cast<PFN_HashSpirv>(GetProcAddress(m, "GFSDK_Aftermath_GetShaderHashSpirv"));
  if (!enable || !p_status) {
    log("[aftermath] entry points missing in %s", dll.c_str());
    return;
  }
  g_dir = root_dir() + "\\captures\\aftermath";
  make_dirs(g_dir + "\\shaders");
  GFSDK_Aftermath_Result r = enable(GFSDK_Aftermath_Version_API, GFSDK_Aftermath_GpuCrashDumpWatchedApiFlags_Vulkan,
                                    GFSDK_Aftermath_GpuCrashDumpFeatureFlags_Default, &on_dump, &on_debug_info,
                                    &on_description, nullptr, nullptr);
  g_on = GFSDK_Aftermath_SUCCEED(r);
  log("[aftermath] GPU crash dumps %s (result 0x%08x), into %s", g_on ? "ENABLED" : "FAILED", unsigned(r), g_dir.c_str());
}

const VkDeviceCreateInfo* aftermath_device_create_info(InstanceData* id, VkPhysicalDevice phys, const VkDeviceCreateInfo* ci,
                                                       VkDeviceCreateInfo* scratch) {
  if (!g_on || !id) return ci;
  uint32_t n = 0;
  id->EnumerateDeviceExtensionProperties(phys, nullptr, &n, nullptr);
  std::vector<VkExtensionProperties> avail(n);
  id->EnumerateDeviceExtensionProperties(phys, nullptr, &n, avail.data());
  bool have = false;
  for (auto& a : avail) have = have || !strcmp(a.extensionName, VK_NV_DEVICE_DIAGNOSTICS_CONFIG_EXTENSION_NAME);
  if (!have || find_in(ci->pNext, VK_STRUCTURE_TYPE_DEVICE_DIAGNOSTICS_CONFIG_CREATE_INFO_NV)) {
    log("[aftermath] %s %s", VK_NV_DEVICE_DIAGNOSTICS_CONFIG_EXTENSION_NAME, have ? "already configured" : "not offered");
    return ci;
  }
  g_ext_names.assign(ci->ppEnabledExtensionNames, ci->ppEnabledExtensionNames + ci->enabledExtensionCount);
  bool listed = false;
  for (auto e : g_ext_names) listed = listed || !strcmp(e, VK_NV_DEVICE_DIAGNOSTICS_CONFIG_EXTENSION_NAME);
  if (!listed) g_ext_names.push_back(VK_NV_DEVICE_DIAGNOSTICS_CONFIG_EXTENSION_NAME);
  g_diag = {VK_STRUCTURE_TYPE_DEVICE_DIAGNOSTICS_CONFIG_CREATE_INFO_NV};
  g_diag.flags = VK_DEVICE_DIAGNOSTICS_CONFIG_ENABLE_SHADER_DEBUG_INFO_BIT_NV |
                 VK_DEVICE_DIAGNOSTICS_CONFIG_ENABLE_RESOURCE_TRACKING_BIT_NV |
                 VK_DEVICE_DIAGNOSTICS_CONFIG_ENABLE_AUTOMATIC_CHECKPOINTS_BIT_NV |
                 VK_DEVICE_DIAGNOSTICS_CONFIG_ENABLE_SHADER_ERROR_REPORTING_BIT_NV;
  g_diag.pNext = const_cast<void*>(ci->pNext);
  *scratch = *ci;
  scratch->pNext = &g_diag;
  scratch->enabledExtensionCount = uint32_t(g_ext_names.size());
  scratch->ppEnabledExtensionNames = g_ext_names.data();
  log("[aftermath] + %s (shader debug info, resource tracking, automatic checkpoints, shader error reporting)",
      VK_NV_DEVICE_DIAGNOSTICS_CONFIG_EXTENSION_NAME);
  return scratch;
}

void aftermath_note_spirv(const char* what, const void* code, size_t size, const char* saved_path) {
  if (!g_on || !p_hash || !code || !size) return;
  GFSDK_Aftermath_SpirvCode sc{};
  sc.pData = code;
  sc.size = uint32_t(size);
  GFSDK_Aftermath_ShaderBinaryHash h{};
  if (!GFSDK_Aftermath_SUCCEED(p_hash(GFSDK_Aftermath_Version_API, &sc, &h))) return;
  char hx[24];
  snprintf(hx, sizeof hx, "%016llx", (unsigned long long)h.hash);
  std::string path = saved_path ? std::string(saved_path) : std::string();
  std::lock_guard<std::mutex> lock(g_mu);
  if (path.empty()) {
    path = g_dir + "\\shaders\\" + hx + ".spv";
    if (!file_mtime(path)) write_file(path, code, size);
  }
  const std::string line = std::string(hx) + " " + path + " " + what + "\n";
  FILE* f = nullptr;
  if (fopen_s(&f, (g_dir + "\\shader_map.txt").c_str(), "ab") == 0 && f) {
    fwrite(line.data(), 1, line.size(), f);
    fclose(f);
  }
}

void aftermath_wait() {
  if (!g_on || !p_status) return;
  // Every thread that sees the loss waits here (the game exits soon after one of
  // them returns); the first one polls, the others wait for it.
  static std::mutex wait_mu;
  static bool waited = false;
  std::lock_guard<std::mutex> wl(wait_mu);
  if (waited) return;
  waited = true;
  log("[aftermath] device lost: waiting for the GPU crash dump");
  GFSDK_Aftermath_CrashDump_Status st = GFSDK_Aftermath_CrashDump_Status_Unknown;
  const ULONGLONG t0 = GetTickCount64();
  while (GetTickCount64() - t0 < 10000) {
    p_status(&st);
    if (st == GFSDK_Aftermath_CrashDump_Status_CollectingDataFailed || st == GFSDK_Aftermath_CrashDump_Status_Finished) break;
    Sleep(50);
  }
  log("[aftermath] crash dump status %d after %llu ms (%d dumps written)", int(st),
      (unsigned long long)(GetTickCount64() - t0), g_dumps.load());
}

bool aftermath_enabled() { return g_on; }

#else   // no SDK at build time

void aftermath_pre_device() {
  if (cfg_int("aftermath", 0)) log("[aftermath] aftermath=1 but the layer was built without the Nsight Aftermath SDK");
}
const VkDeviceCreateInfo* aftermath_device_create_info(InstanceData*, VkPhysicalDevice, const VkDeviceCreateInfo* ci,
                                                       VkDeviceCreateInfo*) {
  return ci;
}
void aftermath_note_spirv(const char*, const void*, size_t, const char*) {}
void aftermath_wait() {}
bool aftermath_enabled() { return false; }

#endif

}  // namespace bg3
