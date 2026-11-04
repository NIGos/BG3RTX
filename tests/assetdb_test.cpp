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
#include "../layer/sebridge.h"

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

int main(int argc, char** argv) {
  if (argc == 3 && (std::string(argv[1]) == "--expect-invalid" || std::string(argv[1]) == "--expect-valid")) {
    SetEnvironmentVariableA("BG3RTX_ASSETDB", argv[2]);
    return bg3::assetdb_api()->available() == (std::string(argv[1]) == "--expect-valid") ? 0 : 1;
  }
  if (argc == 2 && std::string(argv[1]) == "--malformed") {
    // Minimal valid file and overflow/truncation mutations. Each process gets a fresh lazy loader.
    std::vector<unsigned char> valid(97, 0);
    memcpy(valid.data(), "BG3RTXDB", 8);
    auto put = [](std::vector<unsigned char>& b, size_t at, uint64_t v, size_t n) {
      memcpy(b.data() + at, &v, n);
    };
    put(valid, 8, 1, 4); put(valid, 12, 64, 4); put(valid, 16, 1, 4);
    put(valid, 24, 64, 8); put(valid, 32, valid.size(), 8);
    memcpy(valid.data() + 64, "STRS", 4); put(valid, 72, 96, 8); put(valid, 80, 1, 8);
    char temp[MAX_PATH], file[MAX_PATH];
    CHECK(GetTempPathA(MAX_PATH, temp));
    CHECK(GetTempFileNameA(temp, "adb", 0, file));
    for (unsigned test = 0; test < 8; ++test) {
      auto bytes = valid;
      switch (test) {
        case 0: put(bytes, 24, UINT64_MAX - 15, 8); break; // TOC sum wraps
        case 1: put(bytes, 24, 98, 8); break; // offset past EOF
        case 2: put(bytes, 72, UINT64_MAX, 8); put(bytes, 80, 2, 8); break;
        case 3: put(bytes, 72, 98, 8); break;
        case 4: memcpy(bytes.data() + 64, "LVLS", 4);
                put(bytes, 72, UINT64_MAX - 3, 8); put(bytes, 80, 80, 8);
                put(bytes, 88, 1, 4); put(bytes, 92, 80, 4); break;
        case 5: put(bytes, 80, 0, 8); break; // empty strings
        case 6: bytes.back() = 'x'; break; // missing terminator
      }
      FILE* f = fopen(file, "wb");
      CHECK(f);
      if (!f) break;
      CHECK(fwrite(bytes.data(), 1, bytes.size(), f) == bytes.size());
      CHECK(fclose(f) == 0);
      std::string cmd = "\"" + std::string(argv[0]) + "\" " + (test == 7 ? "--expect-valid" : "--expect-invalid") + " \"" + file + "\"";
      STARTUPINFOA si{}; si.cb = sizeof si;
      PROCESS_INFORMATION pi{};
      const bool started = CreateProcessA(nullptr, cmd.data(), nullptr, nullptr, FALSE,
                                          CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi) != FALSE;
      CHECK(started);
      if (started) {
        CHECK(WaitForSingleObject(pi.hProcess, 30000) == WAIT_OBJECT_0);
        DWORD code = 1; CHECK(GetExitCodeProcess(pi.hProcess, &code)); CHECK(code == 0);
        CloseHandle(pi.hThread); CloseHandle(pi.hProcess);
      }
    }
    DeleteFileA(file);
    printf("assetdb: 7 malformed and 1 valid fixture, %d failures\n", g_fail);
    return g_fail ? 1 : 0;
  }
  // Through the layer's own lookup: a worktree without data/assetdb uses the main checkout's.
  std::string path = bg3::assetdb_file();
  printf("database: %s\n", path.c_str());
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

  // Roof volumes (v3, the RT sun's trust test): sorted by distance, and the foundry floor
  // point lies inside one of the oriented boxes (they contain their triggers).
  {
    std::vector<Bg3DbVolume> v(256);
    uint32_t nv = a->roof_volumes(floor0, 120.0f, v.data(), 256);
    CHECK(nv > 0);
    bool inside = false;
    uint32_t kinds = 0;
    for (uint32_t i = 0; i < nv && i < 256; ++i) {
      const Bg3DbVolume& b = v[i];
      kinds |= b.kind;
      float rel[3] = {floor0[0] - b.center[0], floor0[1] - b.center[1], floor0[2] - b.center[2]};
      const float x = -b.rot[0], y = -b.rot[1], z = -b.rot[2], w = b.rot[3];   // world -> local (conjugate)
      float tx = 2 * (y * rel[2] - z * rel[1]), ty = 2 * (z * rel[0] - x * rel[2]), tz = 2 * (x * rel[1] - y * rel[0]);
      float l[3] = {rel[0] + w * tx + (y * tz - z * ty), rel[1] + w * ty + (z * tx - x * tz),
                    rel[2] + w * tz + (x * ty - y * tx)};
      if (std::fabs(l[0]) <= b.half[0] && std::fabs(l[1]) <= b.half[1] && std::fabs(l[2]) <= b.half[2]) inside = true;
    }
    CHECK(inside);
    printf("roof volumes within 120 m of the foundry floor: %u (kinds 0x%x), the floor point inside one: %d\n", nv,
           kinds, int(inside));
  }

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

  // ---- Script Extender bridge (API v2, layer/sebridge.cpp) ----
  {
    bg3::SeSnapshot s;
    CHECK(!bg3::se_parse_lights("BG3RTX_SE 1\nseq 1 ms 0 level X host 0 0 0\nL 1 1 0 0 0 0 1 1 1 1 1 0 1 1 0 0 0 0 a\n", s));
    CHECK(bg3::se_parse_lights("BG3RTX_SE 1\nseq 7 ms 0 level X host 1 2 3 avg_us 5\nL 9 2 1 1 2 3 4 1 1 1 10 0 9 191 4 0 0 0 Lamp\nEND\n", s));
    CHECK(s.seq == 7 && s.lights.size() == 1 && s.lights[0].owner == 2 && s.lights[0].type == 1 &&
          s.lights[0].flags == 9 && s.lights[0].channel == 191 && std::string(s.lights[0].name) == "Lamp" &&
          s.host[2] == 3.0f && s.avg_us == 5.0f);
    bg3::SeSnapshot w;
    CHECK(bg3::se_parse_world("BG3RTX_SE_WORLD 1\nlevel L region R\nT A ATM_X 1234 5678\nEND\n", w));
    CHECK(w.triggers.size() == 1 && w.triggers[0].kind == 'A' && std::string(w.triggers[0].current) == "1234" &&
          std::string(w.region) == "R");

    // Without an export: the database's placed lights alone.
    Bg3RtLight rl[256];
    Bg3SeInfo info;
    CHECK(!a->se_info(&info) && !info.active);
    uint32_t n_db = a->runtime_lights(cam, 40.0f, rl, 256);
    uint32_t idx[256];
    CHECK(n_db == a->lights_near(cam, 40.0f, idx, 256) && n_db > 0);
    CHECK(n_db && (rl[0].flags & BG3_RTL_FROM_DB) && rl[0].owner == BG3_RTL_OWNER_PLACED && rl[0].db_index == idx[0]);

    // With a complete export (written after process start, so it is fresh): a
    // placed light at a database light's position, an item lamp, the character light.
    const Bg3DbLight* D = a->light(idx[0]);
    char dir[MAX_PATH];
    GetTempPathA(MAX_PATH, dir);
    std::string se_dir = std::string(dir) + "bg3rtx_se_test";
    CreateDirectoryA(se_dir.c_str(), nullptr);
    char text[2048];
    snprintf(text, sizeof text,
             "BG3RTX_SE 1\nseq 3 ms 0 level CTY_Main_A host %.3f %.3f %.3f avg_us 1 max_us 2 all 3 classified 3 pending 0 out 3\n"
             "L 11 1 0 %.4f %.4f %.4f %.3f 1 1 1 1000 0 1 191 0 0 0 0 %s\n"
             "L 12 2 0 %.4f %.4f %.4f 6 1 1 1 30000 0 9 191 4 0 0 0 LightSources_GEN_Glasshouse_Lamp_A\n"
             "L 13 4 1 %.4f %.4f %.4f 24 1 1 1 4000 0 257 129 0 0 0 0 Light_Player_Darkvision_Spot_Drow\nEND\n",
             cam[0], cam[1], cam[2], D->pos[0], D->pos[1], D->pos[2], D->range, a->string(D->name), cam[0] + 1,
             cam[1], cam[2], cam[0], cam[1] + 2, cam[2]);
    FILE* f = fopen((se_dir + "\\lights.txt").c_str(), "wb");
    fwrite(text, 1, strlen(text), f);
    fclose(f);
    SetEnvironmentVariableA("BG3RTX_SE_DIR", se_dir.c_str());
    bg3::se_stop();   // restart the reader on the test folder
    bg3::se_latest();
    for (int i = 0; i < 50 && !a->se_info(&info); ++i) Sleep(50);
    CHECK(info.active && info.seq == 3 && info.lights_out == 3);
    uint32_t n_se = a->runtime_lights(cam, 40.0f, rl, 256);
    CHECK(n_se == 3);   // database lights in the bridge's range that it did not report are not loaded
    int placed = -1, item = -1, chr = -1;
    for (uint32_t i = 0; i < n_se && i < 256; ++i) {
      if (rl[i].owner == BG3_RTL_OWNER_PLACED) placed = int(i);
      if (rl[i].owner == BG3_RTL_OWNER_ITEM) item = int(i);
      if (rl[i].owner == BG3_RTL_OWNER_CHARACTER) chr = int(i);
    }
    CHECK(placed >= 0 && (rl[placed].flags & BG3_RTL_FROM_SE) && (rl[placed].flags & BG3_RTL_FROM_DB) &&
          rl[placed].db_index == idx[0]);
    CHECK(item >= 0 && std::fabs(rl[item].source_radius - 0.10f) < 1e-6f && (rl[item].flags & BG3_RTL_FLICKER));
    CHECK(chr >= 0 && (rl[chr].flags & BG3_RTL_PLAYER_LIGHT) && rl[chr].type == 1);
    // Legacy/partial exports still supply their explicit records, but cannot
    // remove the database lights that they did not report.
    std::string legacy(text);
    legacy.replace(legacy.find("seq 3"), 5, "seq 4");
    const auto completeness = legacy.find(" classified 3 pending 0");
    CHECK(completeness != std::string::npos);
    legacy.erase(completeness, strlen(" classified 3 pending 0"));
    f = fopen((se_dir + "\\lights.txt").c_str(), "wb");
    fwrite(legacy.data(), 1, legacy.size(), f);
    fclose(f);
    for (int i = 0; i < 50 && (!a->se_info(&info) || info.seq != 4); ++i) Sleep(50);
    CHECK(info.active && info.seq == 4 && info.lights_out == 3);
    const uint32_t n_legacy = a->runtime_lights(cam, 40.0f, rl, 256);
    CHECK(n_legacy == n_db + 2); // One matched DB placement, two additional explicit lights.
    uint32_t explicit_count = 0;
    for (uint32_t i = 0; i < n_legacy; ++i) explicit_count += (rl[i].flags & BG3_RTL_FROM_SE) != 0;
    CHECK(explicit_count == 3);
    const char *atm = nullptr, *lit = nullptr;
    a->se_environment(cam, &atm, &lit);
    printf("se: %u runtime lights; environment at the camera: atmosphere '%s', lighting '%s'\n", n_se, atm, lit);
    bg3::se_stop();
    DeleteFileA((se_dir + "\\lights.txt").c_str());
    RemoveDirectoryA(se_dir.c_str());
  }
  printf("%s (%d failures)\n", g_fail ? "FAILED" : "OK", g_fail);
  return g_fail ? 1 : 0;
}
