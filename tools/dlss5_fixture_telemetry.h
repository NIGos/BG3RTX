// Small, optional fixture telemetry adapter. Included after Context inside the
// fixture's anonymous namespace. No NVML SDK headers, import library or binaries.
// The handful of public NVML C ABI declarations below are resolved dynamically
// from the installed System32 driver library, on the Vulkan adapter's PCI address.
struct GpuTelemetry {
  struct Snapshot {
    unsigned sm_mhz{}, mem_mhz{};
    unsigned pstate = std::numeric_limits<unsigned>::max(), gpu_util = std::numeric_limits<unsigned>::max();
    bool clocks_valid{}, budget_valid{};
    uint64_t budget_bytes{}, usage_bytes{};
  };

  Context& c;
  HMODULE library{};
  using Device = void*;
  using Return = int;
  struct Utilization { unsigned gpu, memory; };
  using Init = Return (*)();
  using Shutdown = Return (*)();
  using GetHandle = Return (*)(const char*, Device*);
  using GetClock = Return (*)(Device, unsigned, unsigned*);
  using GetPstate = Return (*)(Device, unsigned*);
  using GetUtilization = Return (*)(Device, Utilization*);
  using ErrorString = const char* (*)(Return);
  Shutdown shutdown{};
  GetClock clock{};
  GetPstate pstate{};
  GetUtilization utilization{};
  ErrorString error_string{};
  Device device{};
  bool initialized{};

  template<class T> T proc(const char* name) const {
    return reinterpret_cast<T>(GetProcAddress(library, name));
  }
  const char* error(Return result) const {
    return error_string ? error_string(result) : "NVML error (nvmlErrorString unavailable)";
  }

  explicit GpuTelemetry(Context& context) : c(context) {
    std::printf("telemetry Vulkan memory_budget=%s (device-local heap budget/usage; bytes)\n",
                c.memory_budget ? "available" : "unavailable");
    uint32_t count{};
    auto result = vkEnumerateDeviceExtensionProperties(c.physical, nullptr, &count, nullptr);
    if (result != VK_SUCCESS) {
      std::printf("telemetry NVML unavailable: cannot query PCI-bus extension VkResult=%d\n", int(result));
      return;
    }
    std::vector<VkExtensionProperties> extensions(count);
    result = vkEnumerateDeviceExtensionProperties(c.physical, nullptr, &count, extensions.data());
    bool pci_available = false;
    if (result == VK_SUCCESS)
      for (const auto& extension : extensions)
        pci_available |= std::strcmp(extension.extensionName, VK_EXT_PCI_BUS_INFO_EXTENSION_NAME) == 0;
    if (!pci_available) {
      std::puts("telemetry NVML unavailable: Vulkan PCI address unavailable; refusing to guess adapter index");
      return;
    }
    VkPhysicalDevicePCIBusInfoPropertiesEXT pci{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PCI_BUS_INFO_PROPERTIES_EXT};
    VkPhysicalDeviceProperties2 properties{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2, &pci};
    vkGetPhysicalDeviceProperties2(c.physical, &properties);
    char address[32]{};
    std::snprintf(address, sizeof address, "%04x:%02x:%02x.%x", pci.pciDomain, pci.pciBus, pci.pciDevice, pci.pciFunction);
    std::printf("telemetry Vulkan adapter=%s PCI=%s\n", properties.properties.deviceName, address);

    wchar_t system[MAX_PATH]{};
    DWORD length = GetSystemDirectoryW(system, MAX_PATH);
    if (!length || length >= MAX_PATH) {
      std::puts("telemetry NVML unavailable: system directory lookup failed");
      return;
    }
    fs::path path = fs::path(system) / L"nvml.dll";
    library = LoadLibraryExW(path.c_str(), nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!library) {
      std::printf("telemetry NVML unavailable: System32/nvml.dll load error=%lu\n", GetLastError());
      return;
    }
    auto init = proc<Init>("nvmlInit_v2");
    shutdown = proc<Shutdown>("nvmlShutdown");
    auto get_handle = proc<GetHandle>("nvmlDeviceGetHandleByPciBusId_v2");
    clock = proc<GetClock>("nvmlDeviceGetClockInfo");
    pstate = proc<GetPstate>("nvmlDeviceGetPerformanceState");
    utilization = proc<GetUtilization>("nvmlDeviceGetUtilizationRates");
    error_string = proc<ErrorString>("nvmlErrorString");
    if (!init || !shutdown || !get_handle || !clock || !pstate) {
      std::puts("telemetry NVML unavailable: required public exports missing");
      return;
    }
    Return status = init();
    std::printf("telemetry nvmlInit_v2 status=%d (%s)\n", status, error(status));
    if (status != 0) return;
    initialized = true;
    status = get_handle(address, &device);
    std::printf("telemetry NVML PCI match=%s status=%d (%s) utilization=%s\n",
                address, status, error(status), utilization ? "available" : "unavailable");
    if (status != 0) device = nullptr;
  }

  GpuTelemetry(const GpuTelemetry&) = delete;
  GpuTelemetry& operator=(const GpuTelemetry&) = delete;

  Snapshot snapshot() const {
    Snapshot sample;
    if (device) {
      // Public nvmlClockType_t: SM=1, memory=2. Invalid values remain explicitly
      // marked; a missing counter must not be interpreted as a zero clock.
      Return sm = clock(device, 1, &sample.sm_mhz);
      Return memory = clock(device, 2, &sample.mem_mhz);
      Return state = pstate(device, &sample.pstate);
      sample.clocks_valid = sm == 0 && memory == 0 && state == 0;
      if (!sample.clocks_valid)
        std::printf("telemetry clocks unavailable: SM_status=%d memory_status=%d pstate_status=%d\n", sm, memory, state);
      if (utilization) {
        Utilization used{};
        Return status = utilization(device, &used);
        if (status == 0) sample.gpu_util = used.gpu;
        else std::printf("telemetry utilization unavailable: status=%d (%s)\n", status, error(status));
      }
    }
    if (c.memory_budget) {
      VkPhysicalDeviceMemoryBudgetPropertiesEXT budget{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MEMORY_BUDGET_PROPERTIES_EXT};
      VkPhysicalDeviceMemoryProperties2 properties{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MEMORY_PROPERTIES_2, &budget};
      vkGetPhysicalDeviceMemoryProperties2(c.physical, &properties);
      for (uint32_t i = 0; i < properties.memoryProperties.memoryHeapCount; ++i) {
        if ((properties.memoryProperties.memoryHeaps[i].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT) == 0) continue;
        sample.budget_bytes += budget.heapBudget[i];
        sample.usage_bytes += budget.heapUsage[i];
      }
      sample.budget_valid = true;
    }
    return sample;
  }

  ~GpuTelemetry() {
    if (initialized && shutdown) shutdown();
    if (library) FreeLibrary(library);
  }
};
