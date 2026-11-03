// Runtime HLSL -> SPIR-V through dxcompiler.dll (bin/ first, then the Vulkan
// SDK). A failed compile keeps the previous SPIR-V and logs the errors.
// A request "<file>?NAME=VALUE;NAME=VALUE" (ABI 23, common/shader_variant.h) compiles
// <file> with each pair as a -D define; the whole string is the cache key.
//
// Persistent cache (layer/shader_cache.h): every entry's SPIR-V is stored under the key of its
// inputs (DXC, arguments, request, entry, profile, the source and every file it can include), in
// %LOCALAPPDATA%\BG3RTX\shader_cache (or %BG3RTX_SHADER_CACHE%\runtime; "none" turns it off). A
// launch with unchanged sources reads the SPIR-V instead of compiling it. Hot reload: when
// shaders/ or common/ change, every entry's key is recomputed; only entries whose key changed are
// fetched or compiled (and get a new generation, so their pipelines are rebuilt).
// Prefetch: requests.txt lists the requests of the recent sessions; a background thread fetches
// (or compiles) them at the first request, so the render thread finds them ready.
#include "host.h"
#include "shader_cache.h"

#include <unknwn.h>
#include <dxc/dxcapi.h>

#include <condition_variable>
#include <cstdlib>
#include <cstring>
#include <thread>

namespace bg3 {

namespace sc = shader_cache;

struct ShaderEntry {
  std::string file, entry, profile;
  std::vector<uint32_t> code;
  // Bg3ShaderBlob borrows code after g_mu is released, including on async workers.
  // Keep replaced allocations until process exit; the shader cache has no device lifetime.
  std::vector<std::vector<uint32_t>> retired_code;
  uint64_t generation = 0;
  std::string key;          // the cache key `code` was built from (empty: none)
  bool built = false;       // the first build was attempted
  bool busy = false;        // a thread is building it (the others wait on g_cv)
  bool requested = false;   // asked for by the renderer this session (not only prefetched)
};

static std::mutex g_mu;
static std::condition_variable g_cv;
static std::unordered_map<std::string, ShaderEntry> g_entries;
static uint64_t g_stamp = 0;
static ULONGLONG g_last_check = 0;

// DXC: one module; a compiler per thread (the prefetch thread compiles beside the render thread).
static DxcCreateInstanceProc g_dxc_create = nullptr;
static std::string g_dxc_path;
static std::once_flag g_dxc_once;
struct Dxc {
  IDxcUtils* utils = nullptr;
  IDxcCompiler3* compiler = nullptr;
  IDxcIncludeHandler* include = nullptr;
  bool tried = false;
};
static thread_local Dxc t_dxc;
static thread_local bool t_prefetch = false;

// Statistics: [shaders] cache lines.
static std::atomic<uint64_t> g_hits{0}, g_compiled{0}, g_failed{0}, g_build_us{0}, g_render_wait_us{0};
static uint64_t g_logged_total = ~0ull;
static ULONGLONG g_stats_tick = 0;

static std::wstring widen(const std::string& s) {
  int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
  std::wstring w(n ? n - 1 : 0, L'\0');
  if (n) MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, w.data(), n);
  return w;
}

static std::string shaders_dir() { return root_dir() + "\\shaders"; }

static uint64_t now_us() {
  static LARGE_INTEGER f = [] {
    LARGE_INTEGER v;
    QueryPerformanceFrequency(&v);
    return v;
  }();
  LARGE_INTEGER c;
  QueryPerformanceCounter(&c);
  return uint64_t(c.QuadPart) * 1000000ull / uint64_t(f.QuadPart);
}

static bool load_dxc() {
  std::call_once(g_dxc_once, [] {
    HMODULE mod = LoadLibraryA((bin_dir() + "\\dxcompiler.dll").c_str());
    if (!mod) {
      char sdk[MAX_PATH] = {};
      if (GetEnvironmentVariableA("VULKAN_SDK", sdk, MAX_PATH))
        mod = LoadLibraryA((std::string(sdk) + "\\Bin\\dxcompiler.dll").c_str());
    }
    if (!mod) {
      log("[shaders] dxcompiler.dll not found beside the layer or in %%VULKAN_SDK%%\\Bin");
      return;
    }
    char path[MAX_PATH] = {};
    GetModuleFileNameA(mod, path, MAX_PATH);
    g_dxc_path = path;
    g_dxc_create = reinterpret_cast<DxcCreateInstanceProc>(GetProcAddress(mod, "DxcCreateInstance"));
  });
  return g_dxc_create != nullptr;
}

static Dxc* dxc() {
  Dxc& d = t_dxc;
  if (!d.tried) {
    d.tried = true;
    if (!load_dxc() || FAILED(g_dxc_create(CLSID_DxcUtils, IID_PPV_ARGS(&d.utils))) ||
        FAILED(g_dxc_create(CLSID_DxcCompiler, IID_PPV_ARGS(&d.compiler))) ||
        FAILED(d.utils->CreateDefaultIncludeHandler(&d.include))) {
      log("[shaders] DXC initialisation failed");
      d.compiler = nullptr;
    } else {
      static std::once_flag ready;
      std::call_once(ready, [] { log("[shaders] DXC ready: %s", g_dxc_path.c_str()); });
    }
  }
  return d.compiler ? &d : nullptr;
}

static void dxc_release_thread() {
  Dxc& d = t_dxc;
  if (d.include) d.include->Release();
  if (d.compiler) d.compiler->Release();
  if (d.utils) d.utils->Release();
  d = Dxc{};
}

// The SHaRC SDK headers (docs/DESIGN_SOTA_NEXT.md item 2): external/sharc-1.8.3/include of
// this checkout or, for a worktree under .claude/worktrees/, of the main checkout (external/
// is not versioned). Empty when absent: the cache's shaders then fail to compile and the
// core keeps it off.
static const std::string& sharc_dir() {
  static std::string dir;
  static std::once_flag once;
  std::call_once(once, [] {
    for (const std::string& c : {root_dir() + "\\external\\sharc-1.8.3\\include",
                                 root_dir() + "\\..\\..\\..\\external\\sharc-1.8.3\\include"}) {
      if (GetFileAttributesA((c + "\\SharcCommon.h").c_str()) == INVALID_FILE_ATTRIBUTES) continue;
      dir = c;
      log("[shaders] SHaRC headers: %s", c.c_str());
      return;
    }
    log("[shaders] SHaRC headers not found (external/sharc-1.8.3/include): the radiance cache is unavailable");
  });
  return dir;
}

// The fixed DXC arguments of compile() (part of the tool id: change both together).
static const char kArgsTemplate[] =
    "{src} -spirv -fspv-target-env=vulkan1.3 -HV 2021 -T {profile} -E {entry} -I {shaders} -D BG3RTX=1 "
    "[-I {sharc} -D BG3RTX_SHARC_SDK=1] {defines} {first_line} {debug}";

// The runtime tool id: dxcompiler.dll's bytes and version, the argument template, SHaRC presence
// and the shader_debug mode (its debug info names the source paths, so the root too). Empty
// without DXC (no caching then).
static std::string tool_id(int dbg) {
  static std::string base;
  static std::once_flag once;
  std::call_once(once, [] {
    Dxc* d = dxc();
    if (!d) return;
    std::string bytes;
    {
      FILE* f = nullptr;
      if (fopen_s(&f, g_dxc_path.c_str(), "rb") == 0 && f) {
        char buf[1 << 16];
        size_t n;
        while ((n = fread(buf, 1, sizeof buf, f)) > 0) bytes.append(buf, n);
        fclose(f);
      }
    }
    if (bytes.empty()) return;
    std::string version = "unknown";
    IDxcVersionInfo* vi = nullptr;
    if (SUCCEEDED(d->compiler->QueryInterface(IID_PPV_ARGS(&vi))) && vi) {
      UINT32 major = 0, minor = 0;
      vi->GetVersion(&major, &minor);
      version = std::to_string(major) + "." + std::to_string(minor);
      IDxcVersionInfo2* v2 = nullptr;
      if (SUCCEEDED(vi->QueryInterface(IID_PPV_ARGS(&v2))) && v2) {
        UINT32 commits = 0;
        char* hash = nullptr;
        if (SUCCEEDED(v2->GetCommitInfo(&commits, &hash)) && hash) {
          version += " " + std::to_string(commits) + " " + hash;
          CoTaskMemFree(hash);
        }
        v2->Release();
      }
      vi->Release();
    }
    base = "[" + sc::json_string(sc::sha256_hex(bytes.data(), bytes.size())) + ", " + sc::json_string(version) + ", " +
           sc::json_string(kArgsTemplate) + ", " + (sharc_dir().empty() ? "false" : "true");
  });
  if (base.empty()) return {};
  const std::string id = base + ", " + std::to_string(dbg) + ", " + sc::json_string(dbg ? root_dir() : std::string()) + "]";
  return sc::sha256_hex(id.data(), id.size());
}

// The source tree the keys are computed from; replaced by shader_poll when shaders/ or common/ change.
static std::mutex g_tree_mu;
static std::shared_ptr<const sc::Tree> g_tree;

static std::shared_ptr<const sc::Tree> source_tree(bool rebuild = false) {
  std::lock_guard<std::mutex> lock(g_tree_mu);
  if (!g_tree || rebuild) g_tree = sc::Tree::from_disk(root_dir(), sharc_dir());
  return g_tree;
}

// The cache folder (empty: caching off).
static const std::string& cache_dir() {
  static std::string dir;
  static std::once_flag once;
  std::call_once(once, [] {
    char buf[MAX_PATH] = {};
    std::string env = GetEnvironmentVariableA("BG3RTX_SHADER_CACHE", buf, MAX_PATH) ? buf : "";
    std::string lower = env;
    for (char& c : lower) c = char(tolower(static_cast<unsigned char>(c)));
    if (lower == "none" || lower == "0" || lower == "off") {
      log("[shaders] cache: off (BG3RTX_SHADER_CACHE=%s)", env.c_str());
      return;
    }
    std::string d;
    if (!env.empty()) {
      d = env + "\\runtime";   // beside tools/shader_identity.py's sqlite cache, never in it
    } else {
      char la[MAX_PATH] = {};
      if (GetEnvironmentVariableA("LOCALAPPDATA", la, MAX_PATH)) d = std::string(la) + "\\BG3RTX\\shader_cache";
    }
    if (d.empty() || !sc::ensure_dir(d)) {
      log("[shaders] cache: no usable folder (%s): every shader compiles", d.c_str());
      return;
    }
    dir = d;
  });
  return dir;
}

// DXC's arguments of `file` (see kArgsTemplate). False when the source is unreadable or the compile fails.
static bool compile(const ShaderEntry& e, std::vector<uint32_t>& out) {
  Dxc* d = dxc();
  if (!d) return false;
  const size_t q = e.file.find('?');
  std::vector<std::wstring> defines;
  for (size_t a = q; a != std::string::npos && a + 1 < e.file.size();) {
    const size_t b = e.file.find(';', a + 1);
    defines.push_back(widen(e.file.substr(a + 1, b == std::string::npos ? std::string::npos : b - a - 1)));
    a = b;
  }
  std::wstring path = widen(shaders_dir() + "\\" + e.file.substr(0, q));
  IDxcBlobEncoding* src = nullptr;
  if (FAILED(d->utils->LoadFile(path.c_str(), nullptr, &src))) {
    log("[shaders] cannot read %s", e.file.c_str());
    return false;
  }
  std::wstring entry = widen(e.entry), profile = widen(e.profile), inc = widen(shaders_dir());
  std::vector<LPCWSTR> args = {path.c_str(), L"-spirv", L"-fspv-target-env=vulkan1.3", L"-HV", L"2021",
                               L"-T", profile.c_str(), L"-E", entry.c_str(), L"-I", inc.c_str(),
                               L"-D", L"BG3RTX=1"};
  const std::wstring sharc = widen(sharc_dir());
  if (!sharc.empty()) {
    args.push_back(L"-I");
    args.push_back(sharc.c_str());
    args.push_back(L"-D");
    args.push_back(L"BG3RTX_SHARC_SDK=1");
  }
  for (const std::wstring& def : defines) {
    args.push_back(L"-D");
    args.push_back(def.c_str());
  }
  // A file whose first line is "// dxc: <arguments>" gets those DXC arguments (the SHaRC
  // passes: -enable-16bit-types, which its headers require).
  std::vector<std::wstring> extra;
  {
    const char* p = static_cast<const char*>(src->GetBufferPointer());
    const size_t n = src->GetBufferSize();
    static const char kTag[] = "// dxc:";
    if (n >= sizeof kTag - 1 && std::strncmp(p, kTag, sizeof kTag - 1) == 0) {
      std::string l(p + sizeof kTag - 1, p + n);
      l = l.substr(0, l.find_first_of("\r\n"));
      for (size_t i = 0; i < l.size();) {
        const size_t a = l.find_first_not_of(" \t", i);
        if (a == std::string::npos) break;
        const size_t b = l.find_first_of(" \t", a);
        extra.push_back(widen(l.substr(a, b == std::string::npos ? std::string::npos : b - a)));
        i = b == std::string::npos ? l.size() : b;
      }
    }
  }
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
  HRESULT hr = d->compiler->Compile(&buf, args.data(), UINT32(args.size()), d->include, IID_PPV_ARGS(&result));
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
      out.assign(words, words + obj->GetBufferSize() / 4);
      ok = !out.empty();
      obj->Release();
    }
  }
  result->Release();
  return ok;
}

// Fetches (cache) or compiles `e` for the sources of `tree`. Called without g_mu, with e.busy set
// (or from shader_poll, which holds g_mu: nobody else touches `e` then). Unchanged inputs keep the
// code and the generation.
static void build(ShaderEntry& e, const sc::Tree& tree, bool reload) {
  const uint64_t t0 = now_us();
  const std::string tool = tool_id(cfg_int("shader_debug", 0));
  const std::string key = tool.empty() ? std::string() : sc::request_key(tree, tool, e.file, e.entry, e.profile);
  if (!key.empty() && key == e.key) return;
  const std::string& dir = cache_dir();
  std::vector<uint32_t> code;
  const bool hit = !key.empty() && !dir.empty() && sc::load(dir, key, code);
  const bool ok = hit || compile(e, code);
  if (ok && !hit && !key.empty() && !dir.empty()) sc::store(dir, key, code);
  const double ms = double(now_us() - t0) / 1000.0;
  g_build_us += now_us() - t0;
  (hit ? g_hits : ok ? g_compiled : g_failed) += 1;
  if (ok) {
    if (!e.code.empty()) e.retired_code.push_back(std::move(e.code));
    e.code.swap(code);
    e.key = key;
    ++e.generation;
    aftermath_note_spirv((e.file + ":" + e.entry).c_str(), e.code.data(), e.code.size() * 4, nullptr);
  }
  if (!hit || reload)
    log("[shaders] %s:%s %s in %.0f ms%s (generation %llu)", e.file.c_str(), e.entry.c_str(),
        hit ? "read from the cache" : ok ? "compiled" : "FAILED, keeping previous", ms, t_prefetch ? ", prefetch" : "",
        static_cast<unsigned long long>(e.generation));
}

// ---- the request manifest: <cache>\requests.txt, "<sessions unseen>\t<file>\t<entry>\t<profile>" ----
struct Request {
  std::string file, entry, profile;
  int unseen = 0;
};
static std::mutex g_manifest_mu;
static std::vector<Request> g_manifest;
constexpr int kManifestSessions = 5;   // a request unused for this many sessions is not prefetched

static void manifest_write() {
  const std::string& dir = cache_dir();
  if (dir.empty()) return;
  std::string text;
  for (const Request& r : g_manifest) text += std::to_string(r.unseen) + "\t" + r.file + "\t" + r.entry + "\t" + r.profile + "\n";
  const std::string path = dir + "\\requests.txt", tmp = path + "." + std::to_string(GetCurrentProcessId()) + ".tmp";
  if (write_file(tmp, text.data(), text.size()) && !MoveFileExA(tmp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING))
    DeleteFileA(tmp.c_str());
}

// The previous sessions' requests (each one session older); what this session asks for is reset to 0.
static std::vector<Request> manifest_read() {
  std::lock_guard<std::mutex> lock(g_manifest_mu);
  const std::string& dir = cache_dir();
  if (dir.empty()) return {};
  FILE* f = nullptr;
  if (fopen_s(&f, (dir + "\\requests.txt").c_str(), "rb") == 0 && f) {
    char line[1024];
    while (fgets(line, sizeof line, f)) {
      std::string l(line);
      while (!l.empty() && (l.back() == '\n' || l.back() == '\r')) l.pop_back();
      size_t a = l.find('\t'), b = a == std::string::npos ? a : l.find('\t', a + 1),
             c = b == std::string::npos ? b : l.find('\t', b + 1);
      if (c == std::string::npos) continue;
      Request r{l.substr(a + 1, b - a - 1), l.substr(b + 1, c - b - 1), l.substr(c + 1), std::atoi(l.c_str()) + 1};
      if (r.unseen < kManifestSessions && !r.file.empty()) g_manifest.push_back(r);
    }
    fclose(f);
  }
  return g_manifest;
}

static void manifest_note(const ShaderEntry& e) {
  std::lock_guard<std::mutex> lock(g_manifest_mu);
  for (Request& r : g_manifest)
    if (r.file == e.file && r.entry == e.entry && r.profile == e.profile) {
      if (r.unseen == 0) return;
      r.unseen = 0;
      manifest_write();
      return;
    }
  g_manifest.push_back({e.file, e.entry, e.profile, 0});
  manifest_write();
}

static void start_prefetch();

bool shader_get(const char* file, const char* entry, const char* profile, Bg3ShaderBlob* out) {
  start_prefetch();
  const uint64_t t0 = now_us();
  std::string key = std::string(file) + "|" + entry + "|" + profile;
  std::unique_lock<std::mutex> lock(g_mu);
  ShaderEntry& e = g_entries.try_emplace(key, ShaderEntry{file, entry, profile}).first->second;   // stable reference
  const bool note = !t_prefetch && !e.requested;
  if (note) e.requested = true;
  while (e.busy) g_cv.wait(lock);
  if (!e.built) {
    e.busy = true;
    lock.unlock();
    build(e, *source_tree(), false);
    lock.lock();
    e.built = true;
    e.busy = false;
    g_cv.notify_all();
  }
  const bool ok = !e.code.empty();
  if (ok) {
    out->code = e.code.data();
    out->size_bytes = e.code.size() * 4;
    out->generation = e.generation;
  }
  lock.unlock();
  if (!t_prefetch) g_render_wait_us += now_us() - t0;
  if (note && ok) manifest_note(e);   // a failed request is never prefetched again (its FAILED line would fail every run)
  return ok;
}

static void start_prefetch() {
  static std::once_flag once;
  std::call_once(once, [] {
    if (cache_dir().empty()) return;
    std::vector<Request> reqs = manifest_read();
    // Keep the layer loaded while the thread runs (the loader may unload layers at vkDestroyInstance).
    HMODULE self = nullptr;
    GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
                       reinterpret_cast<LPCSTR>(&start_prefetch), &self);
    std::thread([reqs = std::move(reqs)] {
      SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);
      t_prefetch = true;
      const uint64_t t0 = now_us();
      const sc::PruneStats st = sc::prune(cache_dir(), 512ull << 20);
      const uint64_t h0 = g_hits, c0 = g_compiled;
      for (const Request& r : reqs) {
        Bg3ShaderBlob blob{};
        shader_get(r.file.c_str(), r.entry.c_str(), r.profile.c_str(), &blob);
      }
      log("[shaders] cache: prefetched %zu requests of the last %d sessions in %.0f ms (%llu read, %llu compiled); "
          "%s: %zu entries, %.1f MB, %zu pruned",
          reqs.size(), kManifestSessions, double(now_us() - t0) / 1000.0, (unsigned long long)(g_hits - h0),
          (unsigned long long)(g_compiled - c0), cache_dir().c_str(), st.files, double(st.bytes) / 1048576.0, st.removed);
      dxc_release_thread();
    }).detach();
  });
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

static void log_stats() {
  const uint64_t hits = g_hits, compiled = g_compiled, failed = g_failed;
  const uint64_t total = hits + compiled + failed;
  const ULONGLONG now = GetTickCount64();
  // Once the counts have been stable for 2 s: a startup burst gives one line.
  static uint64_t last_seen = ~0ull;
  if (total != last_seen) {
    last_seen = total;
    g_stats_tick = now;
    return;
  }
  if (total == g_logged_total || now - g_stats_tick < 2000) return;
  g_logged_total = total;
  log("[shaders] cache: %llu hits, %llu compiled, %llu failed, %.0f ms building (render thread waited %.0f ms in all)",
      (unsigned long long)hits, (unsigned long long)compiled, (unsigned long long)failed, double(g_build_us) / 1000.0,
      double(g_render_wait_us) / 1000.0);
}

void shader_poll() {
  ULONGLONG now = GetTickCount64();
  if (now - g_last_check < 250) return;
  g_last_check = now;
  log_stats();
  uint64_t stamp = (std::max)(dir_stamp(shaders_dir()), dir_stamp(root_dir() + "\\common"));
  if (stamp == g_stamp) return;
  bool first = g_stamp == 0;
  g_stamp = stamp;
  if (first) return;
  auto tree = source_tree(true);
  std::unique_lock<std::mutex> lock(g_mu);
  // References stay valid while the prefetch thread adds entries (iterators would not).
  std::vector<ShaderEntry*> entries;
  for (auto& [key, e] : g_entries) entries.push_back(&e);
  for (ShaderEntry* e : entries) {
    while (e->busy) g_cv.wait(lock);
    if (!e->built) continue;
    if (e->requested) build(*e, *tree, true);
    else e->built = false;   // only prefetched: rebuilt (or found unchanged) when the renderer asks
  }
}

}  // namespace bg3
