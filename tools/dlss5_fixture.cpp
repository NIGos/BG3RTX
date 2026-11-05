// Standalone native NR feasibility fixture. No layer/game linkage or implicit layers.
// Allocation/import adapted from dlss5-bridge d1cc508 src/synth.inc
// (Synth12MakeTexture, SynthVkImport): Copyright (c) 2026 NIGos, MIT.
// Full notice: docs/release/licences/DLSS5-bridge-MIT.txt.
// NR parameter/codec sequence adapted from the user's DCS Control dlss_nr.cpp.
// Synchronization deliberately replaces the bridge's event park with submit boundaries.
#undef VK_NO_PROTOTYPES
#include <windows.h>
#include <vulkan/vulkan.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <wintrust.h>
#include <softpub.h>
#include <bcrypt.h>
#include <tlhelp32.h>
#include <nvsdk_ngx_params.h>
#include <nvsdk_ngx.h>
#include <nvsdk_ngx_vk.h>
#include <mutex>
#include <memory>
#include <chrono>
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>
#include "dlss5_fixture_logic.h"
#include "dlss5_nr_codec.h"
#include "../common/dlss5_nr_frame.h"

using Microsoft::WRL::ComPtr;
namespace fs = std::filesystem;
namespace nr = bg3::dlss5;
namespace {
constexpr uint64_t timeout_ns = 20'000'000'000ull;
void need(bool ok, const char* what) { if (!ok) throw std::runtime_error(what); }
void vkcheck(VkResult r, const char* what) {
  if (r != VK_SUCCESS) throw std::runtime_error(std::string(what) + " VkResult=" + std::to_string(int(r)));
}
void hrcheck(HRESULT r, const char* what) {
  if (FAILED(r)) { char s[32]; std::snprintf(s, sizeof s, " 0x%08x", unsigned(r)); throw std::runtime_error(std::string(what) + s); }
}
void ngxcheck(unsigned r, const char* what) {
  if (r != 1) { char s[128]; std::snprintf(s, sizeof s, "%s result=0x%08x; STOP: no identity spoof or runtime modification attempted", what, r); throw std::runtime_error(s); }
}
void no_game() {
  HANDLE h = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
  need(h != INVALID_HANDLE_VALUE, "process inventory unavailable");
  PROCESSENTRY32W p{sizeof p}; bool found = false;
  for (BOOL ok = Process32FirstW(h, &p); ok; ok = Process32NextW(h, &p))
    found |= !_wcsicmp(p.szExeFile, L"bg3.exe") || !_wcsicmp(p.szExeFile, L"bg3_dx11.exe");
  CloseHandle(h); need(!found, "BG3 is running: GPU step refused");
}
std::atomic<unsigned> validation_errors{};
std::atomic<unsigned> known_nr_validation_errors{};
VKAPI_ATTR VkBool32 VKAPI_CALL debug_callback(VkDebugUtilsMessageSeverityFlagBitsEXT severity,
  VkDebugUtilsMessageTypeFlagsEXT, const VkDebugUtilsMessengerCallbackDataEXT* data, void*) {
  if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) {
    ++validation_errors;
    if(data->pMessageIdName && !std::strcmp(data->pMessageIdName,"VUID-vkCmdDraw-None-09600") &&
       std::strstr(data->pMessage,"nv.ngx.dlssnr.resource"))++known_nr_validation_errors;
    std::fprintf(stderr, "VALIDATION %s\n", data->pMessage);
  }
  return VK_FALSE;
}
struct RuntimeFile {
  fs::path path;
  HANDLE lock = INVALID_HANDLE_VALUE;
  HMODULE module{};
  std::array<uint8_t, 32> digest{};
  RuntimeFile() = default;
  RuntimeFile(const RuntimeFile&) = delete;
  ~RuntimeFile() { if (module) FreeLibrary(module); if (lock != INVALID_HANDLE_VALUE) CloseHandle(lock); }
  void verify(const fs::path& input, bool model) {
    path = fs::canonical(input);
    // Keep the verified bytes immutable through LoadLibrary and all evaluations.
    lock = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    need(lock != INVALID_HANDLE_VALUE, "cannot lock runtime for verified read-only loading");
    WINTRUST_FILE_INFO file{sizeof file}; file.pcwszFilePath = path.c_str(); file.hFile = lock;
    WINTRUST_DATA trust{sizeof trust}; trust.dwUIChoice = WTD_UI_NONE; trust.fdwRevocationChecks = WTD_REVOKE_NONE;
    trust.dwUnionChoice = WTD_CHOICE_FILE; trust.pFile = &file; trust.dwStateAction = WTD_STATEACTION_VERIFY;
    trust.dwProvFlags = WTD_CACHE_ONLY_URL_RETRIEVAL; // Offline: never retrieve certificates from network.
    GUID action = WINTRUST_ACTION_GENERIC_VERIFY_V2;
    LONG status = WinVerifyTrust(nullptr, &action, &trust);
    std::wstring organization;
    if (trust.hWVTStateData) {
      auto* provider = WTHelperProvDataFromStateData(trust.hWVTStateData);
      auto* signer = provider ? WTHelperGetProvSignerFromChain(provider, 0, FALSE, 0) : nullptr;
      if (signer && signer->csCertChain) {
        wchar_t name[256]{};
        CertGetNameStringW(signer->pasCertChain[0].pCert, CERT_NAME_ATTR_TYPE, 0,
                          const_cast<char*>(szOID_ORGANIZATION_NAME), name, 256);
        organization = name;
      }
    }
    trust.dwStateAction = WTD_STATEACTION_CLOSE; WinVerifyTrust(nullptr, &action, &trust);
    std::printf("runtime path=%ls trust=0x%08x organization=%ls\n", path.c_str(), unsigned(status), organization.c_str());
    std::printf("runtime signature_status=%s model_policy=%s\n",status==ERROR_SUCCESS?"Valid":status==TRUST_E_BAD_DIGEST?"HashMismatch":status==TRUST_E_NOSIGNATURE?"Unsigned":"OtherFailure",model?"accept-and-log":"require-NVIDIA-signed-core");
    if (!model) need(status == ERROR_SUCCESS && organization == L"NVIDIA Corporation", "NGX core rejected: valid NVIDIA signature required");
    DWORD dummy{}; DWORD bytes = GetFileVersionInfoSizeW(path.c_str(), &dummy);
    need(bytes != 0, "runtime has no version resource");
    std::vector<uint8_t> v(bytes); need(GetFileVersionInfoW(path.c_str(), 0, bytes, v.data()) != 0, "runtime version read");
    VS_FIXEDFILEINFO* info{}; UINT size{};
    need(VerQueryValueW(v.data(), L"\\", reinterpret_cast<void**>(&info), &size) && size >= sizeof(*info), "runtime version structure");
    std::printf("runtime version=%u.%u.%u.%u\n", HIWORD(info->dwFileVersionMS), LOWORD(info->dwFileVersionMS),
                HIWORD(info->dwFileVersionLS), LOWORD(info->dwFileVersionLS));
    if (model) need(HIWORD(info->dwFileVersionMS) == 310 &&
                   (LOWORD(info->dwFileVersionMS) == 8 || LOWORD(info->dwFileVersionMS) == 9), "NR contract requires 310.8/310.9");
    LARGE_INTEGER start{}; need(SetFilePointerEx(lock, start, nullptr, FILE_BEGIN) != 0, "PE header seek");
    IMAGE_DOS_HEADER dos{}; DWORD pe_read{};
    need(ReadFile(lock, &dos, sizeof dos, &pe_read, nullptr) && pe_read == sizeof dos &&
         dos.e_magic == IMAGE_DOS_SIGNATURE && dos.e_lfanew > 0 && dos.e_lfanew < 1024 * 1024, "runtime DOS header");
    LARGE_INTEGER nt_offset{}; nt_offset.QuadPart = dos.e_lfanew;
    need(SetFilePointerEx(lock, nt_offset, nullptr, FILE_BEGIN) != 0, "NT header seek");
    IMAGE_NT_HEADERS64 nt{};
    need(ReadFile(lock, &nt, sizeof nt, &pe_read, nullptr) && pe_read == sizeof nt &&
         nt.Signature == IMAGE_NT_SIGNATURE && nt.FileHeader.Machine == IMAGE_FILE_MACHINE_AMD64 &&
         nt.OptionalHeader.Magic == IMAGE_NT_OPTIONAL_HDR64_MAGIC, "runtime must be an x64 PE DLL");
    need(SetFilePointerEx(lock, start, nullptr, FILE_BEGIN) != 0, "SHA256 rewind");
    BCRYPT_ALG_HANDLE alg{}; BCRYPT_HASH_HANDLE hash{};
    need(BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, nullptr, 0) >= 0, "SHA256 provider");
    DWORD object_size{}, got{};
    need(BCryptGetProperty(alg, BCRYPT_OBJECT_LENGTH, reinterpret_cast<PUCHAR>(&object_size), sizeof object_size, &got, 0) >= 0, "SHA256 object");
    std::vector<uint8_t> object(object_size), block(1 << 20);
    need(BCryptCreateHash(alg, &hash, object.data(), object_size, nullptr, 0, 0) >= 0, "SHA256 create");
    DWORD read{};
    for (;;) {
      need(ReadFile(lock, block.data(), DWORD(block.size()), &read, nullptr) != 0, "runtime hash read");
      if (!read) break;
      need(BCryptHashData(hash, block.data(), read, 0) >= 0, "SHA256 data");
    }
    need(BCryptFinishHash(hash, digest.data(), DWORD(digest.size()), 0) >= 0, "SHA256 finish");
    BCryptDestroyHash(hash); BCryptCloseAlgorithmProvider(alg, 0);
    std::printf("runtime sha256="); for (auto b : digest) std::printf("%02x", b); std::puts("");
  }
  void load() {
    module = LoadLibraryExW(path.c_str(), nullptr, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
    need(module != nullptr, "signed runtime LoadLibrary failed; STOP, no loader identity workaround");
  }
  template<class T> T proc(const char* name) { return reinterpret_cast<T>(GetProcAddress(module, name)); }
};

struct Context {
  VkInstance instance{}; VkPhysicalDevice physical{}; VkDevice device{}; VkQueue queue{};
  uint32_t family{}; VkPhysicalDeviceMemoryProperties memory{}; float tick{};
  VkCommandPool pool{}; VkCommandBuffer a{}, b{}; VkFence produced{}, completed{}; VkQueryPool queries{};
  bool a_pending{}, b_pending{}; bool memory_budget{};
  VkSemaphore timeline{}; VkDebugUtilsMessengerEXT messenger{};
  ComPtr<ID3D12Device> d12; ComPtr<ID3D12CommandQueue> q12;
  ComPtr<ID3D12CommandAllocator> allocator; ComPtr<ID3D12GraphicsCommandList> list;
  ComPtr<ID3D12Fence> fence12, local12; uint64_t local_value{};
  ComPtr<ID3D12QueryHeap> queries12; ComPtr<ID3D12Resource> times12; uint64_t frequency{};
  PFN_vkGetMemoryWin32HandlePropertiesKHR memory_properties{};
  void init(bool validation, const NVSDK_NGX_FeatureCommonInfo* nr_info = nullptr, const wchar_t* ngx_data = nullptr) {
    no_game();
    SetEnvironmentVariableA("VK_LOADER_LAYERS_DISABLE", "~implicit~");
    SetEnvironmentVariableA("VK_INSTANCE_LAYERS", nullptr); SetEnvironmentVariableA("VK_LOADER_LAYERS_ENABLE", nullptr);
    SetEnvironmentVariableA("VK_ADD_LAYER_PATH", nullptr);
    uint32_t count{}; vkcheck(vkEnumerateInstanceLayerProperties(&count, nullptr), "instance layers");
    std::vector<VkLayerProperties> layers(count); vkcheck(vkEnumerateInstanceLayerProperties(&count, layers.data()), "instance layers");
    bool have = false; for (auto& x : layers) have |= !std::strcmp(x.layerName, "VK_LAYER_KHRONOS_validation");
    need(!validation || have, "validation layer missing: install it or explicitly use --no-validation");
    VkDebugUtilsMessengerCreateInfoEXT debug{VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT};
    debug.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
    debug.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT;
    debug.pfnUserCallback = debug_callback;
    const char* layer = "VK_LAYER_KHRONOS_validation"; const char* ext = VK_EXT_DEBUG_UTILS_EXTENSION_NAME;
    VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO}; app.pApplicationName = "BG3RTX DLSS5 fixture"; app.apiVersion = VK_API_VERSION_1_2;
    VkInstanceCreateInfo ici{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO}; ici.pApplicationInfo = &app;
    if (validation) { ici.pNext = &debug; ici.enabledLayerCount = 1; ici.ppEnabledLayerNames = &layer; ici.enabledExtensionCount = 1; ici.ppEnabledExtensionNames = &ext; }
    vkcheck(vkCreateInstance(&ici, nullptr, &instance), "instance");
    if (validation) {
      auto create = reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(vkGetInstanceProcAddr(instance, "vkCreateDebugUtilsMessengerEXT"));
      vkcheck(create(instance, &debug, nullptr, &messenger), "debug messenger");
    }
    vkcheck(vkEnumeratePhysicalDevices(instance, &count, nullptr), "physical devices");
    std::vector<VkPhysicalDevice> devices(count); vkcheck(vkEnumeratePhysicalDevices(instance, &count, devices.data()), "physical devices");
    ComPtr<IDXGIFactory4> factory; hrcheck(CreateDXGIFactory1(IID_PPV_ARGS(&factory)), "DXGI factory");
    for (auto candidate : devices) {
      VkPhysicalDeviceIDProperties id{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ID_PROPERTIES};
      VkPhysicalDeviceProperties2 props{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2, &id}; vkGetPhysicalDeviceProperties2(candidate, &props);
      if (!id.deviceLUIDValid || props.properties.vendorID != 0x10de || props.properties.apiVersion < VK_API_VERSION_1_2) continue;
      nr::Luid vk_id{}; std::memcpy(vk_id.data(), id.deviceLUID, 8);
      for (UINT i = 0;; ++i) {
        ComPtr<IDXGIAdapter1> adapter; if (factory->EnumAdapters1(i, &adapter) == DXGI_ERROR_NOT_FOUND) break;
        DXGI_ADAPTER_DESC1 desc{}; hrcheck(adapter->GetDesc1(&desc), "adapter description");
        nr::Luid dx_id{}; std::memcpy(dx_id.data(), &desc.AdapterLuid, 8);
        if (!nr::same_luid(vk_id, id.deviceLUIDValid != 0, dx_id)) continue;
        if (!nr_info) hrcheck(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_12_0, IID_PPV_ARGS(&d12)), "D3D12 same-adapter device");
        physical = candidate; tick = props.properties.limits.timestampPeriod;
        std::printf("adapter=%s DXGI=%ls LUID=", props.properties.deviceName, desc.Description);
        for (auto v : vk_id) std::printf("%02x", v);
        std::printf(" nodeMask=%u validation=%d implicit_layers=disabled\n", id.deviceNodeMask, int(validation));
        break;
      }
      if (physical) break;
    }
    need(physical != VK_NULL_HANDLE, "no NVIDIA Vulkan/D3D12 device with matching valid LUID");
    std::vector<const char*> extensions;
    if (!nr_info) extensions = {VK_KHR_EXTERNAL_MEMORY_WIN32_EXTENSION_NAME, VK_KHR_EXTERNAL_SEMAPHORE_WIN32_EXTENSION_NAME};
    if (nr_info) {
      NVSDK_NGX_FeatureDiscoveryInfo discovery{}; discovery.SDKVersion = NVSDK_NGX_Version_API;
      discovery.FeatureID = static_cast<NVSDK_NGX_Feature>(18); discovery.Identifier.IdentifierType = NVSDK_NGX_Application_Identifier_Type_Application_Id;
      discovery.Identifier.v.ApplicationId = 0x0876232cULL; discovery.FeatureInfo = nr_info;
      discovery.ApplicationDataPath = ngx_data;
      uint32_t required_count{}; VkExtensionProperties* required{};
      auto result = NVSDK_NGX_VULKAN_GetFeatureDeviceExtensionRequirements(instance, physical, &discovery, &required_count, &required);
      std::printf("NGX Vulkan core GetFeatureDeviceExtensionRequirements(18)=0x%08x count=%u\n", unsigned(result), required_count);
      if (result == NVSDK_NGX_Result_Success) {
        for (uint32_t i = 0; i < required_count; ++i) { extensions.push_back(required[i].extensionName); std::printf("NR required extension=%s\n", required[i].extensionName); }
      } else {
        // Previous feature-18 discovery supplied these names. Still validate device support.
        extensions = {"VK_NVX_binary_import", "VK_NVX_image_view_handle", "VK_KHR_buffer_device_address", "VK_KHR_push_descriptor"};
        std::puts("NR discovery failed: trying previously observed feature-18 extension set, not claiming it is exhaustive");
      }
    }
    vkcheck(vkEnumerateDeviceExtensionProperties(physical, nullptr, &count, nullptr), "device extensions");
    std::vector<VkExtensionProperties> available(count); vkcheck(vkEnumerateDeviceExtensionProperties(physical, nullptr, &count, available.data()), "device extensions");
    for (auto& x : available) if (!std::strcmp(x.extensionName,VK_EXT_MEMORY_BUDGET_EXTENSION_NAME)) { extensions.push_back(VK_EXT_MEMORY_BUDGET_EXTENSION_NAME);memory_budget=true;break; }
    for (auto name : extensions) { bool found = false; for (auto& x : available) found |= !std::strcmp(name, x.extensionName); need(found, name); }
    VkPhysicalDeviceTimelineSemaphoreFeatures tf{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TIMELINE_SEMAPHORE_FEATURES};
    VkPhysicalDeviceBufferDeviceAddressFeatures bda{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BUFFER_DEVICE_ADDRESS_FEATURES};
    if (nr_info) tf.pNext = &bda;
    VkPhysicalDeviceFeatures2 f2{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2, &tf}; vkGetPhysicalDeviceFeatures2(physical, &f2);
    need(tf.timelineSemaphore != 0, "timeline semaphore unsupported");
    if (nr_info) need(bda.bufferDeviceAddress != 0, "NR bufferDeviceAddress unsupported");
    uint32_t nq{}; vkGetPhysicalDeviceQueueFamilyProperties(physical, &nq, nullptr);
    std::vector<VkQueueFamilyProperties> qs(nq); vkGetPhysicalDeviceQueueFamilyProperties(physical, &nq, qs.data());
    family = UINT32_MAX;
    for (uint32_t i = 0; i < nq; ++i) if ((qs[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) && qs[i].timestampValidBits == 64) { family = i; break; }
    need(family != UINT32_MAX, "graphics queue with 64-bit timestamps required");
    float priority = 0; VkDeviceQueueCreateInfo qci{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
    qci.queueFamilyIndex = family; qci.queueCount = 1; qci.pQueuePriorities = &priority;
    VkDeviceCreateInfo dci{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO, &tf}; dci.queueCreateInfoCount = 1; dci.pQueueCreateInfos = &qci;
    dci.enabledExtensionCount = uint32_t(extensions.size()); dci.ppEnabledExtensionNames = extensions.data();
    VkPhysicalDeviceFeatures enabled{};
    if (nr_info) {
      need(f2.features.shaderStorageImageExtendedFormats != 0, "Vulkan NR codec extended storage image formats");
      enabled.shaderStorageImageExtendedFormats = VK_TRUE; enabled.shaderInt64 = f2.features.shaderInt64;
      need(f2.features.shaderStorageImageReadWithoutFormat && f2.features.shaderStorageImageWriteWithoutFormat,
           "NR storage image read/write without format unsupported");
      enabled.shaderStorageImageReadWithoutFormat = VK_TRUE;
      enabled.shaderStorageImageWriteWithoutFormat = VK_TRUE;
      dci.pEnabledFeatures = &enabled;
    }
    vkcheck(vkCreateDevice(physical, &dci, nullptr, &device), "Vulkan device"); vkGetDeviceQueue(device, family, 0, &queue);
    vkGetPhysicalDeviceMemoryProperties(physical, &memory);
    memory_properties = reinterpret_cast<PFN_vkGetMemoryWin32HandlePropertiesKHR>(vkGetDeviceProcAddr(device, "vkGetMemoryWin32HandlePropertiesKHR"));
    need(nr_info || memory_properties != nullptr, "Win32 memory properties export");
    VkCommandPoolCreateInfo pci{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO}; pci.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT; pci.queueFamilyIndex = family;
    vkcheck(vkCreateCommandPool(device, &pci, nullptr, &pool), "command pool");
    VkCommandBufferAllocateInfo ai{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO}; ai.commandPool = pool; ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY; ai.commandBufferCount = 2;
    VkCommandBuffer commands[2]; vkcheck(vkAllocateCommandBuffers(device, &ai, commands), "command buffers"); a = commands[0]; b = commands[1];
    VkFenceCreateInfo fc{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO}; vkcheck(vkCreateFence(device, &fc, nullptr, &completed), "completion fence");
    vkcheck(vkCreateFence(device, &fc, nullptr, &produced), "producer completion fence");
    VkQueryPoolCreateInfo qc{VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO}; qc.queryType = VK_QUERY_TYPE_TIMESTAMP; qc.queryCount = 6;
    vkcheck(vkCreateQueryPool(device, &qc, nullptr, &queries), "Vulkan queries");
    if (nr_info) { std::puts("Vulkan core fixture: no D3D12 device, external-memory images or shared semaphore"); return; }
    D3D12_COMMAND_QUEUE_DESC q{}; q.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    hrcheck(d12->CreateCommandQueue(&q, IID_PPV_ARGS(&q12)), "D3D12 queue");
    hrcheck(d12->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocator)), "D3D12 allocator");
    hrcheck(d12->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator.Get(), nullptr, IID_PPV_ARGS(&list)), "D3D12 list");
    hrcheck(list->Close(), "initial list close");
    hrcheck(d12->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&local12)), "private completion fence");
    hrcheck(d12->CreateFence(0, D3D12_FENCE_FLAG_SHARED, IID_PPV_ARGS(&fence12)), "shared D3D12 fence");
    VkSemaphoreTypeCreateInfo semtype{VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO}; semtype.semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE;
    VkPhysicalDeviceExternalSemaphoreInfo sinfo{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTERNAL_SEMAPHORE_INFO, &semtype};
    sinfo.handleType = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_D3D12_FENCE_BIT;
    VkExternalSemaphoreProperties sprops{VK_STRUCTURE_TYPE_EXTERNAL_SEMAPHORE_PROPERTIES}; vkGetPhysicalDeviceExternalSemaphoreProperties(physical, &sinfo, &sprops);
    need((sprops.externalSemaphoreFeatures & VK_EXTERNAL_SEMAPHORE_FEATURE_IMPORTABLE_BIT) != 0, "D3D12 timeline fence not importable");
    VkSemaphoreCreateInfo sc{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO, &semtype}; vkcheck(vkCreateSemaphore(device, &sc, nullptr, &timeline), "timeline semaphore");
    HANDLE shared{}; hrcheck(d12->CreateSharedHandle(fence12.Get(), nullptr, GENERIC_ALL, nullptr, &shared), "shared fence handle");
    auto import = reinterpret_cast<PFN_vkImportSemaphoreWin32HandleKHR>(vkGetDeviceProcAddr(device, "vkImportSemaphoreWin32HandleKHR"));
    VkImportSemaphoreWin32HandleInfoKHR ii{VK_STRUCTURE_TYPE_IMPORT_SEMAPHORE_WIN32_HANDLE_INFO_KHR}; ii.semaphore = timeline;
    ii.handleType = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_D3D12_FENCE_BIT; ii.handle = shared;
    VkResult result = import(device, &ii); CloseHandle(shared); vkcheck(result, "timeline fence import");
    D3D12_QUERY_HEAP_DESC qh{}; qh.Type = D3D12_QUERY_HEAP_TYPE_TIMESTAMP; qh.Count = 6;
    hrcheck(d12->CreateQueryHeap(&qh, IID_PPV_ARGS(&queries12)), "D3D12 timestamp heap");
    D3D12_HEAP_PROPERTIES hp{}; hp.Type = D3D12_HEAP_TYPE_READBACK;
    D3D12_RESOURCE_DESC rd{}; rd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER; rd.Width = 6 * sizeof(uint64_t); rd.Height = 1;
    rd.DepthOrArraySize = rd.MipLevels = 1; rd.SampleDesc.Count = 1; rd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    hrcheck(d12->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&times12)), "timestamp readback");
    hrcheck(q12->GetTimestampFrequency(&frequency), "D3D12 timestamp frequency");
  }
  uint32_t memory_type(uint32_t bits, VkMemoryPropertyFlags flags) {
    if (flags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT)
      for(uint32_t i=0;i<memory.memoryTypeCount;++i)
        if((bits&(1u<<i)) && (memory.memoryTypes[i].propertyFlags & (flags|VK_MEMORY_PROPERTY_HOST_CACHED_BIT))==(flags|VK_MEMORY_PROPERTY_HOST_CACHED_BIT)) return i;
    for (uint32_t i = 0; i < memory.memoryTypeCount; ++i)
      if ((bits & (1u << i)) && (memory.memoryTypes[i].propertyFlags & flags) == flags) return i;
    throw std::runtime_error("compatible memory type unavailable");
  }
  void begin12() { hrcheck(allocator->Reset(), "allocator reset"); hrcheck(list->Reset(allocator.Get(), nullptr), "list reset"); }
  void execute12() { hrcheck(list->Close(), "list close"); ID3D12CommandList* lists[]{list.Get()}; q12->ExecuteCommandLists(1, lists); }
  void wait12() {
    hrcheck(q12->Signal(local12.Get(), ++local_value), "local completion signal");
    HANDLE event = CreateEventW(nullptr, FALSE, FALSE, nullptr); need(event != nullptr, "completion event");
    hrcheck(local12->SetEventOnCompletion(local_value, event), "local completion event");
    auto result = WaitForSingleObject(event, 20000); CloseHandle(event);
    if (result != WAIT_OBJECT_0) {
      std::fprintf(stderr, "GPU timeout: terminate without releasing in-flight D3D12 resources\n");
      TerminateProcess(GetCurrentProcess(), 2);
    }
    hrcheck(d12->GetDeviceRemovedReason(), "D3D12 removed reason");
  }
  void wait_vulkan() {
    for (auto pair : {std::pair{produced, &a_pending}, std::pair{completed, &b_pending}}) {
      if (!*pair.second) continue;
      auto r = vkWaitForFences(device, 1, &pair.first, VK_TRUE, timeout_ns);
      if (r != VK_SUCCESS) {
        std::fprintf(stderr, "GPU drain result=%d: terminate without unsafe Vulkan teardown\n", int(r));
        TerminateProcess(GetCurrentProcess(), 2);
      }
      *pair.second = false;
    }
  }
  ~Context() {
    // Successful and ordinary failure paths drain before destruction. On a stuck driver
    // the process is terminated instead of releasing resources still named by the GPU.
    if (device) wait_vulkan();
    if (q12) { try { wait12(); } catch (...) { std::fprintf(stderr, "GPU drain blocked; terminating without unsafe teardown\n"); TerminateProcess(GetCurrentProcess(), 2); } }
    if (device) {
      if (queries) vkDestroyQueryPool(device, queries, nullptr); if (timeline) vkDestroySemaphore(device, timeline, nullptr);
      if (completed) vkDestroyFence(device, completed, nullptr); if (produced) vkDestroyFence(device, produced, nullptr);
      if (pool) vkDestroyCommandPool(device, pool, nullptr);
      vkDestroyDevice(device, nullptr);
    }
    if (messenger) reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(vkGetInstanceProcAddr(instance, "vkDestroyDebugUtilsMessengerEXT"))(instance, messenger, nullptr);
    if (instance) vkDestroyInstance(instance, nullptr);
  }
};
#include "dlss5_core_session.h"
#include "dlss5_fixture_telemetry.h"
struct Image {
  Context& c; ComPtr<ID3D12Resource> resource; VkImage image{}; VkDeviceMemory memory{}; VkFormat format{}; uint32_t bytes{};
  Image(Context& ctx, uint32_t w, uint32_t h, VkFormat vf, DXGI_FORMAT df, uint32_t bpp) : c(ctx), format(vf), bytes(bpp) {
    D3D12_HEAP_PROPERTIES hp{}; hp.Type = D3D12_HEAP_TYPE_DEFAULT;
    D3D12_RESOURCE_DESC rd{}; rd.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D; rd.Width = w; rd.Height = h;
    rd.DepthOrArraySize = rd.MipLevels = 1; rd.Format = df; rd.SampleDesc.Count = 1;
    rd.Flags = D3D12_RESOURCE_FLAG_ALLOW_SIMULTANEOUS_ACCESS | D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    hrcheck(c.d12->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_SHARED, &rd, D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(&resource)), "shared texture");
    VkPhysicalDeviceExternalImageFormatInfo external{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTERNAL_IMAGE_FORMAT_INFO};
    external.handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_D3D12_RESOURCE_BIT;
    VkPhysicalDeviceImageFormatInfo2 query{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_IMAGE_FORMAT_INFO_2, &external};
    query.format = vf; query.type = VK_IMAGE_TYPE_2D; query.tiling = VK_IMAGE_TILING_OPTIMAL;
    query.usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    VkExternalImageFormatProperties props{VK_STRUCTURE_TYPE_EXTERNAL_IMAGE_FORMAT_PROPERTIES};
    VkImageFormatProperties2 fp{VK_STRUCTURE_TYPE_IMAGE_FORMAT_PROPERTIES_2, &props};
    vkcheck(vkGetPhysicalDeviceImageFormatProperties2(c.physical, &query, &fp), "external image format");
    need((props.externalMemoryProperties.externalMemoryFeatures & VK_EXTERNAL_MEMORY_FEATURE_IMPORTABLE_BIT) != 0, "D3D12 resource format not importable");
    need(w <= fp.imageFormatProperties.maxExtent.width && h <= fp.imageFormatProperties.maxExtent.height, "shared image dimensions exceed limit");
    VkExternalMemoryImageCreateInfo ext{VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO}; ext.handleTypes = external.handleType;
    VkImageCreateInfo ici{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO, &ext}; ici.imageType = VK_IMAGE_TYPE_2D; ici.format = vf;
    ici.extent = {w, h, 1}; ici.mipLevels = ici.arrayLayers = 1; ici.samples = VK_SAMPLE_COUNT_1_BIT;
    ici.tiling = VK_IMAGE_TILING_OPTIMAL; ici.usage = query.usage;
    vkcheck(vkCreateImage(c.device, &ici, nullptr, &image), "import VkImage");
    HANDLE handle{}; hrcheck(c.d12->CreateSharedHandle(resource.Get(), nullptr, GENERIC_ALL, nullptr, &handle), "image handle");
    VkMemoryRequirements req{}; vkGetImageMemoryRequirements(c.device, image, &req);
    VkMemoryWin32HandlePropertiesKHR hpv{VK_STRUCTURE_TYPE_MEMORY_WIN32_HANDLE_PROPERTIES_KHR};
    VkResult r = c.memory_properties(c.device, external.handleType, handle, &hpv);
    if (r != VK_SUCCESS) { CloseHandle(handle); vkcheck(r, "imported memory properties"); }
    VkMemoryDedicatedAllocateInfo ded{VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO}; ded.image = image;
    VkImportMemoryWin32HandleInfoKHR imp{VK_STRUCTURE_TYPE_IMPORT_MEMORY_WIN32_HANDLE_INFO_KHR, &ded}; imp.handleType = external.handleType; imp.handle = handle;
    VkMemoryAllocateInfo alloc{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, &imp}; alloc.allocationSize = req.size;
    alloc.memoryTypeIndex = c.memory_type(req.memoryTypeBits & hpv.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    r = vkAllocateMemory(c.device, &alloc, nullptr, &memory); CloseHandle(handle); vkcheck(r, "import image memory");
    vkcheck(vkBindImageMemory(c.device, image, memory, 0), "bind imported image");
  }
  Image(const Image&) = delete;
  ~Image() { if (image) vkDestroyImage(c.device, image, nullptr); if (memory) vkFreeMemory(c.device, memory, nullptr); }
};
struct Buffer {
  Context& c; VkBuffer buffer{}; VkDeviceMemory memory{}; void* mapped{};
  Buffer(Context& ctx, VkDeviceSize bytes, VkBufferUsageFlags usage, bool local = false) : c(ctx) {
    VkBufferCreateInfo bi{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO}; bi.size = bytes; bi.usage = usage;
    vkcheck(vkCreateBuffer(c.device, &bi, nullptr, &buffer), "staging buffer");
    VkMemoryRequirements req; vkGetBufferMemoryRequirements(c.device, buffer, &req);
    VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO}; ai.allocationSize = req.size;
    ai.memoryTypeIndex = c.memory_type(req.memoryTypeBits, local ? VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT :
      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    std::printf("buffer bytes=%llu local=%d memory_type=%u flags=0x%x heap=%u\n",static_cast<unsigned long long>(bytes),int(local),ai.memoryTypeIndex,c.memory.memoryTypes[ai.memoryTypeIndex].propertyFlags,c.memory.memoryTypes[ai.memoryTypeIndex].heapIndex);
    vkcheck(vkAllocateMemory(c.device, &ai, nullptr, &memory), "staging memory");
    vkcheck(vkBindBufferMemory(c.device, buffer, memory, 0), "staging bind");
    if (!local) vkcheck(vkMapMemory(c.device, memory, 0, VK_WHOLE_SIZE, 0, &mapped), "staging map");
  }
  ~Buffer() { if (mapped) vkUnmapMemory(c.device, memory); vkDestroyBuffer(c.device, buffer, nullptr); vkFreeMemory(c.device, memory, nullptr); }
};
#include "dlss5_vulkan_codec.h"
void image_barrier(Context& c, VkCommandBuffer cb, Image& im, bool acquire, bool initial = false) {
  VkImageMemoryBarrier b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
  b.oldLayout = initial ? VK_IMAGE_LAYOUT_UNDEFINED : VK_IMAGE_LAYOUT_GENERAL; b.newLayout = VK_IMAGE_LAYOUT_GENERAL;
  b.srcQueueFamilyIndex = acquire ? VK_QUEUE_FAMILY_EXTERNAL : c.family;
  b.dstQueueFamilyIndex = acquire ? c.family : VK_QUEUE_FAMILY_EXTERNAL;
  b.srcAccessMask = acquire ? 0 : VK_ACCESS_TRANSFER_WRITE_BIT | VK_ACCESS_TRANSFER_READ_BIT;
  b.dstAccessMask = acquire ? VK_ACCESS_TRANSFER_WRITE_BIT | VK_ACCESS_TRANSFER_READ_BIT : 0;
  b.image = im.image; b.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
  vkCmdPipelineBarrier(cb, acquire ? VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT : VK_PIPELINE_STAGE_TRANSFER_BIT,
    acquire ? VK_PIPELINE_STAGE_TRANSFER_BIT : VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0, 0, nullptr, 0, nullptr, 1, &b);
}
void transition(ID3D12GraphicsCommandList* list, ID3D12Resource* resource, D3D12_RESOURCE_STATES from, D3D12_RESOURCE_STATES to) {
  D3D12_RESOURCE_BARRIER b{}; b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
  b.Transition = {resource, D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, from, to}; list->ResourceBarrier(1, &b);
}
ComPtr<ID3D12Resource> texture(Context& c, uint32_t w, uint32_t h, DXGI_FORMAT fmt) {
  D3D12_HEAP_PROPERTIES hp{}; hp.Type = D3D12_HEAP_TYPE_DEFAULT;
  D3D12_RESOURCE_DESC rd{}; rd.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D; rd.Width = w; rd.Height = h;
  rd.DepthOrArraySize = rd.MipLevels = 1; rd.Format = fmt; rd.SampleDesc.Count = 1; rd.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
  ComPtr<ID3D12Resource> r; hrcheck(c.d12->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd, D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(&r)), "private texture"); return r;
}

// One full-frame DCS NR evaluator. No gaze/crop/SR or identity-patching machinery.
struct Neural {
  Context& c; RuntimeFile runtime, core;
  using Params = NVSDK_NGX_Parameter;
  using Allocate = unsigned (*)(Params**); using Destroy = unsigned (*)(Params*);
  using Create = unsigned (*)(ID3D12GraphicsCommandList*, unsigned, Params*, void**);
  using Evaluate = unsigned (*)(ID3D12GraphicsCommandList*, const void*, const Params*, void*);
  using Release = unsigned (*)(void*);
  Params* params{}; void* feature{}; Destroy destroy{}; Evaluate evaluate{}; Release release{};
  ComPtr<ID3D12Resource> original, proxy, output;
  ComPtr<ID3D12DescriptorHeap> descriptors; ComPtr<ID3D12RootSignature> root;
  ComPtr<ID3D12PipelineState> encode, decode; uint32_t stride{}, w{}, h{};
  Neural(Context& ctx) : c(ctx) {}
  void init(const fs::path& path, const fs::path& core_path, uint32_t width, uint32_t height, Image& color, Image& dest, CoreSession* core_session = nullptr) {
    w = width; h = height;
    Create create{};
    if (core_session) {
      params = core_session->params;
      create = [](ID3D12GraphicsCommandList* cmd, unsigned id, Params* p, void** handle) -> unsigned {
        auto r=NVSDK_NGX_D3D12_CreateFeature(cmd,static_cast<NVSDK_NGX_Feature>(id),p,reinterpret_cast<NVSDK_NGX_Handle**>(handle));
        CoreSession::log_result("NVSDK_NGX_D3D12_CreateFeature(18)",r);return unsigned(r);
      };
      evaluate=[](ID3D12GraphicsCommandList* cmd,const void* handle,const Params* p,void*) -> unsigned {
        auto r=NVSDK_NGX_D3D12_EvaluateFeature(cmd,static_cast<const NVSDK_NGX_Handle*>(handle),p,nullptr);
        CoreSession::log_result("NVSDK_NGX_D3D12_EvaluateFeature(18)",r);return unsigned(r);
      };
      release=[](void* handle) -> unsigned {
        auto r=NVSDK_NGX_D3D12_ReleaseFeature(static_cast<NVSDK_NGX_Handle*>(handle));
        CoreSession::log_result("D3D12 ReleaseFeature",r);return unsigned(r);
      };
    } else {
    runtime.verify(path, true); runtime.load();
    auto alloc = runtime.proc<Allocate>("NVSDK_NGX_D3D12_AllocateParameters");
    destroy = runtime.proc<Destroy>("NVSDK_NGX_D3D12_DestroyParameters");
    if (!alloc || !destroy) {
      fs::path selected = core_path;
      if (selected.empty()) {
        wchar_t sys[MAX_PATH]; GetSystemDirectoryW(sys, MAX_PATH);
        fs::path store = fs::path(sys) / "DriverStore/FileRepository";
        for (auto& dir : fs::directory_iterator(store, fs::directory_options::skip_permission_denied)) {
          auto p = dir.path() / "_nvngx.dll";
          if (fs::is_regular_file(p)) { selected = p; break; }
        }
      }
      need(!selected.empty(), "signed installed NGX core required for parameter allocator; use --ngx-core");
      core.verify(selected, false); core.load();
      alloc = core.proc<Allocate>("NVSDK_NGX_D3D12_AllocateParameters"); destroy = core.proc<Destroy>("NVSDK_NGX_D3D12_DestroyParameters");
    }
    create = runtime.proc<Create>("NVSDK_NGX_D3D12_CreateFeature");
    evaluate = runtime.proc<Evaluate>("NVSDK_NGX_D3D12_EvaluateFeature"); release = runtime.proc<Release>("NVSDK_NGX_D3D12_ReleaseFeature");
    using Init = unsigned (*)(unsigned long long, const wchar_t*, ID3D12Device*, unsigned, const void*);
    auto init = runtime.proc<Init>("NVSDK_NGX_D3D12_Init_Ext");
    need(init && alloc && destroy && create && evaluate && release, "signed snippet export set incomplete; STOP without workaround");
    // Pass only the original snippet APIs. Never patch GetModuleFileNameW or impersonate nvngx.dll.
    // NGX's data/log directory must never be the read-only reference/runtime folder.
    fs::path data_path = fs::absolute(fs::path(".scratch") / "dlss5-ngx");
    fs::create_directories(data_path);
    auto directory = data_path.wstring();
    ngxcheck(init(0x0876232cULL, directory.c_str(), c.d12.Get(), 0x15, nullptr), "NR Init_Ext");
    ngxcheck(alloc(&params), "NR parameter allocation"); need(params != nullptr, "NR null parameters");
    }
    parameters(params, w, h);
    c.begin12(); unsigned result = create(c.list.Get(), 18, params, &feature);
    // Creation can record work even on failure. Drain it before releasing any objects.
    c.execute12(); c.wait12(); ngxcheck(result, "NR feature 18 create"); need(feature != nullptr, "NR null feature");
    original = texture(c, w, h, DXGI_FORMAT_R16G16B16A16_FLOAT);
    proxy = texture(c, w, h, DXGI_FORMAT_R16G16B16A16_FLOAT); output = texture(c, w, h, DXGI_FORMAT_R16G16B16A16_FLOAT);
    D3D12_DESCRIPTOR_HEAP_DESC dh{}; dh.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV; dh.NumDescriptors = 8; dh.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    hrcheck(c.d12->CreateDescriptorHeap(&dh, IID_PPV_ARGS(&descriptors)), "codec descriptor heap");
    stride = c.d12->GetDescriptorHandleIncrementSize(dh.Type);
    auto cpu = descriptors->GetCPUDescriptorHandleForHeapStart();
    D3D12_SHADER_RESOURCE_VIEW_DESC srv{}; srv.Format = DXGI_FORMAT_R16G16B16A16_FLOAT; srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING; srv.Texture2D.MipLevels = 1;
    for (auto* im : {color.resource.Get(), original.Get(), proxy.Get(), output.Get()}) { c.d12->CreateShaderResourceView(im, &srv, cpu); cpu.ptr += stride; }
    D3D12_UNORDERED_ACCESS_VIEW_DESC uav{}; uav.Format = srv.Format; uav.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
    for (auto* im : {original.Get(), proxy.Get(), dest.resource.Get(), dest.resource.Get()}) { c.d12->CreateUnorderedAccessView(im, nullptr, &uav, cpu); cpu.ptr += stride; }
    D3D12_DESCRIPTOR_RANGE ranges[2]{}; ranges[0] = {D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 3, 0, 0, 0}; ranges[1] = {D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 2, 0, 0, 0};
    D3D12_ROOT_PARAMETER rp[3]{};
    for (int i = 0; i < 2; ++i) { rp[i].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE; rp[i].DescriptorTable = {1, &ranges[i]}; }
    rp[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS; rp[2].Constants = {0, 0, 40};
    D3D12_ROOT_SIGNATURE_DESC rs{}; rs.NumParameters = 3; rs.pParameters = rp;
    ComPtr<ID3DBlob> blob, errors; hrcheck(D3D12SerializeRootSignature(&rs, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &errors), "codec root serialization");
    hrcheck(c.d12->CreateRootSignature(0, blob->GetBufferPointer(), blob->GetBufferSize(), IID_PPV_ARGS(&root)), "codec root");
    auto pipeline = [&](const char* entry, ComPtr<ID3D12PipelineState>& pso) {
      ComPtr<ID3DBlob> code, err;
      auto r = D3DCompile(nr::nr_codec_shader, std::strlen(nr::nr_codec_shader), "DCS nr_codec_shader.hpp", nullptr, nullptr, entry,
                          "cs_5_0", D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &code, &err);
      if (FAILED(r) && err) std::fprintf(stderr, "%s\n", static_cast<const char*>(err->GetBufferPointer()));
      hrcheck(r, "DCS codec compile (D3DCompile, not DXC)");
      D3D12_COMPUTE_PIPELINE_STATE_DESC ps{}; ps.pRootSignature = root.Get(); ps.CS = {code->GetBufferPointer(), code->GetBufferSize()};
      hrcheck(c.d12->CreateComputePipelineState(&ps, IID_PPV_ARGS(&pso)), "codec pipeline");
    };
    pipeline("EncodeMain", encode); pipeline("DecodeMain", decode);
    std::puts("NR feature 18 created: identity-logged, unmodified runtime; DCS full-frame HDR codec ready");
  }
  static unsigned scaling(Params* p) { p->Set("DLSSNR.ScalingRatio", 1.0f); return 1; }
  static void parameters(Params* params, uint32_t w, uint32_t h) {
    for (auto key : {"Width", "OutWidth", "DLSSNR.Width", "DLSSNR.InputWidth", "DLSSNR.OutputWidth", "DLSSNR.Output.Width"}) params->Set(key, w);
    for (auto key : {"Height", "OutHeight", "DLSSNR.Height", "DLSSNR.InputHeight", "DLSSNR.OutputHeight", "DLSSNR.Output.Height"}) params->Set(key, h);
    params->Set("DLSSNR.Scale", 1.0f); params->Set("DLSSNR.ScalingRatio", 1.0f); params->Set("DLSSNR.Upscaling", 0u);
    params->Set("DLSSNR.Enabled", 1u); params->Set("PerfQualityValue", 0u); params->Set("DLSS.Feature.Create.Flags", 1u);
    params->Set("CreationNodeMask", 1u); params->Set("VisibilityNodeMask", 1u);
    params->Set("DLSSNR.Hint.Render.Preset", 0u); params->Set("DLSSNR.Intensity", 1.0f);
    params->Set("DLSSNR.LocalToneStrength", 1.0f); params->Set("DLSSNR.LocalStructureStrength", 1.0f);
    params->Set("DLSSNR.SkinStructureStrength", 1.0f); params->Set("DLSSNR.ControlMask", static_cast<void*>(nullptr));
    params->Set("DLSSNR.UseAutoMask", 0u); params->Set("DLSSNR.Style", 0u); params->Set("DLSSNR.UICorrection", 0u);
    params->Set("DLSSNRComputeScalingRatioCallback", reinterpret_cast<void*>(&Neural::scaling));
  }
  void codec(bool encoding) {
    auto* l = c.list.Get(); ID3D12DescriptorHeap* heaps[]{descriptors.Get()}; l->SetDescriptorHeaps(1, heaps);
    l->SetComputeRootSignature(root.Get()); l->SetPipelineState(encoding ? encode.Get() : decode.Get());
    auto gpu = descriptors->GetGPUDescriptorHandleForHeapStart(); gpu.ptr += (encoding ? 0 : 1) * stride; l->SetComputeRootDescriptorTable(0, gpu);
    gpu = descriptors->GetGPUDescriptorHandleForHeapStart(); gpu.ptr += (encoding ? 4 : 6) * stride; l->SetComputeRootDescriptorTable(1, gpu);
    nr::CodecConstants constants{}; constants.size[0] = constants.source_size[0] = constants.proxy_size[0] = constants.region_size[0] = w;
    constants.size[1] = constants.source_size[1] = constants.proxy_size[1] = constants.region_size[1] = h;
    constants.paper_white_scale = constants.transfer_strength = constants.color_strength = 1;
    constants.hdr_mode = 1; constants.foveation_width = constants.foveation_height = 1;
    l->SetComputeRoot32BitConstants(2, 40, &constants, 0); l->Dispatch((w + 15) / 16, (h + 15) / 16, 1);
  }
  void record(Image& color, Image& depth, Image& mv, Image& dest, bool reset) {
    auto* l = c.list.Get();
    l->EndQuery(c.queries12.Get(), D3D12_QUERY_TYPE_TIMESTAMP, 0);
    transition(l, color.resource.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    for (auto* p : {original.Get(), proxy.Get(), output.Get(), dest.resource.Get()}) transition(l, p, D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    codec(true);
    transition(l, original.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    transition(l, proxy.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    for (auto* im : {depth.resource.Get(), mv.resource.Get()}) transition(l, im, D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    params->Set("DLSSNR.Color", proxy.Get()); params->Set("DLSSNR.Output", output.Get());
    params->Set("DLSSNR.Depth", depth.resource.Get()); params->Set("DLSSNR.MVec", mv.resource.Get());
    for (auto prefix : {"Color", "Output", "Depth", "MVec"}) {
      const std::string base = std::string("DLSSNR.") + prefix + "Subrect";
      params->Set((base + "BaseX").c_str(), 0u); params->Set((base + "BaseY").c_str(), 0u);
      params->Set((base + "Width").c_str(), w); params->Set((base + "Height").c_str(), h);
    }
    // Synthetic motion is already previous-minus-current UV, full-frame unjittered.
    params->Set("DLSSNR.MVecScaleX", float(w)); params->Set("DLSSNR.MVecScaleY", float(h));
    params->Set("DLSSNR.DepthInverted", 0u); params->Set("DLSSNR.Reset", reset ? 1u : 0u);
    l->EndQuery(c.queries12.Get(), D3D12_QUERY_TYPE_TIMESTAMP, 2);
    ngxcheck(evaluate(l, feature, params, nullptr), "NR feature 18 evaluate");
    l->EndQuery(c.queries12.Get(), D3D12_QUERY_TYPE_TIMESTAMP, 3);
    transition(l, output.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    codec(false);
    for (auto* im : {color.resource.Get(), depth.resource.Get(), mv.resource.Get(), original.Get(), proxy.Get(), output.Get()})
      transition(l, im, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COMMON);
    transition(l, dest.resource.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COMMON);
    l->EndQuery(c.queries12.Get(), D3D12_QUERY_TYPE_TIMESTAMP, 1);
    l->ResolveQueryData(c.queries12.Get(), D3D12_QUERY_TYPE_TIMESTAMP, 0, 4, c.times12.Get(), 0);
  }
  ~Neural() {
    // All recordings must be complete before release; even failure can record work.
    if (c.q12) { try { c.wait12(); } catch (...) { TerminateProcess(GetCurrentProcess(), 2); } }
    if (feature && release) release(feature); if (params && destroy) destroy(params);
  }
};

float half_to_float(uint16_t h);
double median(std::vector<double> values);
void vk_image_barrier(Context& c, VkCommandBuffer cmd, VulkanImage& image, bool initial = false, VkImageLayout from=VK_IMAGE_LAYOUT_GENERAL, VkImageLayout to=VK_IMAGE_LAYOUT_GENERAL) {
  VkImageMemoryBarrier b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
  b.oldLayout = initial ? VK_IMAGE_LAYOUT_UNDEFINED : from;
  b.newLayout = to; b.srcQueueFamilyIndex = b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  b.srcAccessMask = initial ? 0 : VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
  b.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
  b.image = image.image; b.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
  vkCmdPipelineBarrier(cmd, initial ? VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT : VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
    VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, nullptr, 0, nullptr, 1, &b);
}
int run_vulkan_core(const fs::path& runtime, const fs::path& core, uint32_t w, uint32_t h, uint32_t frames, bool validation, bool try_shutdown = false, uint32_t warmup=2, bool readonly_inputs=false, unsigned guide_switch=0, bool converted_guides=false) {
  Context c; CoreSession session;
  session.prepare(runtime, core, "vulkan");
  c.init(validation, &session.info, session.data.c_str()); no_game(); session.init(c, true);
  Neural::parameters(session.params, w, h);
  GpuTelemetry telemetry(c);
  auto milliseconds=[](){return std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now().time_since_epoch()).count();};
  VulkanImage color(c,w,h,VK_FORMAT_R16G16B16A16_SFLOAT);
  std::unique_ptr<VulkanImage> depth,motion;
  const uint32_t sizes[][2]{{2560,1067},{2227,928},{1920,800},{2560,1067}};
  uint32_t guide_w=guide_switch&&!converted_guides?sizes[0][0]:w,guide_h=guide_switch&&!converted_guides?sizes[0][1]:h;
  auto make_guides=[&](){depth=std::make_unique<VulkanImage>(c,guide_w,guide_h,VK_FORMAT_R32_SFLOAT);
    motion=std::make_unique<VulkanImage>(c,guide_w,guide_h,VK_FORMAT_R32G32_SFLOAT);};
  make_guides();
  VulkanImage original(c,w,h,VK_FORMAT_R16G16B16A16_SFLOAT), proxy(c,w,h,VK_FORMAT_R16G16B16A16_SFLOAT);
  VulkanImage neural(c,w,h,VK_FORMAT_R16G16B16A16_SFLOAT), output(c,w,h,VK_FORMAT_R16G16B16A16_SFLOAT);
  VulkanCodec codec(c);
  uint64_t pixels = uint64_t(w)*h, bytes = pixels*20;
  std::printf("fixture images color=%p depth=%p motion=%p original=%p proxy=%p neural=%p output=%p\n",reinterpret_cast<void*>(color.image),reinterpret_cast<void*>(depth->image),reinterpret_cast<void*>(motion->image),reinterpret_cast<void*>(original.image),reinterpret_cast<void*>(proxy.image),reinterpret_cast<void*>(neural.image),reinterpret_cast<void*>(output.image));
  Buffer upload(c,bytes,VK_BUFFER_USAGE_TRANSFER_SRC_BIT), readback(c,pixels*8,VK_BUFFER_USAGE_TRANSFER_DST_BIT);
  struct Feature {
    NVSDK_NGX_Handle* handle{};
    ~Feature() { if(handle) CoreSession::log_result("Vulkan ReleaseFeature",NVSDK_NGX_VULKAN_ReleaseFeature(handle)); }
  } feature;
  struct Drain { Context& c; ~Drain(){ c.wait_vulkan(); } } drain{c};
  VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO}; begin.flags=VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
  vkcheck(vkBeginCommandBuffer(c.a,&begin),"Vulkan core begin create");
  auto result=NVSDK_NGX_VULKAN_CreateFeature(c.a,static_cast<NVSDK_NGX_Feature>(18),session.params,&feature.handle);
  CoreSession::log_result("NVSDK_NGX_VULKAN_CreateFeature(18)",result);
  vkcheck(vkEndCommandBuffer(c.a),"Vulkan core end create");
  auto submit_frame=[&](){
    VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO}; submit.commandBufferCount=1; submit.pCommandBuffers=&c.a;
    vkcheck(vkQueueSubmit(c.queue,1,&submit,c.produced),"Vulkan core submit"); c.a_pending=true; c.wait_vulkan();
    vkcheck(vkResetFences(c.device,1,&c.produced),"Vulkan core reset fence");
  };
  submit_frame();
  unsigned create_count=1;
  auto config_key=[&](uint32_t rw,uint32_t rh){
    bg3::dlss5_frame::FeatureKey key{};auto& in=key.guides;
    in.render[0]=in.depth_extent[0]=in.motion_extent[0]=in.motion_size[0]=rw;
    in.render[1]=in.depth_extent[1]=in.motion_extent[1]=in.motion_size[1]=rh;
    in.output[0]=w;in.output[1]=h;in.scale[0]=in.scale[1]=1;
    key.color_format=VK_FORMAT_R16G16B16A16_SFLOAT;key.depth_format=VK_FORMAT_R32_SFLOAT;key.motion_format=VK_FORMAT_R32G32_SFLOAT;key.hdr=true;
    return key;
  };
  auto feature_key=config_key(guide_switch?sizes[0][0]:w,guide_switch?sizes[0][1]:h);
  if(result!=NVSDK_NGX_Result_Success || !feature.handle){
    std::printf("FEATURE_BLOCKED create=0x%08x; NR frames=0, median/p90 unavailable (not zero)\n",unsigned(result));
    if(feature.handle){CoreSession::log_result("Vulkan ReleaseFeature",NVSDK_NGX_VULKAN_ReleaseFeature(feature.handle));feature.handle=nullptr;}
    bool clean=session.finish_vulkan(try_shutdown);std::fflush(nullptr);
    if(!clean)ExitProcess(session.finish_failed?2:1);
    ngxcheck(unsigned(result),"Vulkan core feature 18 create");need(feature.handle!=nullptr,"Vulkan core null feature");
  }
  std::vector<double> nr_times,total_times,encode_times,decode_times,eval_cpu_times,wait_cpu_times,prep_cpu_times;
  std::vector<uint8_t> cpu_input(bytes); std::vector<uint16_t> cpu_output(pixels*4);
  // Preserve the reference half decoder exactly, without millions of scalar
  // ldexp calls between GPU submissions (which also distort idle clock policy).
  std::vector<float> half_values(65536);
  for(unsigned i=0;i<65536;++i)half_values[i]=half_to_float(uint16_t(i));
  FILE* csv{};auto csv_path=session.directory/"timings.csv";need(_wfopen_s(&csv,csv_path.c_str(),L"wb")==0 && csv,"timing CSV");
  std::fprintf(csv,"frame,warmup,nr_ms,encode_ms,decode_ms,total_ms,eval_cpu_ms,submit_wait_ms,prepare_cpu_ms,sm_mhz,memory_mhz,pstate,gpu_util,budget_bytes,usage_bytes\n");
  std::printf("benchmark frames=%u warmup=%u measured=%u feature_create_count=1 fixture_resources_persistent=1 timing_csv=%ls readonly_inputs=%d\n",frames,warmup,frames-warmup,csv_path.c_str(),int(readonly_inputs));
  for(uint32_t frame=0;frame<frames;++frame){
    no_game();
    const unsigned phase=guide_switch?(frame/(frames/4))%4:0;
    const bool switched=guide_switch && frame && frame%(frames/4)==0;
    const uint32_t render_w=guide_switch?sizes[phase][0]:w,render_h=guide_switch?sizes[phase][1]:h;
    const auto key=config_key(render_w,render_h);
    const bool needs_recreate=bg3::dlss5_frame::recreate(feature_key,key);
    if(switched){
      // submit_frame waited for the previous use. Drop the executable recording
      // before releasing its feature/images, matching the host lifetime contract.
      vkcheck(vkResetCommandBuffer(c.a,0),"discard old guide recording");
      if(guide_switch==2 && needs_recreate){
        ngxcheck(unsigned(NVSDK_NGX_VULKAN_ReleaseFeature(feature.handle)),"switch ReleaseFeature");feature.handle=nullptr;
        ngxcheck(unsigned(NVSDK_NGX_VULKAN_DestroyParameters(session.params)),"switch DestroyParameters");session.params=nullptr;
        ngxcheck(unsigned(NVSDK_NGX_VULKAN_AllocateParameters(&session.params)),"switch AllocateParameters");
        Neural::parameters(session.params,w,h);
      }
      guide_w=converted_guides?w:render_w;guide_h=converted_guides?h:render_h;make_guides();
      if(guide_switch==2 && needs_recreate){
        vkcheck(vkBeginCommandBuffer(c.a,&begin),"begin switch create");
        result=NVSDK_NGX_VULKAN_CreateFeature(c.a,static_cast<NVSDK_NGX_Feature>(18),session.params,&feature.handle);
        CoreSession::log_result("switch CreateFeature(18)",result);
        vkcheck(vkEndCommandBuffer(c.a),"end switch create");submit_frame();
        ngxcheck(unsigned(result),"switch CreateFeature");need(feature.handle!=nullptr,"switch null feature");++create_count;feature_key=key;
      }
    }
    if(guide_switch)std::printf("GUIDE_SWITCH frame=%u phase=%u render=%ux%u NGX_guides=%ux%u output=%ux%u recreate=%u creates=%u validation=%u known=%u\n",frame+1,phase,render_w,render_h,guide_w,guide_h,w,h,unsigned(switched&&guide_switch==2),create_count,validation_errors.load(),known_nr_validation_errors.load());
    double prep_start=milliseconds();
    auto* half=reinterpret_cast<uint16_t*>(cpu_input.data());
    auto* z=reinterpret_cast<float*>(cpu_input.data()+pixels*8);
    auto* mv=reinterpret_cast<float*>(cpu_input.data()+pixels*12);
    for(uint32_t y=0;y<h;++y)for(uint32_t x=0;x<w;++x){
      uint64_t i=uint64_t(y)*w+x;
      half[i*4]=uint16_t(0x3000+((x+frame*7)%512)*4); half[i*4+1]=uint16_t(0x3000+(y%512)*4);
      half[i*4+2]=((x/32+y/32+frame)%2)?0x4400:0x3400; half[i*4+3]=0x3c00;
    }
    for(uint32_t y=0;y<guide_h;++y)for(uint32_t x=0;x<guide_w;++x){
      const uint64_t i=uint64_t(y)*guide_w+x;
      // CPU equivalent of GuidesMain's pixel-centre sampling for the converted
      // case. NR sees fixed output extents in the layer, not raw render extents.
      const uint32_t sy=uint32_t((uint64_t(2*y+1)*render_h)/(2*guide_h));
      z[i]=.25f+.5f*float(sy)/render_h;
      mv[i*2]=frame?1.0f/render_w:0;mv[i*2+1]=0;
    }
    std::memcpy(upload.mapped,cpu_input.data(),size_t(bytes));
    double prep_ms=milliseconds()-prep_start;
    vkcheck(vkResetCommandBuffer(c.a,0),"Vulkan core reset recording"); vkcheck(vkBeginCommandBuffer(c.a,&begin),"Vulkan core begin sequence");
    vkCmdResetQueryPool(c.a,c.queries,0,6);
    VulkanImage* all[]{&color,depth.get(),motion.get(),&original,&proxy,&neural,&output};
    for(auto* image:all)vk_image_barrier(c,c.a,*image,frame==0 || (switched && (image==depth.get()||image==motion.get())));
    VulkanImage* inputs[]{&color,depth.get(),motion.get()}; uint64_t offsets[]{0,pixels*8,pixels*12};
    for(unsigned i=0;i<3;++i){
      VkBufferImageCopy copy{}; copy.bufferOffset=offsets[i]; copy.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1};copy.imageExtent=i?VkExtent3D{guide_w,guide_h,1}:VkExtent3D{w,h,1};
      vkCmdCopyBufferToImage(c.a,upload.buffer,inputs[i]->image,VK_IMAGE_LAYOUT_GENERAL,1,&copy); vk_image_barrier(c,c.a,*inputs[i]);
    }
    nr::CodecConstants constants{};
    constants.size[0]=constants.source_size[0]=constants.proxy_size[0]=constants.region_size[0]=w;
    constants.size[1]=constants.source_size[1]=constants.proxy_size[1]=constants.region_size[1]=h;
    constants.paper_white_scale=constants.transfer_strength=constants.color_strength=1;constants.hdr_mode=1;
    constants.foveation_width=constants.foveation_height=1;
    vkCmdWriteTimestamp(c.a,VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,c.queries,2);
    codec.record(c.a,true,{color.view,color.view,color.view},{original.view,proxy.view},constants);
    vk_image_barrier(c,c.a,original);vk_image_barrier(c,c.a,proxy);
    vkCmdWriteTimestamp(c.a,VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,c.queries,4);
    if(readonly_inputs)for(auto* image:{&proxy,depth.get(),motion.get()}){
      vk_image_barrier(c,c.a,*image,false,VK_IMAGE_LAYOUT_GENERAL,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);image->ngx.ReadWrite=false;
    }
    session.params->Set("DLSSNR.Color",static_cast<void*>(&proxy.ngx)); session.params->Set("DLSSNR.Output",static_cast<void*>(&neural.ngx));
    session.params->Set("DLSSNR.Depth",static_cast<void*>(&depth->ngx));session.params->Set("DLSSNR.MVec",static_cast<void*>(&motion->ngx));
    for(auto prefix:{"Color","Output","Depth","MVec"}){
      std::string base=std::string("DLSSNR.")+prefix+"Subrect";
      session.params->Set((base+"BaseX").c_str(),0u);session.params->Set((base+"BaseY").c_str(),0u);
      const bool is_guide=!std::strcmp(prefix,"Depth")||!std::strcmp(prefix,"MVec");
      session.params->Set((base+"Width").c_str(),is_guide?guide_w:w);session.params->Set((base+"Height").c_str(),is_guide?guide_h:h);
    }
    session.params->Set("DLSSNR.MVecScaleX",float(w));session.params->Set("DLSSNR.MVecScaleY",float(h));
    session.params->Set("DLSSNR.DepthInverted",0u);session.params->Set("DLSSNR.Reset",frame==0||switched?1u:0u);
    vkCmdWriteTimestamp(c.a,VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,c.queries,0);
    double eval_start=milliseconds();
    result=NVSDK_NGX_VULKAN_EvaluateFeature(c.a,feature.handle,session.params,nullptr);
    double eval_ms=milliseconds()-eval_start;
    CoreSession::log_result("NVSDK_NGX_VULKAN_EvaluateFeature(18)",result);
    vkCmdWriteTimestamp(c.a,VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,c.queries,1);
    if(result==NVSDK_NGX_Result_Success){
      if(readonly_inputs)for(auto* image:{&proxy,depth.get(),motion.get()})vk_image_barrier(c,c.a,*image,false,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,VK_IMAGE_LAYOUT_GENERAL);
      vk_image_barrier(c,c.a,neural);
      vkCmdWriteTimestamp(c.a,VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,c.queries,5);
      codec.record(c.a,false,{original.view,proxy.view,neural.view},{output.view,proxy.view},constants);
      vk_image_barrier(c,c.a,output);
    }
    vkCmdWriteTimestamp(c.a,VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,c.queries,3);
    VkBufferImageCopy copy{};copy.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1};copy.imageExtent={w,h,1};
    if(result==NVSDK_NGX_Result_Success)vkCmdCopyImageToBuffer(c.a,output.image,VK_IMAGE_LAYOUT_GENERAL,readback.buffer,1,&copy);
    VkMemoryBarrier host{VK_STRUCTURE_TYPE_MEMORY_BARRIER};host.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;host.dstAccessMask=VK_ACCESS_HOST_READ_BIT;
    vkCmdPipelineBarrier(c.a,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_HOST_BIT,0,1,&host,0,nullptr,0,nullptr);
    vkcheck(vkEndCommandBuffer(c.a),"Vulkan core end sequence");
    double wait_start=milliseconds();submit_frame();double wait_ms=milliseconds()-wait_start;
    auto gpu=telemetry.snapshot();
    ngxcheck(unsigned(result),"Vulkan core feature18 evaluate");
    std::memcpy(cpu_output.data(),readback.mapped,size_t(pixels*8));
    auto* decoded=cpu_output.data();size_t changed{};double delta{},mean{};
    for(size_t i=0;i<pixels*4;++i){
      float value=half_values[decoded[i]]; need(std::isfinite(value),"non-finite Vulkan NR decoded HDR");
      if(i%4==3)need(decoded[i]==half[i],"Vulkan NR codec alpha changed");
      else{double d=std::abs(double(value)-half_values[half[i]]);changed+=d>1e-4;delta+=d;mean+=value;}
    }
    need(changed>pixels/10,"Vulkan NR decoded output unchanged");
    uint64_t times[6]{};vkcheck(vkGetQueryPoolResults(c.device,c.queries,0,6,sizeof times,times,sizeof(uint64_t),VK_QUERY_RESULT_64_BIT|VK_QUERY_RESULT_WAIT_BIT),"Vulkan NR timings");
    double nr_ms=double(times[1]-times[0])*c.tick/1e6,total_ms=double(times[3]-times[2])*c.tick/1e6;
    double encode_ms=double(times[4]-times[2])*c.tick/1e6,decode_ms=double(times[3]-times[5])*c.tick/1e6;
    if(frame>=warmup){nr_times.push_back(nr_ms);total_times.push_back(total_ms);encode_times.push_back(encode_ms);decode_times.push_back(decode_ms);eval_cpu_times.push_back(eval_ms);wait_cpu_times.push_back(wait_ms);prep_cpu_times.push_back(prep_ms);}
    std::fprintf(csv,"%u,%u,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%u,%u,%u,%u,%llu,%llu\n",frame+1,frame<warmup?1:0,nr_ms,encode_ms,decode_ms,total_ms,eval_ms,wait_ms,prep_ms,gpu.sm_mhz,gpu.mem_mhz,gpu.pstate,gpu.gpu_util,static_cast<unsigned long long>(gpu.budget_bytes),static_cast<unsigned long long>(gpu.usage_bytes));std::fflush(csv);
    std::printf("Vulkan frame %u/%u warmup=%u finite=1 changed=%zu nr=%.3f encode=%.3f decode=%.3f eval_cpu=%.3f submit_wait=%.3f prep_cpu=%.3f ms SM=%u MEM=%u P%u budget=%llu usage=%llu\n",frame+1,frames,frame<warmup?1:0,changed,nr_ms,encode_ms,decode_ms,eval_ms,wait_ms,prep_ms,gpu.sm_mhz,gpu.mem_mhz,gpu.pstate,static_cast<unsigned long long>(gpu.budget_bytes),static_cast<unsigned long long>(gpu.usage_bytes));
    if(validation_errors && (!guide_switch || validation_errors!=known_nr_validation_errors)){
      std::fclose(csv);
      std::fprintf(stderr,"VALIDATION_BLOCKED errors=%u; no validation messages filtered\n",validation_errors.load());
      CoreSession::log_result("Vulkan ReleaseFeature",NVSDK_NGX_VULKAN_ReleaseFeature(feature.handle));feature.handle=nullptr;
      bool clean=session.finish_vulkan(try_shutdown);std::fflush(nullptr);
      if(!clean)ExitProcess(session.finish_failed?2:1);
      need(false,"Vulkan core validation errors");
    }
  }
  std::fclose(csv);
  if(guide_switch==2)need(create_count==4,"recreate decision did not replace each guide configuration");
  auto report=[](const char* label,const std::vector<double>&v){auto st=nr::summarize(v);need(st.has_value(),"no post-warmup samples");std::printf("SUMMARY %s count=%zu median=%.6f p90=%.6f min=%.6f max=%.6f ms\n",label,v.size(),st->median,st->p90,st->minimum,st->maximum);};
  report("NR",nr_times);report("encode",encode_times);report("decode",decode_times);report("codec+NR",total_times);report("eval_cpu",eval_cpu_times);report("submit_wait",wait_cpu_times);report("prepare_cpu",prep_cpu_times);
  std::printf("SEQUENCE_COMPLETE Vulkan core NR %ux%u frames=%u measured=%zu validation=%s errors=%u known_NR_VUID=%u; feature_create_count=%u finite=1 guide_switch=%u converted=%u\n",w,h,frames,nr_times.size(),validation?"on":"off",validation_errors.load(),known_nr_validation_errors.load(),create_count,guide_switch,unsigned(converted_guides));
  CoreSession::log_result("Vulkan ReleaseFeature",NVSDK_NGX_VULKAN_ReleaseFeature(feature.handle));feature.handle=nullptr;
  bool clean=session.finish_vulkan(try_shutdown);
  std::printf("fixture finish clean_sdk_shutdown=%d shutdown_probe_stalled=%d idle_failed=%d exit_policy=%s\n",int(clean),int(session.shutdown_stalled),int(session.finish_failed),clean?"normal":"process-lifetime-no-op");
  std::fflush(nullptr);
  if(!clean) ExitProcess(session.finish_failed?2:validation_errors?1:0);
  return validation_errors?1:0;
}

float half_to_float(uint16_t h) { // Same decoder as frame_replay.cpp.
  const uint32_t sign = h >> 15, exponent = (h >> 10) & 31, mantissa = h & 1023;
  float v = exponent == 0 ? std::ldexp(float(mantissa), -24) : exponent == 31 ? (mantissa ? NAN : INFINITY) : std::ldexp(float(mantissa | 1024), int(exponent) - 25);
  return sign ? -v : v;
}
double median(std::vector<double> v) { std::sort(v.begin(), v.end()); return v[v.size()/2]; }
int run(bool neural, const fs::path& runtime, const fs::path& core, uint32_t w, uint32_t h, uint32_t frames, bool validation, bool core_route = false) {
  Context c; CoreSession core_session;
  if (core_route) core_session.prepare(runtime, core, "d3d12");
  c.init(validation);
  if (core_route) { no_game(); core_session.init(c, false); }
  Image color(c, w, h, VK_FORMAT_R16G16B16A16_SFLOAT, DXGI_FORMAT_R16G16B16A16_FLOAT, 8);
  Image depth(c, w, h, VK_FORMAT_R32_SFLOAT, DXGI_FORMAT_R32_FLOAT, 4);
  Image motion(c, w, h, VK_FORMAT_R16G16_SFLOAT, DXGI_FORMAT_R16G16_FLOAT, 4);
  Image output(c, w, h, VK_FORMAT_R16G16B16A16_SFLOAT, DXGI_FORMAT_R16G16B16A16_FLOAT, 8);
  Image* images[]{&color, &depth, &motion, &output};
  const uint64_t pixels = uint64_t(w) * h, offsets[]{0, pixels * 8, pixels * 12, pixels * 16}, total = pixels * 24;
  Buffer upload(c, total, VK_BUFFER_USAGE_TRANSFER_SRC_BIT), download(c, total, VK_BUFFER_USAGE_TRANSFER_DST_BIT);
  Buffer device_source(c, total, VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, true);
  Buffer device_result(c, total, VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, true);
  Neural nr(c); if (neural) { no_game(); nr.init(runtime, core, w, h, color, output, core_route ? &core_session : nullptr); }
  auto depth_scratch = texture(c, w, h, DXGI_FORMAT_R32_FLOAT), mv_scratch = texture(c, w, h, DXGI_FORMAT_R16G16_FLOAT);
  struct Drain {
    Context& c;
    ~Drain() {
      // Runs before any image/buffer/NR object destructor, including exception paths.
      c.wait_vulkan();
      try { c.wait12(); } catch (...) { TerminateProcess(GetCurrentProcess(), 2); }
    }
  } drain{c};
  nr::FenceBook book; std::vector<double> vulkan_ms, d3d_ms, nr_ms, codec_ms;
  uint64_t last_progress = GetTickCount64();
  for (uint32_t frame = 0; frame < frames; ++frame) {
    no_game(); // Also catches a game started between frames; no next GPU work is issued.
    auto ticket = book.begin(); need(ticket.has_value(), "fence bookkeeping reservation");
    auto* half = static_cast<uint16_t*>(upload.mapped); auto* depths = reinterpret_cast<float*>(static_cast<uint8_t*>(upload.mapped) + offsets[1]);
    auto* mv = reinterpret_cast<uint16_t*>(static_cast<uint8_t*>(upload.mapped) + offsets[2]);
    for (uint32_t y = 0; y < h; ++y) for (uint32_t x = 0; x < w; ++x) {
      uint64_t i = uint64_t(y) * w + x;
      half[i*4] = uint16_t(0x3000 + ((x + frame * 7) % 512) * 4);
      half[i*4+1] = uint16_t(0x3000 + (y % 512) * 4);
      half[i*4+2] = ((x / 32 + y / 32 + frame) % 2) ? 0x4400 : 0x3400; half[i*4+3] = 0x3c00;
      depths[i] = .25f + .5f * float(y) / h;
      // Exact half UV displacement 2^-12 on later frames (the first is a reset).
      mv[i*2] = frame ? 0x0c00 : 0; mv[i*2+1] = 0;
    }
    std::memset(static_cast<uint8_t*>(upload.mapped) + offsets[3], 0xa5, size_t(pixels * 8));
    VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO}; begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkcheck(vkResetCommandBuffer(c.a, 0), "reset A"); vkcheck(vkBeginCommandBuffer(c.a, &begin), "begin A");
    vkCmdResetQueryPool(c.a, c.queries, 0, 4);
    VkBufferCopy whole{0, 0, total}; vkCmdCopyBuffer(c.a, upload.buffer, device_source.buffer, 1, &whole);
    VkMemoryBarrier prepared{VK_STRUCTURE_TYPE_MEMORY_BARRIER}; prepared.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    prepared.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    vkCmdPipelineBarrier(c.a, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 1, &prepared, 0, nullptr, 0, nullptr);
    // Exclude PCIe fixture upload/readback from the GPU-local transport timings.
    vkCmdWriteTimestamp(c.a, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, c.queries, 0);
    for (uint32_t i = 0; i < 4; ++i) {
      image_barrier(c, c.a, *images[i], true, frame == 0);
      VkBufferImageCopy copy{}; copy.bufferOffset = offsets[i]; copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1}; copy.imageExtent = {w, h, 1};
      vkCmdCopyBufferToImage(c.a, device_source.buffer, images[i]->image, VK_IMAGE_LAYOUT_GENERAL, 1, &copy);
      image_barrier(c, c.a, *images[i], false);
    }
    vkCmdWriteTimestamp(c.a, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, c.queries, 1); vkcheck(vkEndCommandBuffer(c.a), "end A");
    VkTimelineSemaphoreSubmitInfo tsi{VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO}; tsi.signalSemaphoreValueCount = 1; tsi.pSignalSemaphoreValues = &ticket->ready;
    VkSubmitInfo a{VK_STRUCTURE_TYPE_SUBMIT_INFO, &tsi}; a.commandBufferCount = 1; a.pCommandBuffers = &c.a; a.signalSemaphoreCount = 1; a.pSignalSemaphores = &c.timeline;
    vkcheck(vkQueueSubmit(c.queue, 1, &a, c.produced), "submit A / signal ready"); c.a_pending = true;
    hrcheck(c.q12->Wait(c.fence12.Get(), ticket->ready), "D3D12 queue wait ready");
    c.begin12();
    if (neural) nr.record(color, depth, motion, output, frame == 0);
    else {
      c.list->EndQuery(c.queries12.Get(), D3D12_QUERY_TYPE_TIMESTAMP, 0);
      transition(c.list.Get(), color.resource.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_SOURCE);
      transition(c.list.Get(), output.resource.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_DEST);
      c.list->CopyResource(output.resource.Get(), color.resource.Get());
      transition(c.list.Get(), color.resource.Get(), D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_COMMON);
      transition(c.list.Get(), output.resource.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_COMMON);
      for (uint32_t i = 1; i <= 2; ++i) {
        auto* scratch = i == 1 ? depth_scratch.Get() : mv_scratch.Get(); auto* im = images[i]->resource.Get();
        transition(c.list.Get(), im, D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_SOURCE);
        transition(c.list.Get(), scratch, D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_DEST);
        c.list->CopyResource(scratch, im);
        transition(c.list.Get(), im, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_COPY_DEST);
        transition(c.list.Get(), scratch, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_COPY_SOURCE);
        c.list->CopyResource(im, scratch);
        transition(c.list.Get(), im, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_COMMON);
        transition(c.list.Get(), scratch, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_COMMON);
      }
      c.list->EndQuery(c.queries12.Get(), D3D12_QUERY_TYPE_TIMESTAMP, 1);
      c.list->ResolveQueryData(c.queries12.Get(), D3D12_QUERY_TYPE_TIMESTAMP, 0, 2, c.times12.Get(), 0);
    }
    c.execute12(); hrcheck(c.q12->Signal(c.fence12.Get(), ticket->done), "D3D12 signal done");
    vkcheck(vkResetCommandBuffer(c.b, 0), "reset B"); vkcheck(vkBeginCommandBuffer(c.b, &begin), "begin B");
    vkCmdWriteTimestamp(c.b, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, c.queries, 2);
    for (uint32_t i = 0; i < 4; ++i) {
      image_barrier(c, c.b, *images[i], true);
      VkBufferImageCopy copy{}; copy.bufferOffset = offsets[i]; copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1}; copy.imageExtent = {w, h, 1};
      vkCmdCopyImageToBuffer(c.b, images[i]->image, VK_IMAGE_LAYOUT_GENERAL, device_result.buffer, 1, &copy);
      image_barrier(c, c.b, *images[i], false);
    }
    vkCmdWriteTimestamp(c.b, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, c.queries, 3);
    vkCmdPipelineBarrier(c.b, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 1, &prepared, 0, nullptr, 0, nullptr);
    vkCmdCopyBuffer(c.b, device_result.buffer, download.buffer, 1, &whole);
    VkMemoryBarrier host{VK_STRUCTURE_TYPE_MEMORY_BARRIER}; host.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT; host.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
    vkCmdPipelineBarrier(c.b, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0, 1, &host, 0, nullptr, 0, nullptr);
    vkcheck(vkEndCommandBuffer(c.b), "end B");
    VkPipelineStageFlags stage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
    tsi.signalSemaphoreValueCount = 0; tsi.waitSemaphoreValueCount = 1; tsi.pWaitSemaphoreValues = &ticket->done;
    VkSubmitInfo b{VK_STRUCTURE_TYPE_SUBMIT_INFO, &tsi}; b.commandBufferCount = 1; b.pCommandBuffers = &c.b;
    b.waitSemaphoreCount = 1; b.pWaitSemaphores = &c.timeline; b.pWaitDstStageMask = &stage;
    vkcheck(vkQueueSubmit(c.queue, 1, &b, c.completed), "submit B / wait done"); c.b_pending = true;
    c.wait_vulkan();
    vkcheck(vkResetFences(c.device, 1, &c.completed), "reset completion");
    vkcheck(vkResetFences(c.device, 1, &c.produced), "reset producer completion");
    uint64_t value{}; vkcheck(vkGetSemaphoreCounterValue(c.device, c.timeline, &value), "shared timeline counter");
    need(value == ticket->done && c.fence12->GetCompletedValue() == value && book.complete(value), "both APIs agree on completed fence value");
    auto* got = static_cast<uint8_t*>(download.mapped); auto* source = static_cast<uint8_t*>(upload.mapped);
    need(std::memcmp(got, source, size_t(offsets[3])) == 0, "colour/depth/MV ingress round trip not bit-exact");
    size_t changed{}; double delta{}, mean{};
    if (!neural) need(std::memcmp(got + offsets[3], source, size_t(pixels * 8)) == 0, "D3D12 copy result not bit-exact");
    else {
      auto* result = reinterpret_cast<uint16_t*>(got + offsets[3]);
      for (size_t i = 0; i < pixels * 4; ++i) {
        float v = half_to_float(result[i]), original = half_to_float(half[i]); need(std::isfinite(v), "non-finite neural output");
        if (i % 4 == 3) need(result[i] == half[i], "NR codec changed alpha");
        else { double d = std::abs(double(v) - original); changed += d > 1e-4; delta += d; mean += v; }
      }
      need(changed > pixels / 10, "NR output indistinguishable from passthrough");
    }
    uint64_t vt[4]{}; vkcheck(vkGetQueryPoolResults(c.device, c.queries, 0, 4, sizeof vt, vt, sizeof(uint64_t), VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WAIT_BIT), "Vulkan timestamps");
    uint64_t* dt{}; D3D12_RANGE range{0, neural ? 32u : 16u}; hrcheck(c.times12->Map(0, &range, reinterpret_cast<void**>(&dt)), "D3D12 times map");
    double vm = ((vt[1]-vt[0])+(vt[3]-vt[2])) * c.tick / 1e6;
    double dm = double(dt[1]-dt[0]) * 1000 / c.frequency;
    double nm = neural ? double(dt[3]-dt[2]) * 1000 / c.frequency : 0;
    D3D12_RANGE none{0, 0}; c.times12->Unmap(0, &none);
    if (frame >= 2) { vulkan_ms.push_back(vm); d3d_ms.push_back(dm); nr_ms.push_back(nm); codec_ms.push_back(dm-nm); }
    need(validation_errors == 0, "Vulkan validation errors");
    if (frame == 0 || frame + 1 == frames || GetTickCount64() - last_progress >= 10000) {
      std::printf("frame %u/%u ready=%llu done=%llu checks=PASS changed=%zu mean_delta=%g mean=%g vk=%.3f d3d=%.3f nr=%.3f ms\n",
        frame+1, frames, ticket->ready, ticket->done, changed, delta/(pixels*3), mean/(pixels*3), vm, dm, nm); last_progress = GetTickCount64();
    }
  }
  c.wait_vulkan(); c.wait12();
  std::printf("PASS --%s %ux%u frames=%u validation_errors=%u copy=%s (medians; first 2 frames excluded)\n", neural ? "nr" : "copy", w, h, frames, validation_errors.load(), neural ? "ingress-exact/NR-finite-changed" : "bit-exact-all-images");
  std::printf("gpu Vulkan_transport_A+B=%.3f D3D12_work=%.3f NR=%.3f codec_or_copy=%.3f GPU_local_total=%.3f ms\n",
      median(vulkan_ms), median(d3d_ms), median(nr_ms), median(codec_ms), median(vulkan_ms)+median(d3d_ms));
  std::puts("GPU durations exclude queue-wait latency, CPU uploads/readback checking and initialization; no cross-API clocks subtracted.");
  return 0;
}
} // namespace
int wmain(int argc, wchar_t** argv) {
  setvbuf(stdout, nullptr, _IONBF, 0); setvbuf(stderr, nullptr, _IONBF, 0);
  SetPriorityClass(GetCurrentProcess(), BELOW_NORMAL_PRIORITY_CLASS);
  bool neural = false, copy = false, verify = false, core_vk = false, core_d12 = false, validation = true, try_shutdown = false, readonly_inputs=false,converted_guides=false;
  unsigned guide_switch=0;
  fs::path runtime, core; uint32_t w = 3840, h = 1600, frames = 8, warmup=2;
  try {
    for (int i = 1; i < argc; ++i) {
      std::wstring arg = argv[i];
      auto value = [&]() -> const wchar_t* { need(i+1 < argc, "missing option value"); return argv[++i]; };
      if (arg == L"--nr-core-vulkan") core_vk = true; else if (arg == L"--nr-core-d3d12") core_d12 = true;
      else if (arg == L"--copy") copy = true; else if (arg == L"--nr") neural = true;
      else if (arg == L"--readonly-inputs") readonly_inputs=true;
      else if (arg == L"--converted-guides") converted_guides=true;
      else if (arg == L"--guide-switch") {std::wstring mode=value();need(mode==L"reuse"||mode==L"recreate","guide switch must be reuse or recreate");guide_switch=mode==L"reuse"?1:2;}
      else if (arg == L"--warmup") warmup=std::stoul(value());
      else if (arg == L"--try-shutdown") try_shutdown = true;
      else if (arg == L"--verify-runtime") verify = true;
      else if (arg == L"--runtime") runtime = value(); else if (arg == L"--ngx-core") core = value();
      else if (arg == L"--width") w = std::stoul(value()); else if (arg == L"--height") h = std::stoul(value());
      else if (arg == L"--frames") frames = std::stoul(value()); else if (arg == L"--no-validation") validation = false;
      else throw std::runtime_error("usage: dlss5_fixture --copy | --nr-core-vulkan | --nr-core-d3d12 | --nr --runtime PATH [--ngx-core PATH] [--width N --height N --frames N --warmup N] [--guide-switch reuse|recreate --converted-guides] [--no-validation] [--readonly-inputs] [--try-shutdown]; --verify-runtime --runtime PATH is CPU-only");
    }
    need(unsigned(copy)+unsigned(neural)+unsigned(verify)+unsigned(core_vk)+unsigned(core_d12) == 1, "select exactly one mode");
    need(w >= 32 && h >= 32 && w <= 8192 && h <= 8192 && frames >= 3 && frames <= 600 && warmup < frames, "dimensions/frames/warmup outside fixture bounds");
    need(!guide_switch || (core_vk && w>=2560 && h>=1067 && frames>=12 && frames%4==0),"guide-switch requires Vulkan core, output >=2560x1067, >=12 frames divisible by 4");
    need(!converted_guides||guide_switch,"converted-guides requires guide-switch");
    if (neural || verify || core_vk || core_d12) { need(!runtime.empty(), "--runtime required"); RuntimeFile checked; checked.verify(runtime, true); }
    if (verify) return 0;
    if (core_vk) return run_vulkan_core(runtime, core, w, h, frames, validation, try_shutdown, warmup, readonly_inputs,guide_switch,converted_guides);

    return run(neural || core_d12, runtime, core, w, h, frames, validation, core_d12);
  } catch (const std::exception& e) { std::fprintf(stderr, "FAIL/BLOCKED %s\n", e.what()); return 1; }
}
