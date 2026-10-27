// Device-fault diagnostics (VK_EXT_device_fault).
//
// With rt=1 the layer adds VK_EXT_device_fault (feature deviceFault) to the
// game's device when the driver offers it, the same additive way as the RT
// extensions (rtscene.cpp). Our buffers register their device-address ranges
// here (soup, mesh pool, AS chunks, TLAS slots, InstInfo ring, alpha records,
// scratch). The first VK_ERROR_DEVICE_LOST that a submit or a present returns
// logs vkGetDeviceFaultInfoEXT: its description, every faulting address (with
// its precision) mapped to the registered ranges that contain it, live or
// freed, and the vendor records. The next fault, if any, names the buffer.
#include "host.h"

#include <algorithm>
#include <cstring>
#include <deque>
#include <map>
#include <thread>

namespace bg3 {

namespace {

std::vector<const char*> g_ext_names;
VkPhysicalDeviceFaultFeaturesEXT g_f_fault{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FAULT_FEATURES_EXT};
bool g_enabled = false;
PFN_vkGetDeviceFaultInfoEXT p_fault_info = nullptr;
// VK_NV_device_diagnostic_checkpoints: markers in the command stream; after a loss
// each queue reports the last marker every pipeline stage reached.
bool g_ckpt_dev = false;
PFN_vkCmdSetCheckpointNV p_set_ckpt = nullptr;
PFN_vkGetQueueCheckpointDataNV p_get_ckpt = nullptr;
std::mutex g_q_mu;
std::vector<VkQueue> g_queues;   // queues the game submitted to
std::atomic<bool> g_reported{false};

struct Range {
  std::string what;
  VkDeviceAddress lo = 0, hi = 0;
  uint64_t created = 0, freed = 0;   // frame_index(); freed 0: live
};
std::mutex g_mu;
std::vector<Range> g_live;
std::deque<Range> g_freed;           // the most recent frees (a fault on freed memory names them)
constexpr size_t kFreedKeep = 1024;

const VkBaseInStructure* find_in(const void* chain, VkStructureType t) {
  for (auto p = static_cast<const VkBaseInStructure*>(chain); p; p = p->pNext)
    if (p->sType == t) return p;
  return nullptr;
}

const char* type_name(VkDeviceFaultAddressTypeEXT t) {
  switch (t) {
    case VK_DEVICE_FAULT_ADDRESS_TYPE_NONE_EXT: return "none";
    case VK_DEVICE_FAULT_ADDRESS_TYPE_READ_INVALID_EXT: return "read invalid";
    case VK_DEVICE_FAULT_ADDRESS_TYPE_WRITE_INVALID_EXT: return "write invalid";
    case VK_DEVICE_FAULT_ADDRESS_TYPE_EXECUTE_INVALID_EXT: return "execute invalid";
    case VK_DEVICE_FAULT_ADDRESS_TYPE_INSTRUCTION_POINTER_UNKNOWN_EXT: return "instruction pointer (unknown)";
    case VK_DEVICE_FAULT_ADDRESS_TYPE_INSTRUCTION_POINTER_INVALID_EXT: return "instruction pointer (invalid)";
    case VK_DEVICE_FAULT_ADDRESS_TYPE_INSTRUCTION_POINTER_FAULT_EXT: return "instruction pointer (fault)";
    default: return "?";
  }
}

// The registered ranges overlapping [lo, hi).
std::string name_range(VkDeviceAddress lo, VkDeviceAddress hi) {
  std::string out;
  auto add = [&](const Range& r) {
    if (r.hi <= lo || r.lo >= hi) return;
    char b[256];
    snprintf(b, sizeof b, "%s%s [0x%llx, +%llu) at +%lld, created frame %llu%s", out.empty() ? "" : "; ", r.what.c_str(),
             (unsigned long long)r.lo, (unsigned long long)(r.hi - r.lo), (long long)(lo - r.lo),
             (unsigned long long)r.created, r.freed ? "" : ", live");
    out += b;
    if (r.freed) {
      snprintf(b, sizeof b, ", FREED in frame %llu", (unsigned long long)r.freed);
      out += b;
    }
  };
  for (const Range& r : g_live) add(r);
  for (const Range& r : g_freed) add(r);
  return out.empty() ? "no buffer of ours (the game's, or an unregistered one)" : out;
}

// ---- VK_EXT_device_address_binding_report: every address range the driver binds ----
bool g_bind_inst = false, g_bind_dev = false;
std::vector<const char*> g_inst_ext;
VkPhysicalDeviceAddressBindingReportFeaturesEXT g_f_bind{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ADDRESS_BINDING_REPORT_FEATURES_EXT};
VkDebugUtilsMessengerEXT g_messenger = VK_NULL_HANDLE;
VkInstance g_messenger_inst = VK_NULL_HANDLE;
PFN_vkDestroyDebugUtilsMessengerEXT p_destroy_messenger = nullptr;
struct Bound { VkDeviceSize size; VkObjectType type; uint64_t handle; uint64_t frame; bool internal; };
std::mutex g_bind_mu;
std::multimap<VkDeviceAddress, Bound> g_bound;   // live bindings by base address
struct Unbound { VkDeviceAddress base; Bound b; uint64_t frame; };
std::deque<Unbound> g_unbound;                    // the most recent unbinds
constexpr size_t kUnboundKeep = 16384;
uint64_t g_bind_count = 0, g_unbind_count = 0;

VKAPI_ATTR VkBool32 VKAPI_CALL on_binding(VkDebugUtilsMessageSeverityFlagBitsEXT, VkDebugUtilsMessageTypeFlagsEXT types,
                                          const VkDebugUtilsMessengerCallbackDataEXT* d, void*) {
  if (!(types & VK_DEBUG_UTILS_MESSAGE_TYPE_DEVICE_ADDRESS_BINDING_BIT_EXT) || !d) return VK_FALSE;
  auto* a = reinterpret_cast<const VkDeviceAddressBindingCallbackDataEXT*>(
      find_in(d->pNext, VK_STRUCTURE_TYPE_DEVICE_ADDRESS_BINDING_CALLBACK_DATA_EXT));
  if (!a) return VK_FALSE;
  const VkObjectType ot = d->objectCount ? d->pObjects[0].objectType : VK_OBJECT_TYPE_UNKNOWN;
  const uint64_t h = d->objectCount ? d->pObjects[0].objectHandle : 0;
  const uint64_t f = frame_index();
  std::lock_guard<std::mutex> lock(g_bind_mu);
  if (a->bindingType == VK_DEVICE_ADDRESS_BINDING_TYPE_BIND_EXT) {
    g_bound.emplace(a->baseAddress, Bound{a->size, ot, h, f, (a->flags & VK_DEVICE_ADDRESS_BINDING_INTERNAL_OBJECT_BIT_EXT) != 0});
    ++g_bind_count;
  } else {
    Bound b{a->size, ot, h, 0, false};
    auto range = g_bound.equal_range(a->baseAddress);
    for (auto it = range.first; it != range.second; ++it)
      if (it->second.handle == h) {
        b = it->second;
        g_bound.erase(it);
        break;
      }
    g_unbound.push_back({a->baseAddress, b, f});
    if (g_unbound.size() > kUnboundKeep) g_unbound.pop_front();
    ++g_unbind_count;
  }
  return VK_FALSE;
}

const char* object_name(VkObjectType t) {
  switch (t) {
    case VK_OBJECT_TYPE_DEVICE_MEMORY: return "memory";
    case VK_OBJECT_TYPE_BUFFER: return "buffer";
    case VK_OBJECT_TYPE_IMAGE: return "image";
    case VK_OBJECT_TYPE_ACCELERATION_STRUCTURE_KHR: return "acceleration structure";
    case VK_OBJECT_TYPE_QUEUE: return "queue";
    case VK_OBJECT_TYPE_DEVICE: return "device";
    default: return "object";
  }
}

// Every live or recently unbound driver binding overlapping [lo, hi).
std::string name_binding(VkDeviceAddress lo, VkDeviceAddress hi) {
  std::lock_guard<std::mutex> lock(g_bind_mu);
  if (!g_bind_count) return "";
  std::string out;
  char b[256];
  int n = 0;
  for (const auto& [base, x] : g_bound)
    if (base < hi && base + x.size > lo && n++ < 8) {
      snprintf(b, sizeof b, "%s%s 0x%llx [0x%llx, +%llu) at +%lld, bound frame %llu%s, LIVE", out.empty() ? "" : "; ",
               object_name(x.type), (unsigned long long)x.handle, (unsigned long long)base, (unsigned long long)x.size,
               (long long)(lo - base), (unsigned long long)x.frame, x.internal ? " (driver-internal)" : "");
      out += b;
    }
  for (auto it = g_unbound.rbegin(); it != g_unbound.rend(); ++it)
    if (it->base < hi && it->base + it->b.size > lo && n++ < 12) {
      snprintf(b, sizeof b, "%s%s 0x%llx [0x%llx, +%llu) at +%lld, bound frame %llu, UNBOUND in frame %llu",
               out.empty() ? "" : "; ", object_name(it->b.type), (unsigned long long)it->b.handle,
               (unsigned long long)it->base, (unsigned long long)it->b.size, (long long)(lo - it->base),
               (unsigned long long)it->b.frame, (unsigned long long)it->frame);
      out += b;
    }
  if (out.empty()) {
    snprintf(b, sizeof b, "no driver binding (%zu live, %zu recent unbinds recorded)", g_bound.size(), g_unbound.size());
    out = b;
  }
  return out;
}

}  // namespace

const VkDeviceCreateInfo* fault_device_create_info(InstanceData* id, VkPhysicalDevice phys, const VkDeviceCreateInfo* ci,
                                                   VkDeviceCreateInfo* scratch) {
  g_enabled = false;
  g_bind_dev = false;
  if (!cfg_int("rt", 0) || !cfg_int("dev_fault", 1) || !id || !id->GetPhysicalDeviceFeatures2) return ci;
  // Robust buffer access (the game's choice; we do not change it): out-of-range
  // descriptor reads are only defined with it. Our shaders clamp their own.
  VkBool32 robust = ci->pEnabledFeatures ? ci->pEnabledFeatures->robustBufferAccess : VK_FALSE;
  if (auto* f2 = reinterpret_cast<const VkPhysicalDeviceFeatures2*>(find_in(ci->pNext, VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2)))
    robust = robust || f2->features.robustBufferAccess;
  log("[fault] the game's device: robustBufferAccess %s", robust ? "ON" : "off");
  uint32_t n = 0;
  id->EnumerateDeviceExtensionProperties(phys, nullptr, &n, nullptr);
  std::vector<VkExtensionProperties> avail(n);
  id->EnumerateDeviceExtensionProperties(phys, nullptr, &n, avail.data());
  auto offered = [&](const char* e) {
    for (auto& a : avail)
      if (!strcmp(a.extensionName, e)) return true;
    return false;
  };
  g_ext_names.assign(ci->ppEnabledExtensionNames, ci->ppEnabledExtensionNames + ci->enabledExtensionCount);
  auto listed = [&](const char* e) {
    for (auto x : g_ext_names)
      if (!strcmp(x, e)) return true;
    return false;
  };
  VkPhysicalDeviceFaultFeaturesEXT ff{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FAULT_FEATURES_EXT};
  VkPhysicalDeviceAddressBindingReportFeaturesEXT bf{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ADDRESS_BINDING_REPORT_FEATURES_EXT, &ff};
  VkPhysicalDeviceFeatures2 f2{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2, &bf};
  id->GetPhysicalDeviceFeatures2(phys, &f2);
  const void* next = ci->pNext;
  bool changed = false;
  if (!offered(VK_EXT_DEVICE_FAULT_EXTENSION_NAME) || !ff.deviceFault) {
    log("[fault] %s not usable: no fault details on a device loss", VK_EXT_DEVICE_FAULT_EXTENSION_NAME);
  } else if (find_in(next, VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FAULT_FEATURES_EXT)) {
    g_enabled = listed(VK_EXT_DEVICE_FAULT_EXTENSION_NAME);   // asked for by someone else: as it is
  } else {
    if (!listed(VK_EXT_DEVICE_FAULT_EXTENSION_NAME)) g_ext_names.push_back(VK_EXT_DEVICE_FAULT_EXTENSION_NAME);
    g_f_fault = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FAULT_FEATURES_EXT};
    g_f_fault.deviceFault = VK_TRUE;
    g_f_fault.pNext = const_cast<void*>(next);
    next = &g_f_fault;
    g_enabled = changed = true;
    log("[fault] + %s (deviceFault)", VK_EXT_DEVICE_FAULT_EXTENSION_NAME);
  }
  // Every device-address range the driver binds or unbinds (the game's too), so a
  // fault address outside our buffers is still named (needs the instance messenger).
  if (g_bind_inst && offered(VK_EXT_DEVICE_ADDRESS_BINDING_REPORT_EXTENSION_NAME) && bf.reportAddressBinding &&
      !find_in(next, VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ADDRESS_BINDING_REPORT_FEATURES_EXT)) {
    if (!listed(VK_EXT_DEVICE_ADDRESS_BINDING_REPORT_EXTENSION_NAME))
      g_ext_names.push_back(VK_EXT_DEVICE_ADDRESS_BINDING_REPORT_EXTENSION_NAME);
    g_f_bind = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ADDRESS_BINDING_REPORT_FEATURES_EXT};
    g_f_bind.reportAddressBinding = VK_TRUE;
    g_f_bind.pNext = const_cast<void*>(next);
    next = &g_f_bind;
    g_bind_dev = changed = true;
    log("[fault] + %s (every address binding is recorded)", VK_EXT_DEVICE_ADDRESS_BINDING_REPORT_EXTENSION_NAME);
  } else {
    log("[fault] address binding reports off (instance messenger %d, offered %d, feature %d)", int(g_bind_inst),
        int(offered(VK_EXT_DEVICE_ADDRESS_BINDING_REPORT_EXTENSION_NAME)), int(bf.reportAddressBinding));
  }
  g_ckpt_dev = false;
  if (cfg_int("fault_checkpoints", 1) && offered(VK_NV_DEVICE_DIAGNOSTIC_CHECKPOINTS_EXTENSION_NAME)) {
    if (!listed(VK_NV_DEVICE_DIAGNOSTIC_CHECKPOINTS_EXTENSION_NAME))
      g_ext_names.push_back(VK_NV_DEVICE_DIAGNOSTIC_CHECKPOINTS_EXTENSION_NAME);
    g_ckpt_dev = changed = true;
    log("[fault] + %s (markers in our recorded work, read back after a loss)",
        VK_NV_DEVICE_DIAGNOSTIC_CHECKPOINTS_EXTENSION_NAME);
  }
  if (!changed) return ci;
  *scratch = *ci;
  scratch->pNext = next;
  scratch->enabledExtensionCount = uint32_t(g_ext_names.size());
  scratch->ppEnabledExtensionNames = g_ext_names.data();
  return scratch;
}

// ---- instance: a debug-utils messenger for the address binding reports ----
const VkInstanceCreateInfo* fault_instance_create_info(PFN_vkGetInstanceProcAddr next, const VkInstanceCreateInfo* ci,
                                                       VkInstanceCreateInfo* scratch) {
  g_bind_inst = false;
  if (!next || !ci || !cfg_int("rt", 0) || !cfg_int("dev_fault", 1) || !cfg_int("dev_fault_bindings", 1)) return ci;
  auto enumerate = reinterpret_cast<PFN_vkEnumerateInstanceExtensionProperties>(
      next(VK_NULL_HANDLE, "vkEnumerateInstanceExtensionProperties"));
  bool have = false;
  if (enumerate) {
    uint32_t n = 0;
    enumerate(nullptr, &n, nullptr);
    std::vector<VkExtensionProperties> avail(n);
    enumerate(nullptr, &n, avail.data());
    for (auto& a : avail) have = have || !strcmp(a.extensionName, VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
  }
  if (!have) {
    log("[fault] %s not offered: no address binding reports", VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
    return ci;
  }
  g_inst_ext.assign(ci->ppEnabledExtensionNames, ci->ppEnabledExtensionNames + ci->enabledExtensionCount);
  bool listed = false;
  for (auto e : g_inst_ext) listed = listed || !strcmp(e, VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
  g_bind_inst = true;
  if (listed) return ci;
  g_inst_ext.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
  *scratch = *ci;
  scratch->enabledExtensionCount = uint32_t(g_inst_ext.size());
  scratch->ppEnabledExtensionNames = g_inst_ext.data();
  log("[fault] + %s (instance, for the address binding reports)", VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
  return scratch;
}

void fault_instance_created(VkInstance inst, PFN_vkGetInstanceProcAddr next) {
  if (!g_bind_inst || g_messenger) return;
  auto create = reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(next(inst, "vkCreateDebugUtilsMessengerEXT"));
  p_destroy_messenger = reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(next(inst, "vkDestroyDebugUtilsMessengerEXT"));
  if (!create) {
    g_bind_inst = false;
    return;
  }
  VkDebugUtilsMessengerCreateInfoEXT mi{VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT};
  mi.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT |
                       VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
  mi.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_DEVICE_ADDRESS_BINDING_BIT_EXT;
  mi.pfnUserCallback = &on_binding;
  if (create(inst, &mi, nullptr, &g_messenger) != VK_SUCCESS) {
    g_messenger = VK_NULL_HANDLE;
    g_bind_inst = false;
    return;
  }
  g_messenger_inst = inst;
}

void fault_instance_destroyed(VkInstance inst) {
  if (g_messenger && inst == g_messenger_inst && p_destroy_messenger) p_destroy_messenger(inst, g_messenger, nullptr);
  if (inst == g_messenger_inst) g_messenger = VK_NULL_HANDLE, g_messenger_inst = VK_NULL_HANDLE;
}
void fault_device_created(DeviceData* dd, bool enabled) {
  if (enabled && g_ckpt_dev) {
    p_set_ckpt = reinterpret_cast<PFN_vkCmdSetCheckpointNV>(dd->gdpa(dd->device, "vkCmdSetCheckpointNV"));
    p_get_ckpt = reinterpret_cast<PFN_vkGetQueueCheckpointDataNV>(dd->gdpa(dd->device, "vkGetQueueCheckpointDataNV"));
    if (!p_set_ckpt || !p_get_ckpt) p_set_ckpt = nullptr, p_get_ckpt = nullptr;
    log("[fault] checkpoints %s", p_set_ckpt ? "ready" : "UNAVAILABLE (no entry points)");
  }
  g_enabled = enabled && g_enabled;
  p_fault_info = g_enabled ? reinterpret_cast<PFN_vkGetDeviceFaultInfoEXT>(dd->gdpa(dd->device, "vkGetDeviceFaultInfoEXT"))
                           : nullptr;
  if (g_enabled) log("[fault] device fault reporting %s", p_fault_info ? "ready" : "UNAVAILABLE (no entry point)");
  if (cfg_int("fault_poll_ms", 1000) > 0) fault_poll_start(dd);
}

void fault_track(const char* what, VkDeviceAddress addr, VkDeviceSize size) {
  if (!addr || !size) return;
  std::lock_guard<std::mutex> lock(g_mu);
  for (Range& r : g_live)
    if (r.lo == addr) {
      r.what = what;
      r.hi = addr + size;
      return;
    }
  Range r;
  r.what = what;
  r.lo = addr;
  r.hi = addr + size;
  r.created = frame_index();
  log("[fault] range of ours: %s [0x%llx, +%llu)", what, (unsigned long long)addr, (unsigned long long)size);
  g_live.push_back(std::move(r));
}

void fault_untrack(VkDeviceAddress addr) {
  if (!addr) return;
  std::lock_guard<std::mutex> lock(g_mu);
  for (size_t i = 0; i < g_live.size(); ++i)
    if (g_live[i].lo == addr) {
      g_live[i].freed = frame_index();
      g_freed.push_back(std::move(g_live[i]));
      if (g_freed.size() > kFreedKeep) g_freed.pop_front();
      g_live.erase(g_live.begin() + i);
      return;
    }
}

// vkGetDeviceFaultInfoEXT: addresses mapped to our ranges, vendor records.
// `quiet`: a query on a device that may not be lost (a stall): one line if empty.
static void fault_details(DeviceData* dd, bool quiet) {
  if (!p_fault_info) {
    log("[fault] no VK_EXT_device_fault on this device: no fault addresses");
    return;
  }
  VkDeviceFaultCountsEXT counts{VK_STRUCTURE_TYPE_DEVICE_FAULT_COUNTS_EXT};
  VkResult q = p_fault_info(dd->device, &counts, nullptr);
  if (q != VK_SUCCESS && q != VK_INCOMPLETE) {
    log("[fault] vkGetDeviceFaultInfoEXT (counts) failed: %d", q);
    return;
  }
  if (quiet && !counts.addressInfoCount && !counts.vendorInfoCount) {
    log("[fault] vkGetDeviceFaultInfoEXT: no fault recorded (%d)", q);
    return;
  }
  std::vector<VkDeviceFaultAddressInfoEXT> addrs(counts.addressInfoCount);
  std::vector<VkDeviceFaultVendorInfoEXT> vendor(counts.vendorInfoCount);
  counts.vendorBinarySize = 0;   // not asked for
  VkDeviceFaultInfoEXT info{VK_STRUCTURE_TYPE_DEVICE_FAULT_INFO_EXT};
  info.pAddressInfos = addrs.empty() ? nullptr : addrs.data();
  info.pVendorInfos = vendor.empty() ? nullptr : vendor.data();
  q = p_fault_info(dd->device, &counts, &info);
  log("[fault] vkGetDeviceFaultInfoEXT: %d; \"%s\"; %u address records, %u vendor records", q, info.description,
      counts.addressInfoCount, counts.vendorInfoCount);
  std::lock_guard<std::mutex> lock(g_mu);
  for (uint32_t i = 0; i < counts.addressInfoCount && i < addrs.size(); ++i) {
    const VkDeviceFaultAddressInfoEXT& a = addrs[i];
    const VkDeviceSize prec = a.addressPrecision ? a.addressPrecision : 1;
    const VkDeviceAddress lo = a.reportedAddress & ~(prec - 1), hi = lo + prec;
    const std::string bvh = bvh_describe_address(a.reportedAddress);
    log("[fault]   %s at 0x%llx (precision %llu): %s%s%s", type_name(a.addressType), (unsigned long long)a.reportedAddress,
        (unsigned long long)a.addressPrecision, name_range(lo, hi).c_str(), bvh.empty() ? "" : "; ", bvh.c_str());
    const std::string drv = name_binding(lo, hi);
    if (!drv.empty()) log("[fault]     driver bindings: %s", drv.c_str());
  }
  for (uint32_t i = 0; i < counts.vendorInfoCount && i < vendor.size(); ++i)
    log("[fault]   vendor: \"%s\" code 0x%llx data 0x%llx", vendor[i].description,
        (unsigned long long)vendor[i].vendorFaultCode, (unsigned long long)vendor[i].vendorFaultData);
  log("[fault] %zu live and %zu recently freed ranges of ours were registered:", g_live.size(), g_freed.size());
  for (const Range& r : g_live)
    log("[fault]   ours: %s [0x%llx, 0x%llx) created frame %llu", r.what.c_str(), (unsigned long long)r.lo,
        (unsigned long long)r.hi, (unsigned long long)r.created);
  {
    std::lock_guard<std::mutex> bl(g_bind_mu);
    log("[fault] driver address bindings: %s; %llu binds, %llu unbinds, %zu live", g_bind_dev ? "recorded" : "OFF",
        (unsigned long long)g_bind_count, (unsigned long long)g_unbind_count, g_bound.size());
  }
}

// ---- checkpoints ----
static const char* ckpt_site(uint32_t s) {
  switch (s) {
    case kCkCmdBegin: return "game command buffer begins";
    case kCkPassBeginPre: return "before core on_pass_begin";
    case kCkPassBeginPost: return "after core on_pass_begin";
    case kCkPassEndPre: return "before core on_pass_end";
    case kCkPassEndPost: return "after core on_pass_end";
    case kCkDispPrePre: return "before core on_dispatch_pre";
    case kCkDispPrePost: return "after core on_dispatch_pre";
    case kCkDispPre: return "before core on_dispatch";
    case kCkDispPost: return "after core on_dispatch";
    case kCkCmdEndPre: return "before core on_cmd_end";
    case kCkCmdEndPost: return "after core on_cmd_end";
    case kCkBvhStart: return "bvh build: start";
    case kCkBvhConverted: return "bvh build: soup converted";
    case kCkBvhBlas: return "bvh build: BLAS builds/refits done";
    case kCkBvhTlas: return "bvh build: TLAS built";
    case kCkSceneCommitted: return "scene slot committed (alpha set written)";
    case kCkLegacyStart: return "legacy build: start";
    case kCkLegacyTlas: return "legacy build: TLAS built";
    default: return "?";
  }
}

void fault_checkpoint(VkCommandBuffer cmd, uint32_t site, uint32_t arg) {
  if (!p_set_ckpt || !cmd) return;
  const uint64_t m = (uint64_t(site & 0xff) << 56) | (uint64_t(frame_index() & 0xffffff) << 32) | arg;
  p_set_ckpt(cmd, reinterpret_cast<const void*>(uintptr_t(m)));
}

void fault_note_queue(VkQueue q) {
  if (!p_get_ckpt || !q) return;
  std::lock_guard<std::mutex> lock(g_q_mu);
  for (VkQueue x : g_queues)
    if (x == q) return;
  g_queues.push_back(q);
}

static void dump_checkpoints() {
  if (!p_get_ckpt) return;
  std::vector<VkQueue> qs;
  {
    std::lock_guard<std::mutex> lock(g_q_mu);
    qs = g_queues;
  }
  for (VkQueue q : qs) {
    uint32_t n = 0;
    p_get_ckpt(q, &n, nullptr);
    std::vector<VkCheckpointDataNV> d(n, VkCheckpointDataNV{VK_STRUCTURE_TYPE_CHECKPOINT_DATA_NV});
    if (n) p_get_ckpt(q, &n, d.data());
    log("[fault] checkpoints of queue %p: %u", (void*)q, n);
    for (uint32_t i = 0; i < n; ++i) {
      const uint64_t m = uint64_t(uintptr_t(d[i].pCheckpointMarker));
      log("[fault]   stage 0x%x: last marker \"%s\", frame %llu, arg 0x%08x", d[i].stage, ckpt_site(uint32_t(m >> 56)),
          (unsigned long long)((m >> 32) & 0xffffff), uint32_t(m));
    }
  }
}

void fault_on_result(DeviceData* dd, VkResult r, const char* where) {
  if (r != VK_ERROR_DEVICE_LOST || !dd) return;
  if (g_reported.exchange(true)) {
    aftermath_wait();   // another thread reports; this one still holds the game until the dump is out
    return;
  }
  log("[fault] VK_ERROR_DEVICE_LOST from %s in frame %llu (presents completed on the GPU: %llu)", where,
      (unsigned long long)frame_index(), (unsigned long long)life_gpu_done());
  rt_log_state("[fault]");
  bvh_log_state("[fault]");
  fault_details(dd, false);
  dump_checkpoints();
  aftermath_wait();
}

// Active detection: once the device is lost the game stops calling Vulkan, so the
// submit/present hooks never see VK_ERROR_DEVICE_LOST. This thread polls our latest
// present fence every second (fault_poll_ms), and when presents stop for 5 s it logs
// the state once per stall and asks the driver for a fault record.
void fault_poll_start(DeviceData* dd) {
  static std::once_flag once;
  std::call_once(once, [dd] {
    std::thread([dd] {
      uint64_t last_frame = frame_index();
      ULONGLONG last_change = GetTickCount64();
      bool stall_logged = false;
      for (;;) {
        Sleep(DWORD((std::max)(cfg_int("fault_poll_ms", 1000), 100)));
        if (g_reported.load()) return;
        const VkResult r = life_poll_fence(dd);
        if (r == VK_ERROR_INITIALIZATION_FAILED) return;   // device destroyed
        if (r == VK_ERROR_DEVICE_LOST) {
          fault_on_result(dd, r, "the fault poll (our present fence)");
          return;
        }
        const uint64_t f = frame_index();
        const ULONGLONG now = GetTickCount64();
        if (f != last_frame) {
          if (stall_logged) log("[fault] presents resumed at frame %llu", (unsigned long long)f);
          last_frame = f;
          last_change = now;
          stall_logged = false;
        } else if (!stall_logged && f > 0 && now - last_change >= 5000) {
          stall_logged = true;
          log("[fault] no present for %llu s (frame %llu, presents done on the GPU %llu, %zu present fences pending, "
              "latest fence %d)",
              (unsigned long long)((now - last_change) / 1000), (unsigned long long)f,
              (unsigned long long)life_gpu_done(), life_pending_fences(), int(r));
          rt_log_state("[fault] stall:");
          bvh_log_state("[fault] stall:");
          fault_details(dd, true);
        }
      }
    }).detach();
  });
}

}  // namespace bg3
