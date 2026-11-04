#include "../common/log_stats.h"
#include <cstdio>
#include <cstdlib>
#include <map>
#include <string>
static std::map<std::string, int> keys;
static int cfg(const char* key, int fallback) {
  auto it = keys.find(key);
  return it == keys.end() ? fallback : it->second;
}
#define CHECK(x) do { if (!(x)) { std::printf("FAIL line %d: %s\n", __LINE__, #x); return 1; } } while (0)
int main() {
  using namespace bg3::logging;
  CHECK(stats_enabled(cfg)); // Developer defaults, including diagnostics builds.
  keys = {{"dev", 0}, {"rt", 1}, {"gi", 1}, {"ls_lights", 1}, {"rt_omm", 1}};
  CHECK(!stats_enabled(cfg)); // Shipping effects do not request developer statistics.
  CHECK(!sdk_info_enabled(cfg));
  for (const char* key : {"detailed_logging", "dev", "rt_stats", "ls_stats", "rt_stats_period_frames"}) {
    keys[key] = 1;
    CHECK(stats_enabled(cfg));
    keys[key] = 0;
    CHECK(!stats_enabled(cfg));
  }
  keys["rt_stats_period_frames"] = -1;
  CHECK(!stats_enabled(cfg));
  keys["sl_verbose"] = 1;
  CHECK(sdk_info_enabled(cfg));
  CHECK(!stats_enabled(cfg));
  keys["sl_verbose"] = 0;
  keys["detailed_logging"] = 1;
  CHECK(sdk_info_enabled(cfg));
  keys["detailed_logging"] = 0;
  keys["dev"] = 1;
  CHECK(sdk_info_enabled(cfg));
  std::puts("logging policy passed");
}
