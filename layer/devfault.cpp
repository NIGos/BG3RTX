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
#include <thread>

namespace bg3 {

namespace {

std::vector<const char*> g_ext_names;
VkPhysicalDeviceFaultFeaturesEXT g_f_fault{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FAULT_FEATURES_EXT};
bool g_enabled = false;
PFN_vkGetDeviceFaultInfoEXT p_fault_info = nullptr;
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

}  // namespace

const VkDeviceCreateInfo* fault_device_create_info(InstanceData* id, VkPhysicalDevice phys, const VkDeviceCreateInfo* ci,
                                                   VkDeviceCreateInfo* scratch) {
  g_enabled = false;
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
  bool have = false;
  for (auto& a : avail) have = have || !strcmp(a.extensionName, VK_EXT_DEVICE_FAULT_EXTENSION_NAME);
  if (!have) {
    log("[fault] %s not offered by the driver: no fault details on a device loss", VK_EXT_DEVICE_FAULT_EXTENSION_NAME);
    return ci;
  }
  VkPhysicalDeviceFaultFeaturesEXT ff{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FAULT_FEATURES_EXT};
  VkPhysicalDeviceFeatures2 f2{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2, &ff};
  id->GetPhysicalDeviceFeatures2(phys, &f2);
  if (!ff.deviceFault) {
    log("[fault] deviceFault feature not supported");
    return ci;
  }
  g_ext_names.assign(ci->ppEnabledExtensionNames, ci->ppEnabledExtensionNames + ci->enabledExtensionCount);
  bool listed = false;
  for (auto e : g_ext_names) listed = listed || !strcmp(e, VK_EXT_DEVICE_FAULT_EXTENSION_NAME);
  if (find_in(ci->pNext, VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FAULT_FEATURES_EXT)) {
    // Already asked for by someone else: use it as it is.
    g_enabled = listed;
    return ci;
  }
  if (!listed) g_ext_names.push_back(VK_EXT_DEVICE_FAULT_EXTENSION_NAME);
  g_f_fault = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FAULT_FEATURES_EXT};
  g_f_fault.deviceFault = VK_TRUE;
  g_f_fault.deviceFaultVendorBinary = VK_FALSE;
  g_f_fault.pNext = const_cast<void*>(ci->pNext);
  *scratch = *ci;
  scratch->pNext = &g_f_fault;
  scratch->enabledExtensionCount = uint32_t(g_ext_names.size());
  scratch->ppEnabledExtensionNames = g_ext_names.data();
  g_enabled = true;
  log("[fault] + %s (deviceFault)", VK_EXT_DEVICE_FAULT_EXTENSION_NAME);
  return scratch;
}

void fault_device_created(DeviceData* dd, bool enabled) {
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
  }
  for (uint32_t i = 0; i < counts.vendorInfoCount && i < vendor.size(); ++i)
    log("[fault]   vendor: \"%s\" code 0x%llx data 0x%llx", vendor[i].description,
        (unsigned long long)vendor[i].vendorFaultCode, (unsigned long long)vendor[i].vendorFaultData);
  log("[fault] %zu live and %zu recently freed ranges of ours were registered", g_live.size(), g_freed.size());
}

void fault_on_result(DeviceData* dd, VkResult r, const char* where) {
  if (r != VK_ERROR_DEVICE_LOST || !dd || g_reported.exchange(true)) return;
  log("[fault] VK_ERROR_DEVICE_LOST from %s in frame %llu (presents completed on the GPU: %llu)", where,
      (unsigned long long)frame_index(), (unsigned long long)life_gpu_done());
  rt_log_state("[fault]");
  bvh_log_state("[fault]");
  fault_details(dd, false);
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
