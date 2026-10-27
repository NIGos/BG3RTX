// Runtime HLSL -> SPIR-V through dxcompiler.dll (bin/ first, then the Vulkan
// SDK). Every requested entry is recompiled when anything under shaders/
// changes; a failed compile keeps the previous SPIR-V and logs the errors.
#include "host.h"

#include <unknwn.h>
#include <dxc/dxcapi.h>

#include <cstdlib>
#include <cstring>

namespace bg3 {

struct ShaderEntry {
  std::string file, entry, profile;
  std::vector<uint32_t> code;
  uint64_t generation = 0;
};

static std::mutex g_mu;
static std::unordered_map<std::string, ShaderEntry> g_entries;
static uint64_t g_stamp = 0;
static ULONGLONG g_last_check = 0;
static IDxcUtils* g_utils = nullptr;
static IDxcCompiler3* g_compiler = nullptr;
static IDxcIncludeHandler* g_include = nullptr;
static bool g_dxc_tried = false;

static std::wstring widen(const std::string& s) {
  int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
  std::wstring w(n ? n - 1 : 0, L'\0');
  if (n) MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, w.data(), n);
  return w;
}

static std::string shaders_dir() { return root_dir() + "\\shaders"; }

static bool init_dxc() {
  if (g_dxc_tried) return g_compiler != nullptr;
  g_dxc_tried = true;
  HMODULE mod = LoadLibraryA((bin_dir() + "\\dxcompiler.dll").c_str());
  if (!mod) {
    char sdk[MAX_PATH] = {};
    if (GetEnvironmentVariableA("VULKAN_SDK", sdk, MAX_PATH))
      mod = LoadLibraryA((std::string(sdk) + "\\Bin\\dxcompiler.dll").c_str());
  }
  if (!mod) {
    log("[shaders] dxcompiler.dll not found beside the layer or in %%VULKAN_SDK%%\\Bin");
    return false;
  }
  auto create = reinterpret_cast<DxcCreateInstanceProc>(GetProcAddress(mod, "DxcCreateInstance"));
  if (!create || FAILED(create(CLSID_DxcUtils, IID_PPV_ARGS(&g_utils))) ||
      FAILED(create(CLSID_DxcCompiler, IID_PPV_ARGS(&g_compiler))) ||
      FAILED(g_utils->CreateDefaultIncludeHandler(&g_include))) {
    log("[shaders] DXC initialisation failed");
    g_compiler = nullptr;
    return false;
  }
  log("[shaders] DXC ready");
  return true;
}

// The SHaRC SDK headers (docs/DESIGN_SOTA_NEXT.md item 2): external/sharc-1.8.3/include of
// this checkout or, for a worktree under .claude/worktrees/, of the main checkout (external/
// is not versioned). Empty when absent: the cache's shaders then fail to compile and the
// core keeps it off.
static const std::wstring& sharc_include() {
  static std::wstring dir;
  static bool done = false;
  if (done) return dir;
  done = true;
  for (const std::string& c : {root_dir() + "\\external\\sharc-1.8.3\\include",
                               root_dir() + "\\..\\..\\..\\external\\sharc-1.8.3\\include"}) {
    if (GetFileAttributesA((c + "\\SharcCommon.h").c_str()) == INVALID_FILE_ATTRIBUTES) continue;
    dir = widen(c);
    log("[shaders] SHaRC headers: %s", c.c_str());
    return dir;
  }
  log("[shaders] SHaRC headers not found (external/sharc-1.8.3/include): the radiance cache is unavailable");
  return dir;
}

// A file whose first line is "// dxc: <arguments>" gets those DXC arguments (the SHaRC
// passes: -enable-16bit-types, which its headers require). Other files compile as before.
static std::vector<std::wstring> first_line_args(IDxcBlobEncoding* src) {
  std::vector<std::wstring> out;
  const char* p = static_cast<const char*>(src->GetBufferPointer());
  size_t n = src->GetBufferSize();
  static const char kTag[] = "// dxc:";
  if (n < sizeof kTag - 1 || std::strncmp(p, kTag, sizeof kTag - 1) != 0) return out;
  std::string line(p + sizeof kTag - 1, p + n);
  line = line.substr(0, line.find_first_of("\r\n"));
  size_t i = 0;
  while (i < line.size()) {
    size_t a = line.find_first_not_of(" \t", i);
    if (a == std::string::npos) break;
    size_t b = line.find_first_of(" \t", a);
    out.push_back(widen(line.substr(a, b == std::string::npos ? std::string::npos : b - a)));
    i = b == std::string::npos ? line.size() : b;
  }
  return out;
}

static bool compile(ShaderEntry& e) {
  if (!init_dxc()) return false;
  std::wstring path = widen(shaders_dir() + "\\" + e.file);
  IDxcBlobEncoding* src = nullptr;
  if (FAILED(g_utils->LoadFile(path.c_str(), nullptr, &src))) {
    log("[shaders] cannot read %s", e.file.c_str());
    return false;
  }
  std::wstring entry = widen(e.entry), profile = widen(e.profile), inc = widen(shaders_dir());
  std::vector<LPCWSTR> args = {path.c_str(), L"-spirv", L"-fspv-target-env=vulkan1.3", L"-HV", L"2021",
                               L"-T", profile.c_str(), L"-E", entry.c_str(), L"-I", inc.c_str(),
                               L"-D", L"BG3RTX=1"};
  const std::wstring& sharc = sharc_include();
  if (!sharc.empty()) {
    args.push_back(L"-I");
    args.push_back(sharc.c_str());
    args.push_back(L"-D");
    args.push_back(L"BG3RTX_SHARC_SDK=1");
  }
  const std::vector<std::wstring> extra = first_line_args(src);
  for (const std::wstring& a : extra) args.push_back(a.c_str());
  // shader_debug=1: unoptimised with NonSemantic debug info and the source (DXC rejects
  // it for the RT passes: a DebugLine column past the RT_ALPHA_CANDIDATE macro line).
  // shader_debug=2: optimised (-O3) with OpLine line info only: the same code as the
  // release build, mapped to file:line by Aftermath (.nvdbg).
  const int dbg = cfg_int("shader_debug", 0);
  if (dbg == 1) {
    args.push_back(L"-Od");
    args.push_back(L"-fspv-debug=vulkan-with-source");
  } else if (dbg == 2) {
    args.push_back(L"-O3");
    args.push_back(L"-Zi");
    args.push_back(L"-fspv-debug=file");
    args.push_back(L"-fspv-debug=source");
    args.push_back(L"-fspv-debug=line");
  } else {
    args.push_back(L"-O3");
  }
  DxcBuffer buf{src->GetBufferPointer(), src->GetBufferSize(), DXC_CP_ACP};
  IDxcResult* result = nullptr;
  HRESULT hr = g_compiler->Compile(&buf, args.data(), UINT32(args.size()), g_include, IID_PPV_ARGS(&result));
  src->Release();
  if (FAILED(hr) || !result) {
    log("[shaders] %s:%s compile call failed", e.file.c_str(), e.entry.c_str());
    return false;
  }
  HRESULT status = E_FAIL;
  result->GetStatus(&status);
  IDxcBlobUtf8* errors = nullptr;
  result->GetOutput(DXC_OUT_ERRORS, IID_PPV_ARGS(&errors), nullptr);
  if (errors && errors->GetStringLength()) log("[shaders] %s:%s\n%s", e.file.c_str(), e.entry.c_str(), errors->GetStringPointer());
  if (errors) errors->Release();
  bool ok = false;
  if (SUCCEEDED(status)) {
    IDxcBlob* obj = nullptr;
    if (SUCCEEDED(result->GetResult(&obj)) && obj) {
      auto words = static_cast<const uint32_t*>(obj->GetBufferPointer());
      e.code.assign(words, words + obj->GetBufferSize() / 4);
      ++e.generation;
      aftermath_note_spirv((e.file + ":" + e.entry).c_str(), e.code.data(), e.code.size() * 4, nullptr);
      ok = true;
      obj->Release();
    }
  }
  result->Release();
  log("[shaders] %s:%s %s (generation %llu)", e.file.c_str(), e.entry.c_str(), ok ? "compiled" : "FAILED, keeping previous",
      static_cast<unsigned long long>(e.generation));
  return ok;
}

bool shader_get(const char* file, const char* entry, const char* profile, Bg3ShaderBlob* out) {
  std::lock_guard<std::mutex> lock(g_mu);
  std::string key = std::string(file) + "|" + entry + "|" + profile;
  auto it = g_entries.find(key);
  if (it == g_entries.end()) {
    ShaderEntry e{file, entry, profile};
    compile(e);
    it = g_entries.emplace(key, std::move(e)).first;
  }
  if (it->second.code.empty()) return false;
  out->code = it->second.code.data();
  out->size_bytes = it->second.code.size() * 4;
  out->generation = it->second.generation;
  return true;
}

static uint64_t dir_stamp(const std::string& dir) {
  uint64_t stamp = 0;
  WIN32_FIND_DATAA fd;
  HANDLE h = FindFirstFileA((dir + "\\*").c_str(), &fd);
  if (h == INVALID_HANDLE_VALUE) return 0;
  do {
    if (fd.cFileName[0] == '.') continue;
    if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
      stamp = (std::max)(stamp, dir_stamp(dir + "\\" + fd.cFileName));
      continue;
    }
    uint64_t t = (uint64_t(fd.ftLastWriteTime.dwHighDateTime) << 32) | fd.ftLastWriteTime.dwLowDateTime;
    stamp = (std::max)(stamp, t);
  } while (FindNextFileA(h, &fd));
  FindClose(h);
  return stamp;
}

void shader_poll() {
  ULONGLONG now = GetTickCount64();
  if (now - g_last_check < 250) return;
  g_last_check = now;
  uint64_t stamp = dir_stamp(shaders_dir());
  if (stamp == g_stamp) return;
  bool first = g_stamp == 0;
  g_stamp = stamp;
  if (first) return;
  std::lock_guard<std::mutex> lock(g_mu);
  for (auto& [key, e] : g_entries) compile(e);
}

}  // namespace bg3
