// Public SDK/core route for the offline NR fixture. Included after Context and
// RuntimeFile, inside the fixture's anonymous namespace. No snippet entry-point
// loads, caller-name spoofing, import patches or binary modification.
// Requires nvsdk_ngx.h and nvsdk_ngx_vk.h before the enclosing namespace.
struct CoreSession {
  RuntimeFile source, staged, core;
  std::wstring search, data;
  const wchar_t* paths[1]{};
  NVSDK_NGX_FeatureCommonInfo info{};
  NVSDK_NGX_Parameter* params{};
  NVSDK_NGX_Parameter* caps{};
  bool vk{}, initialized{};
  Context* context{};
  fs::path directory;

  // NGX has no callback user-data argument. The fixture opens one route/session
  // at a time; shutdown happens before this file is detached from the callback.
  inline static SRWLOCK callback_lock = SRWLOCK_INIT;
  inline static FILE* callback_file{};
  FILE* own_log{};
  bool finish_requested{}, process_lifetime_retention{}, finish_failed{}, shutdown_stalled{};
  NVSDK_NGX_Result shutdown_result = NVSDK_NGX_Result_Fail;
  VkResult idle_result = VK_NOT_READY;
  HANDLE shutdown_worker{};

  CoreSession() = default;
  CoreSession(const CoreSession&) = delete;

  static void NVSDK_CONV callback(const char* message, NVSDK_NGX_Logging_Level level,
                                  NVSDK_NGX_Feature component) noexcept {
    AcquireSRWLockExclusive(&callback_lock);
    if (message && (std::strstr(message, "dlssnr") || std::strstr(message, "DLSSNR"))) {
      std::fprintf(stdout, "NGX level=%u feature=%u %s\n", unsigned(level), unsigned(component), message);
      std::fflush(stdout);
    }
    if (callback_file) {
      std::fprintf(callback_file, "level=%u feature=%u %s\n", unsigned(level), unsigned(component), message ? message : "(null)");
      std::fflush(callback_file);
    }
    ReleaseSRWLockExclusive(&callback_lock);
  }

  static NVSDK_NGX_Result log_result(const char* operation, NVSDK_NGX_Result result) {
    std::printf("core %s result=0x%08x\n", operation, unsigned(result));
    std::fflush(stdout);
    return result;
  }

  static std::uint64_t file_version(const fs::path& path) {
    DWORD ignored{};
    DWORD bytes = GetFileVersionInfoSizeW(path.c_str(), &ignored);
    if (!bytes) return 0;
    std::vector<std::uint8_t> block(bytes);
    if (!GetFileVersionInfoW(path.c_str(), 0, bytes, block.data())) return 0;
    VS_FIXEDFILEINFO* version{}; UINT size{};
    if (!VerQueryValueW(block.data(), L"\\", reinterpret_cast<void**>(&version), &size) ||
        size < sizeof(*version) || version->dwSignature != 0xfeef04bd) return 0;
    return (std::uint64_t(version->dwFileVersionMS) << 32) | version->dwFileVersionLS;
  }

  static fs::path newest_installed_core() {
    wchar_t system[MAX_PATH]{};
    need(GetSystemDirectoryW(system, MAX_PATH) != 0, "system directory unavailable for NGX core discovery");
    fs::path store = fs::path(system) / "DriverStore/FileRepository";
    fs::path selected;
    std::uint64_t version{};
    for (const auto& dir : fs::directory_iterator(store, fs::directory_options::skip_permission_denied)) {
      fs::path candidate = dir.path() / "_nvngx.dll";
      if (!fs::is_regular_file(candidate)) continue;
      const auto current = file_version(candidate);
      // Stable path tie-break avoids filesystem enumeration order selecting the core.
      if (selected.empty() || current > version || (current == version && candidate.native() < selected.native())) {
        selected = candidate; version = current;
      }
    }
    need(!selected.empty(), "installed NGX core missing; provide --ngx-core with a signed core path");
    return selected;
  }

  void log_loaded_core() const {
    std::vector<wchar_t> actual(32768);
    DWORD length = GetModuleFileNameW(core.module, actual.data(), DWORD(actual.size()));
    need(length && length < actual.size(), "loaded NGX core path unavailable");
    fs::path path(actual.data());
    std::printf("core loaded_module=%ls\n", path.c_str());
    need(fs::equivalent(path, core.path), "loaded NGX core differs from the verified locked file");
  }

  void prepare(const fs::path& runtime_path, const fs::path& optional_core_path,
               const std::string& route_name) {
    need(!context && !initialized && directory.empty(), "CoreSession prepare called more than once");
    need(!route_name.empty(), "empty core route name");
    for (unsigned char ch : route_name)
      need((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
           (ch >= '0' && ch <= '9') || ch == '-', "invalid core route name");
    source.verify(runtime_path, true);
    fs::create_directories(fs::absolute(".scratch"));
    const std::string stem = "dlss5-core-" + route_name + "-" + std::to_string(GetCurrentProcessId());
    for (unsigned suffix = 0; suffix < 10000; ++suffix) {
      auto candidate = fs::absolute(fs::path(".scratch") / (stem + (suffix ? "-" + std::to_string(suffix) : "")));
      if (fs::create_directory(candidate)) { directory = candidate; break; }
    }
    need(!directory.empty(), "cannot reserve a unique core-session scratch directory");
    const auto runtime_dir = directory / "runtime";
    const auto log_dir = directory / "logs";
    fs::create_directory(runtime_dir); fs::create_directory(log_dir);
    fs::path copy = runtime_dir / "nvngx_dlssnr.dll";
    need(fs::copy_file(source.path, copy, fs::copy_options::none), "runtime scratch copy failed");
    staged.verify(copy, true);
    need(staged.digest == source.digest, "staged NR runtime SHA-256 differs from the verified source");
    std::printf("core staged_runtime=%ls hash_identical=1\n", staged.path.c_str());

    core.verify(optional_core_path.empty() ? newest_installed_core() : optional_core_path, false);
    core.load();
    log_loaded_core();
    // The staged NR snippet is discovered by the public NGX core. Never call
    // LoadLibrary or a direct Init export on it here.
    search = runtime_dir.wstring(); data = log_dir.wstring(); paths[0] = search.c_str();
    info.PathListInfo.Path = paths; info.PathListInfo.Length = 1;
    info.LoggingInfo.LoggingCallback = &callback;
    info.LoggingInfo.MinimumLoggingLevel = NVSDK_NGX_LOGGING_LEVEL_VERBOSE;
    info.LoggingInfo.DisableOtherLoggingSinks = true; // Full verbose log is the flushed callback file.
    fs::path log = log_dir / "ngx-callback.log";
    need(_wfopen_s(&own_log, log.c_str(), L"wb") == 0 && own_log, "cannot open NGX callback log");
    AcquireSRWLockExclusive(&callback_lock);
    if (callback_file) {
      ReleaseSRWLockExclusive(&callback_lock);
      throw std::runtime_error("only one public NGX CoreSession may be active");
    }
    callback_file = own_log;
    ReleaseSRWLockExclusive(&callback_lock);
    std::printf("core search=%ls data=%ls callback_log=%ls\n", search.c_str(), data.c_str(), log.c_str());
  }

  void capabilities() {
    auto result = vk ? NVSDK_NGX_VULKAN_GetCapabilityParameters(&caps)
                     : NVSDK_NGX_D3D12_GetCapabilityParameters(&caps);
    log_result("GetCapabilityParameters", result);
    if (result != NVSDK_NGX_Result_Success || !caps) {
      std::puts("core capabilities unknown; separate allocation/create probe remains permitted");
      return;
    }
    for (const char* key : {"DLSSNR.Available", "DLSSNR.NeedsUpdatedDriver", "DLSSNR.FeatureInitResult",
                            "DLSSNR.MinDriverVersionMajor", "DLSSNR.MinDriverVersionMinor",
                            "SuperSampling.Available", "SuperSampling.NeedsUpdatedDriver",
                            "SuperSampling.FeatureInitResult", "18.Available", "18.NeedsUpdatedDriver",
                            "18.FeatureInitResult"}) {
      int value{};
      auto got = caps->Get(key, &value);
      std::printf("core capability key=%s result=0x%08x ", key, unsigned(got));
      if (got == NVSDK_NGX_Result_Success) std::printf("value=%d bits=0x%08x\n", value, unsigned(value));
      else std::puts("value=unknown (unavailable key is not evidence of unsupported NR)");
    }
    std::fflush(stdout);
  }

  void init(Context& c, bool vulkan) {
    need(!directory.empty() && !context && !initialized, "CoreSession must be prepared exactly once before init");
    context = &c; vk = vulkan;
    auto result = vk
      ? NVSDK_NGX_VULKAN_Init(0x0876232cULL, data.c_str(), c.instance, c.physical, c.device,
                             &vkGetInstanceProcAddr, &vkGetDeviceProcAddr, &info, NVSDK_NGX_Version_API)
      : NVSDK_NGX_D3D12_Init(0x0876232cULL, data.c_str(), c.d12.Get(), &info, NVSDK_NGX_Version_API);
    log_result(vk ? "NVSDK_NGX_VULKAN_Init" : "NVSDK_NGX_D3D12_Init", result);
    ngxcheck(unsigned(result), "public NGX core Init");
    initialized = true;
    log_loaded_core();
    capabilities();
    result = vk ? NVSDK_NGX_VULKAN_AllocateParameters(&params) : NVSDK_NGX_D3D12_AllocateParameters(&params);
    log_result("AllocateParameters", result);
    ngxcheck(unsigned(result), "public NGX core AllocateParameters");
    need(params != nullptr, "public NGX core returned null parameters");
  }

  static DWORD WINAPI shutdown_thread(void* opaque) {
    auto& s = *static_cast<CoreSession*>(opaque);
    s.shutdown_result = s.vk ? NVSDK_NGX_VULKAN_Shutdown1(s.context->device)
                            : NVSDK_NGX_D3D12_Shutdown1(s.context->d12.Get());
    log_result(s.vk ? "NVSDK_NGX_VULKAN_Shutdown1" : "NVSDK_NGX_D3D12_Shutdown1", s.shutdown_result);
    return 0;
  }

  static DWORD WINAPI idle_thread(void* opaque) {
    auto& s = *static_cast<CoreSession*>(opaque);
    s.idle_result = vkDeviceWaitIdle(s.context->device);
    std::printf("core before shutdown vkDeviceWaitIdle result=%d\n", int(s.idle_result));
    std::fflush(stdout);
    return 0;
  }

  void destroy_parameters() {
    if (params) log_result("DestroyParameters", vk ? NVSDK_NGX_VULKAN_DestroyParameters(params)
                                                  : NVSDK_NGX_D3D12_DestroyParameters(params));
    if (caps) log_result("DestroyCapabilityParameters", vk ? NVSDK_NGX_VULKAN_DestroyParameters(caps)
                                                          : NVSDK_NGX_D3D12_DestroyParameters(caps));
    params = nullptr; caps = nullptr;
  }

  void retain_until_process_exit(const char* reason) {
    process_lifetime_retention = true;
    auto pin = [](HMODULE module) {
      if (!module) return;
      HMODULE pinned{};
      if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
                             reinterpret_cast<LPCWSTR>(module), &pinned)) {
        std::fprintf(stderr, "cannot pin an active NGX module; terminating without unsafe unload\n");
        TerminateProcess(GetCurrentProcess(), 2);
      }
    };
    pin(core.module);
    // Merely retain a module the core has already loaded. Never LoadLibrary the
    // feature snippet directly, and never modify its import table or identity.
    HMODULE loaded_nr = GetModuleHandleW(staged.path.c_str());
    pin(loaded_nr);
    std::printf("core process-lifetime retention: %s; core_pinned=1 staged_module_pinned=%u\n",
                reason, loaded_nr ? 1u : 0u);
    std::puts("core callback, module locks, session and VkDevice remain alive until immediate process exit; no unload or device destruction");
    std::fflush(stdout);
    if (own_log) std::fflush(own_log);
  }

  // Caller MUST have released the feature and observed all its GPU work complete.
  // true: SDK shut down and ordinary C++ teardown is safe. false: caller must
  // print its result and call ExitProcess(status) immediately, while this object,
  // Context, callback file and verified runtime locks remain alive. Never unwind
  // an un-shutdown runtime over vkDestroyDevice or FreeLibrary.
  bool finish_vulkan(bool attempt_shutdown = false) {
    need(vk && initialized && context, "finish_vulkan requires an initialized Vulkan core session");
    if (finish_requested) return !process_lifetime_retention;
    finish_requested = true;
    context->wait_vulkan(); // Finite fixture-fence waits before the SDK idle call.
    HANDLE idle = CreateThread(nullptr, 0, idle_thread, this, 0, nullptr);
    if (!idle) { finish_failed = true; retain_until_process_exit("cannot start Vulkan idle probe"); return false; }
    DWORD waited = WaitForSingleObject(idle, 20000);
    CloseHandle(idle); // Does not terminate a still-running worker.
    if (waited != WAIT_OBJECT_0 || idle_result != VK_SUCCESS) {
      finish_failed = true;
      retain_until_process_exit("Vulkan idle failed or exceeded 20 seconds");
      return false;
    }
    destroy_parameters();
    if (!attempt_shutdown) {
      // Existing shared-NGX layer hook: layer/streamline.cpp:576 returns success
      // for the game's Shutdown while Streamline owns that session. This fixture
      // has no other SDK owner, so its explicitly chosen no-op is process-exit
      // retention, NOT evidence that a standalone session shut down cleanly.
      std::puts("core Vulkan Shutdown1 explicitly skipped (no-op policy, like fg_game_ngx=2); standalone session retained for process exit");
      retain_until_process_exit("observed Shutdown1 stall avoided by explicit no-op policy");
      return false;
    }
    shutdown_worker = CreateThread(nullptr, 0, shutdown_thread, this, 0, nullptr);
    if (!shutdown_worker) { finish_failed = true; retain_until_process_exit("cannot start NGX shutdown probe"); return false; }
    for (unsigned seconds = 0; seconds < 30; seconds += 5) {
      DWORD result = WaitForSingleObject(shutdown_worker, 5000);
      if (result == WAIT_OBJECT_0) {
        CloseHandle(shutdown_worker); shutdown_worker = nullptr;
        if (shutdown_result == NVSDK_NGX_Result_Success) { initialized = false; return true; }
        finish_failed = true;
        retain_until_process_exit("NGX shutdown returned failure");
        return false;
      }
      std::printf("core shutdown pending %u seconds (bounded at 30 seconds)\n", seconds + 5);
      std::fflush(stdout);
      if (result == WAIT_FAILED) break;
    }
    shutdown_stalled = true;
    std::fprintf(stderr, "core NGX shutdown probe did not complete; no second shutdown call, no runtime unload\n");
    retain_until_process_exit("30-second shutdown probe stalled; retain active worker and apply process-exit no-op policy");
    return false;
  }

  ~CoreSession() {
    if (process_lifetime_retention) {
      std::fprintf(stderr, "retained CoreSession must exit before stack unwinding; terminating without unsafe teardown\n");
      TerminateProcess(GetCurrentProcess(), 2);
    }
    if (vk && initialized && context) {
      // Exception path: no successful explicit finish. Keep failure visible and
      // preserve resources if even the bounded real-shutdown probe cannot finish.
      if (!finish_vulkan(true)) TerminateProcess(GetCurrentProcess(), 2);
    } else {
      destroy_parameters();
      if (initialized && context) {
        HANDLE thread = CreateThread(nullptr, 0, shutdown_thread, this, 0, nullptr);
        if (!thread || WaitForSingleObject(thread, 30000) != WAIT_OBJECT_0) {
          std::fprintf(stderr, "BLOCKED NGX shutdown stalled; terminating process without unloading an active runtime\n");
          TerminateProcess(GetCurrentProcess(), 2);
        }
        CloseHandle(thread);
      }
    }
    AcquireSRWLockExclusive(&callback_lock);
    if (callback_file == own_log) callback_file = nullptr;
    if (own_log) std::fclose(own_log);
    ReleaseSRWLockExclusive(&callback_lock);
    // Keep the unique, hash-identical scratch payload and logs for inspection. Never clean
    // a runtime/reference directory, and never copy this file into a tracked path.
  }
};
