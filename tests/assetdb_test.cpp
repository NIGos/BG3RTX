// Offline test of the layer's asset database loader (layer/assetdb.cpp) against a built
// database: python -m tools.assets build --out data/assetdb, then bin/assetdb_test.exe.
// Skips (exit 0) when the database is missing. Host functions the loader uses are
// stubbed here; the join check (GPU copies) is not exercised.
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "../layer/assetdb.h"

namespace bg3 {
void log(const char* fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  vprintf(fmt, ap);
  va_end(ap);
  printf("\n");
}
const std::string& root_dir() {
  static const std::string r = BG3RTX_SOURCE_DIR;
  return r;
}
std::string now_stamp() { return "test"; }
void make_dirs(const std::string&) {}
bool write_file(const std::string&, const void*, size_t) { return true; }
int cfg_int(const char*, int fallback) { return fallback; }
float cfg_float(const char*, float fallback) { return fallback; }
bool query_binding(VkCommandBuffer, VkPipelineBindPoint, uint32_t, uint32_t, uint32_t, Bg3Binding*) { return false; }
DeviceData* device_of(const void*) { return nullptr; }
bool alloc_host_buffer(VkDeviceSize, VkBufferUsageFlags, VkBuffer*, VkDeviceMemory*, void**) { return false; }
uint64_t frame_index() { return 0; }
Tracker& tracker() {
  static Tracker t;
  return t;
}
void defer_destroy(Bg3DeferKind, uint64_t) {}
}  // namespace bg3

static int g_fail = 0;
#define CHECK(c)                                                    \
  do {                                                              \
    if (!(c)) {                                                     \
      printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c);           \
      ++g_fail;                                                     \
    }                                                               \
  } while (0)

int main() {
  std::string path = std::string(BG3RTX_SOURCE_DIR) + "/data/assetdb/bg3rtx_assetdb.bin";
  if (FILE* f = fopen(path.c_str(), "rb")) {
    fclose(f);
  } else {
    printf("SKIP: %s not built\n", path.c_str());
    return 0;
  }
  const Bg3AssetDbApi* a = bg3::assetdb_api();
  CHECK(a->version == BG3RTX_ASSETDB_API_VERSION && a->struct_size == sizeof(Bg3AssetDbApi));
  CHECK(a->available());
  const char *name = nullptr, *src = nullptr;
  CHECK(a->current_level(&name, &src) == -1);  // no save on this command line
  CHECK(!bg3::assetdb_select_level("NoSuchLevel", "test"));
  CHECK(bg3::assetdb_select_level("BGH_SteelWatchFoundry_B", "test"));  // a sub-level selects its world
  CHECK(a->current_level(&name, &src) >= 0 && std::string(name) == "CTY_Main_A");

  // Foundry camera (TRACK_D 1): 16 placed lights within 25 m (tools/assets build, CTY_LowerCity_A)
  const float cam[3] = {-320.9f, 19.9f, -132.5f};
  uint32_t idx[256];
  uint32_t n = a->lights_near(cam, 25.0f, idx, 256);
  CHECK(n == 16);
  float prev = 0;
  for (uint32_t i = 0; i < n && i < 256; ++i) {
    const Bg3DbLight* l = a->light(idx[i]);
    float d = std::sqrt((l->pos[0] - cam[0]) * (l->pos[0] - cam[0]) + (l->pos[1] - cam[1]) * (l->pos[1] - cam[1]) +
                        (l->pos[2] - cam[2]) * (l->pos[2] - cam[2]));
    CHECK(d <= 25.0f && d >= prev);
    prev = d;
  }
  // Box query = brute force over the level
  const float bmin[3] = {cam[0] - 10, cam[1] - 10, cam[2] - 10}, bmax[3] = {cam[0] + 10, cam[1] + 10, cam[2] + 10};
  std::vector<uint32_t> box(4096);
  uint32_t nb = a->lights_in_box(bmin, bmax, box.data(), 4096);
  uint32_t brute = 0;
  {  // every light of the level (lights_near with a huge radius), tested directly
    std::vector<uint32_t> all(1 << 16);
    uint32_t na = a->lights_near(cam, 1e6f, all.data(), uint32_t(all.size()));
    CHECK(na > 1000);
    for (uint32_t k = 0; k < na && k < all.size(); ++k) {
      const Bg3DbLight* l = a->light(all[k]);
      float q = 0;
      for (int c = 0; c < 3; ++c) {
        float v = std::fmax(bmin[c] - l->pos[c], std::fmax(0.0f, l->pos[c] - bmax[c]));
        q += v * v;
      }
      if (q <= l->range * l->range) ++brute;
    }
  }
  CHECK(nb == brute && nb > 0);
  printf("lights near the foundry camera: %u within 25 m, %u reach the 20 m box\n", n, nb);

  // Rooms: the foundry ground floor is a covered floor (F0 below F1) inside the foundry atmosphere
  Bg3RoomInfo r{};
  const float floor0[3] = {-315.0f, 6.0f, -140.0f};
  uint32_t fl = a->room_at(floor0, &r);
  CHECK(fl & BG3_ROOM_COVERED_FLOOR);
  CHECK(r.atmosphere != UINT32_MAX && a->preset(r.atmosphere));
  printf("room_at foundry floor: flags %u floor %d building '%s' lighting %u atmosphere %u\n", fl, r.floor, r.building,
         r.lighting, r.atmosphere);

  // Materials: the deferred FS of Base_VT (AUDIT_ENGINE_DATA 2.1)
  Bg3MaterialInfo m{};
  CHECK(a->material(0x14678345c10aefbcull, &m));
  CHECK(std::string(m.template_name) == "Base/Base_VT");
  CHECK(m.stage == 4 && m.pass == 0 && m.instanced == 1 && m.cls == BG3_MAT_CLASS_OPAQUE);
  CHECK(m.slot[BG3_MAT_VT_ALBEDO].kind == BG3_MAT_KIND_VT_CACHE && m.slot[BG3_MAT_VT_ALBEDO].binding == 5);
  CHECK(m.slot[BG3_MAT_VT_NORMAL].binding == 4 && m.slot[BG3_MAT_VT_PHYSICAL].binding == 6);
  CHECK(m.slot[BG3_MAT_VT_TABLE].set == 1 && m.slot[BG3_MAT_VT_TABLE].binding == 3);
  CHECK(m.slot[BG3_MAT_REFLECTANCE].kind == BG3_MAT_KIND_SCALAR && m.slot[BG3_MAT_REFLECTANCE].offset == 0);
  CHECK(!a->material(0x1234, &m));
  const char* eng = a->engine_shader(0xf469db7865d7c524ull);
  CHECK(eng && std::string(eng) == "ClusteredLighting/ClusteredLighting_0");

  // Probes near the camera carry SH for at least one lighting
  uint32_t pr[64];
  uint32_t np = a->probes_near(cam, 60.0f, pr, 64);
  CHECK(np > 0);
  if (np) {
    const Bg3DbProbe* p = a->probe(pr[0]);
    CHECK(p && p->capture_count > 0 && a->probe_capture(p->first_capture));
  }
  printf("%s (%d failures)\n", g_fail ? "FAILED" : "OK", g_fail);
  return g_fail ? 1 : 0;
}
