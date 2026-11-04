// Exercise the runtime manifest/build path with a private source tree and cache, no game.
#include "../layer/shaders.cpp"
#include <cstdarg>
#include <filesystem>
#include <fstream>
#include <chrono>

namespace bg3 {
static std::string test_root, test_bin;
static std::mutex logs_mu;
static std::condition_variable logs_cv;
static std::string logs;
static bool prefetch_done = false;
const std::string& root_dir() { return test_root; }
const std::string& bin_dir() { return test_bin; }
int cfg_int(const char*, int fallback) { return fallback; }
void aftermath_note_spirv(const char*, const void*, size_t, const char*) {}
bool write_file(const std::string& path, const void* data, size_t size) {
  std::ofstream f(path, std::ios::binary);
  f.write(static_cast<const char*>(data), size);
  return bool(f);
}
void log(const char* fmt, ...) {
  char text[16384];
  va_list args;
  va_start(args, fmt);
  vsnprintf(text, sizeof text, fmt, args);
  va_end(args);
  std::lock_guard<std::mutex> lock(logs_mu);
  logs += std::string(text) + "\n";
  if (std::strstr(text, "cache: prefetched")) prefetch_done = true;
  logs_cv.notify_all();
}
}

// Do not use assert: CTest runs release builds too.
#define CHECK(c) do { if (!(c)) { std::fprintf(stderr, "FAIL line %d: %s\n%s", __LINE__, #c, bg3::logs.c_str()); return 1; } } while (0)

int main() {
  using namespace bg3;
  namespace fs = std::filesystem;
  SetPriorityClass(GetCurrentProcess(), BELOW_NORMAL_PRIORITY_CLASS);
  char temp[MAX_PATH], sdk[MAX_PATH];
  GetTempPathA(MAX_PATH, temp);
  CHECK(GetEnvironmentVariableA("VULKAN_SDK", sdk, MAX_PATH));
  test_root = std::string(temp) + "bg3rtx-prefetch-" + std::to_string(GetCurrentProcessId());
  test_bin = std::string(sdk) + "\\Bin";
  fs::create_directories(test_root + "/shaders/rt");
  fs::create_directories(test_root + "/cache/runtime");
  SetEnvironmentVariableA("BG3RTX_SHADER_CACHE", (test_root + "/cache").c_str());
  const std::string valid = "[numthreads(1,1,1)] void main() {}\n";
  CHECK(write_file(test_root + "/shaders/entry.hlsl", valid.data(), valid.size()));
  const std::string broken = "#error intentional real compile failure\n";
  CHECK(write_file(test_root + "/shaders/broken.hlsl", broken.data(), broken.size()));
  const std::string manifest =
      "0\trt/hair_primary_probe.hlsl?OLD=1\tmain\tcs_6_6\n"
      "0\trt/hair_primary_probe.hlsl?OLD=1\tmain\tcs_6_6\n"
      "0\tentry.hlsl\tremoved\tcs_6_6\n"
      "0\tbroken.hlsl\tmain\tcs_6_6\n";
  const std::string manifest_path = test_root + "/cache/runtime/requests.txt";
  CHECK(write_file(manifest_path, manifest.data(), manifest.size()));
  Bg3ShaderBlob blob{};
  CHECK(shader_get("entry.hlsl", "main", "cs_6_6", &blob));
  {
    std::unique_lock<std::mutex> lock(logs_mu);
    CHECK(logs_cv.wait_for(lock, std::chrono::seconds(30), [] { return prefetch_done; }));
  }
  std::ifstream f(manifest_path);
  const std::string persisted((std::istreambuf_iterator<char>(f)), {});
  CHECK(persisted.find("hair_primary_probe") == std::string::npos);
  CHECK(persisted.find("\tremoved\t") == std::string::npos);
  CHECK(persisted.find("broken.hlsl") != std::string::npos);
  CHECK(persisted.find("entry.hlsl\tmain") != std::string::npos);
  const std::string dropped = "[shaders] prefetch: dropped stale request rt/hair_primary_probe.hlsl?OLD=1:main (source removed)";
  CHECK(logs.find(dropped) != std::string::npos);
  CHECK(logs.find(dropped, logs.find(dropped) + 1) == std::string::npos);
  CHECK(logs.find("prefetch: dropped stale request entry.hlsl:removed (entry point removed)") != std::string::npos);
  CHECK(logs.find("cannot read rt/hair_primary_probe") == std::string::npos);
  CHECK(logs.find("entry.hlsl:removed FAILED") == std::string::npos);
  CHECK(logs.find("broken.hlsl:main FAILED, keeping previous") != std::string::npos);
  CHECK(g_failed == 1);
  // After a stale prefetch, renderer demand must still compile and report the failure.
  CHECK(!shader_get("rt/hair_primary_probe.hlsl?OLD=1", "main", "cs_6_6", &blob));
  CHECK(!shader_get("entry.hlsl", "removed", "cs_6_6", &blob));
  CHECK(logs.find("rt/hair_primary_probe.hlsl?OLD=1:main FAILED, keeping previous") != std::string::npos);
  CHECK(logs.find("entry.hlsl:removed FAILED, keeping previous") != std::string::npos);
  CHECK(g_failed == 3);
  // Model renderer demand registered while a prefetch build is in flight.
  // requested is set before shader_get waits on busy; it must veto pruning.
  for (const char* key : {"rt/hair_primary_probe.hlsl?OLD=1|main|cs_6_6", "entry.hlsl|removed|cs_6_6"}) {
    ShaderEntry& e = g_entries.at(key);
    CHECK(e.requested);
    manifest_note(e);
    e.built = false;
    t_prefetch = true;
    CHECK(!shader_get(e.file.c_str(), e.entry.c_str(), e.profile.c_str(), &blob));
    t_prefetch = false;
    CHECK(e.built);
    CHECK(std::count_if(g_manifest.begin(), g_manifest.end(), [&](const Request& r) {
      return r.file == e.file && r.entry == e.entry && r.profile == e.profile;
    }) == 1);
  }
  CHECK(g_failed == 5);
  f.close();
  fs::remove_all(test_root);
  std::puts("shader prefetch: removed source, removed entry, duplicate, real failure and renderer demand passed");
  return 0;
}
