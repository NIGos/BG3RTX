// Decodes an Nsight Aftermath GPU crash dump written by the layer (aftermath=1,
// layer/aftermath.cpp) into JSON plus a short summary.
//
//   bin\aftermath_decode.exe captures\aftermath\<time>.nv-gpudmp [out.json]
//
// Next to the dump it uses: shader_map.txt (Aftermath shader hash -> SPIR-V file,
// and what the shader is: ours "gi/rt_shadow.hlsl:main", or "game <hash>"), the
// shader debug info files <id0>-<id1>.nvdbg, and captures/shaders/ (the game's
// SPIR-V with dump_shaders=1). The summary prints the page fault (VA, type, access,
// engine, client), every resource Aftermath tracked at that VA (also destroyed
// ones), and the active shaders with their names.
#include <GFSDK_Aftermath.h>
#include <GFSDK_Aftermath_GpuCrashDump.h>
#include <GFSDK_Aftermath_GpuCrashDumpDecoding.h>

#include <cstdio>
#include <cstring>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

namespace {

struct Ctx {
  std::string dir;
  std::map<uint64_t, std::pair<std::string, std::string>> shaders;   // hash -> path, what
};

bool read_file(const std::string& path, std::vector<char>& out) {
  std::ifstream f(path, std::ios::binary);
  if (!f) return false;
  out.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
  return true;
}

void GFSDK_AFTERMATH_CALL debug_info_lookup(const GFSDK_Aftermath_ShaderDebugInfoIdentifier* id,
                                            PFN_GFSDK_Aftermath_SetData set, void* user) {
  auto* c = static_cast<Ctx*>(user);
  char name[64];
  snprintf(name, sizeof name, "%016llx-%016llx.nvdbg", (unsigned long long)id->id[0], (unsigned long long)id->id[1]);
  std::vector<char> d;
  if (read_file(c->dir + "\\" + name, d)) set(d.data(), uint32_t(d.size()));
}

void GFSDK_AFTERMATH_CALL shader_lookup(const GFSDK_Aftermath_ShaderBinaryHash* h, PFN_GFSDK_Aftermath_SetData set,
                                        void* user) {
  auto* c = static_cast<Ctx*>(user);
  auto it = c->shaders.find(h->hash);
  if (it == c->shaders.end()) return;
  std::vector<char> d;
  if (read_file(it->second.first, d)) set(d.data(), uint32_t(d.size()));
}

const char* shader_type(GFSDK_Aftermath_ShaderType t) {
  switch (t) {
    case GFSDK_Aftermath_ShaderType_Vertex: return "vertex";
    case GFSDK_Aftermath_ShaderType_Pixel: return "pixel";
    case GFSDK_Aftermath_ShaderType_Compute: return "compute";
    default: return "other";
  }
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 2) {
    fprintf(stderr, "usage: aftermath_decode <dump.nv-gpudmp> [out.json]\n");
    return 2;
  }
  const std::string dump = argv[1];
  const std::string out = argc > 2 ? argv[2] : dump + ".json";
  Ctx c;
  const size_t slash = dump.find_last_of("\\/");
  c.dir = slash == std::string::npos ? "." : dump.substr(0, slash);
  {
    std::ifstream m(c.dir + "\\shader_map.txt");
    std::string line;
    while (std::getline(m, line)) {
      // "hash<TAB>path<TAB>what"; older maps used spaces (paths with spaces: split at ".spv ").
      std::string hx, path, what;
      const size_t t1 = line.find('\t');
      if (t1 != std::string::npos) {
        const size_t t2 = line.find('\t', t1 + 1);
        hx = line.substr(0, t1);
        path = line.substr(t1 + 1, t2 == std::string::npos ? std::string::npos : t2 - t1 - 1);
        what = t2 == std::string::npos ? "" : line.substr(t2 + 1);
      } else {
        const size_t s1 = line.find(' '), e = line.find(".spv ");
        if (s1 == std::string::npos || e == std::string::npos) continue;
        hx = line.substr(0, s1);
        path = line.substr(s1 + 1, e + 4 - s1 - 1);
        what = line.substr(e + 5);
      }
      if (!what.empty() && what.back() == '\r') what.pop_back();
      if (!hx.empty()) c.shaders[std::stoull(hx, nullptr, 16)] = {path, what};
    }
  }
  std::vector<char> data;
  if (!read_file(dump, data)) {
    fprintf(stderr, "cannot read %s\n", dump.c_str());
    return 1;
  }
  GFSDK_Aftermath_GpuCrashDump_Decoder dec{};
  GFSDK_Aftermath_Result r =
      GFSDK_Aftermath_GpuCrashDump_CreateDecoder(GFSDK_Aftermath_Version_API, data.data(), uint32_t(data.size()), &dec);
  if (!GFSDK_Aftermath_SUCCEED(r)) {
    fprintf(stderr, "CreateDecoder failed: 0x%08x\n", unsigned(r));
    return 1;
  }
  printf("dump %s: %zu bytes, %zu shaders in the map\n", dump.c_str(), data.size(), c.shaders.size());

  // Page fault and the resources at its VA.
  GFSDK_Aftermath_GpuCrashDump_PageFaultInfo pf{};
  if (GFSDK_Aftermath_SUCCEED(GFSDK_Aftermath_GpuCrashDump_GetPageFaultInfo(dec, &pf))) {
    printf("page fault at VA 0x%llx: fault type %d, access %d, engine %d, client %d, %u resources\n",
           (unsigned long long)pf.faultingGpuVA, int(pf.faultType), int(pf.accessType), int(pf.engine), int(pf.client),
           pf.resourceInfoCount);
    std::vector<GFSDK_Aftermath_GpuCrashDump_ResourceInfo> ri(pf.resourceInfoCount);
    if (!ri.empty() && GFSDK_Aftermath_SUCCEED(GFSDK_Aftermath_GpuCrashDump_GetPageFaultResourceInfo(dec, uint32_t(ri.size()), ri.data())))
      for (const auto& x : ri)
        printf("  resource 0x%llx (api handle 0x%llx, \"%s\"): VA 0x%llx +%llu, %ux%ux%u mips %u format %u, %s, "
               "created tick %llu, destroyed tick %llu\n",
               (unsigned long long)x.gpuVa, (unsigned long long)x.apiResource, x.debugName,
               (unsigned long long)x.gpuVa, (unsigned long long)x.size, x.width, x.height, x.depth, x.mipLevels, x.format,
               x.bWasDestroyed ? "DESTROYED" : "live", (unsigned long long)x.createTick,
               (unsigned long long)x.destroyTick);
  } else {
    printf("no page fault info\n");
  }

  // Active shaders.
  uint32_t ns = 0;
  if (GFSDK_Aftermath_SUCCEED(GFSDK_Aftermath_GpuCrashDump_GetActiveShadersInfoCount(dec, &ns)) && ns) {
    std::vector<GFSDK_Aftermath_GpuCrashDump_ShaderInfo> si(ns);
    if (GFSDK_Aftermath_SUCCEED(GFSDK_Aftermath_GpuCrashDump_GetActiveShadersInfo(dec, ns, si.data()))) {
      printf("%u active shaders:\n", ns);
      for (const auto& s : si) {
        GFSDK_Aftermath_ShaderBinaryHash h{};
        std::string name = s.isInternal ? "(driver-internal)" : "?";
        if (!s.isInternal && GFSDK_Aftermath_SUCCEED(GFSDK_Aftermath_GetShaderHashForShaderInfo(dec, &s, &h))) {
          auto it = c.shaders.find(h.hash);
          name = it != c.shaders.end() ? it->second.second : "not in the shader map";
        }
        printf("  %s shader, hash 0x%016llx (binary 0x%016llx): %s\n", shader_type(s.shaderType),
               (unsigned long long)s.shaderHash, (unsigned long long)h.hash, name.c_str());
      }
    }
  }

  // Full JSON.
  uint32_t size = 0;
  r = GFSDK_Aftermath_GpuCrashDump_GenerateJSON(dec, GFSDK_Aftermath_GpuCrashDumpDecoderFlags_ALL_INFO,
                                                GFSDK_Aftermath_GpuCrashDumpFormatterFlags_NONE, &debug_info_lookup,
                                                &shader_lookup, nullptr, &c, &size);
  if (GFSDK_Aftermath_SUCCEED(r) && size) {
    std::vector<char> json(size);
    GFSDK_Aftermath_GpuCrashDump_GetJSON(dec, size, json.data());
    std::ofstream f(out, std::ios::binary);
    f.write(json.data(), std::streamsize(strnlen(json.data(), json.size())));
    printf("JSON written to %s (%u bytes)\n", out.c_str(), size);
  } else {
    fprintf(stderr, "GenerateJSON failed: 0x%08x\n", unsigned(r));
  }
  GFSDK_Aftermath_GpuCrashDump_DestroyDecoder(dec);
  return 0;
}
