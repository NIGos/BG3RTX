// Offline asset database: loader, queries, level detection and the runtime join check.
// File format: tools/assets/dbformat.py (docs/ASSET_DB.md). The core reaches it through
// Bg3HostApi::assetdb (common/assetdb_api.h).
//
// Loading is lazy: the first query (or the first join check) reads
// data/assetdb/bg3rtx_assetdb.bin (or %BG3RTX_ASSETDB%) in one go (8 MB) and validates
// every section against the record sizes below. Per-level spatial grids are built when
// the level becomes current.
//
// Level detection, cheapest first:
//   1. the save the game was started with (`-loadSaveGame <folder>`, as auto-run passes it;
//      else the newest save for `-continueGame`): its SaveInfo.json "Current Level";
//   2. runtime light positions: at each join check every light of the lighting pass's
//      array votes for the database levels that have a light within 5 cm of it, so a level
//      change after a load (or a wrong hint) is picked up.
#include "assetdb.h"

#include <shellapi.h>  // CommandLineToArgvW
#pragma comment(lib, "shell32.lib")

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace bg3 {

std::atomic<bool> g_assetdb_armed{false};
std::atomic<bool> g_assetdb_counting{false};

namespace {

// ---- file records (tools/assets/dbformat.py RECORDS) ----
#pragma pack(push, 4)
struct RHeader {
  char magic[8];
  uint32_t version, header_size, section_count, flags;
  uint64_t toc_offset, file_size, build_time;
  uint8_t digest[16];
};
struct RToc {
  char tag[4];
  uint32_t version;
  uint64_t offset, size;
  uint32_t count, record_size;
};
struct RLevel {
  uint32_t name, lighting, atmosphere, flags;
  uint32_t first_inst, n_inst, first_light, n_light, first_trig, n_trig, first_probe, n_probe;
  float bmin[3], bmax[3];
  uint32_t lighting_id, atmosphere_id;
};
struct RInst {
  uint32_t name;
  int32_t parent;
  float pos[3], rot[4], scale;
  uint32_t flags, depth;
};
struct RAlias {
  uint32_t name, level, instance;
};
struct RTrig {
  float pos[3], scale, rot[4], extents[3], height, radius, fade_time;
  uint32_t flags;
  int32_t floor;
  uint8_t kind, shape, render_method, pad;
  uint32_t instance, name, building, first_point, point_count, first_ref, ref_count;
};
struct RTpl {
  uint32_t name, flags;
  uint8_t cls, shading, blend, material_type, usage, raster, depth, uv;
  uint32_t diffusion, pad[3];
};
struct RVar {
  uint32_t tpl;
  uint8_t variant, ecb_set, ecb_binding, mcb_set, mcb_binding, pad;
  uint16_t param_count, ecb_size, mcb_size;
  uint32_t first_param, pad2;
};
struct RPrm {
  uint8_t role, kind, set, binding;
  uint16_t offset;
  uint8_t layer, flags;
  uint32_t name;
  float value[4];
  uint32_t pad;
};
#pragma pack(pop)
#pragma pack(push, 8)
struct RShd {
  uint64_t hash;
  uint32_t first_ref;
  uint16_t ref_count;
  uint8_t stage, pass;
  uint32_t canonical, pad;
};
struct REsh {
  uint64_t hash;
  uint32_t name;
  uint8_t stage, pad[3];
};
#pragma pack(pop)
static_assert(sizeof(RHeader) == 64 && sizeof(RToc) == 32 && sizeof(RLevel) == 80 && sizeof(RInst) == 48 &&
                  sizeof(RAlias) == 12 && sizeof(RTrig) == 96 && sizeof(RTpl) == 32 && sizeof(RVar) == 24 &&
                  sizeof(RPrm) == 32 && sizeof(RShd) == 24 && sizeof(REsh) == 16,
              "records must match tools/assets/dbformat.py");

enum { KIND_ROOM, KIND_PORTAL, KIND_FLOOR, KIND_LIGHTING, KIND_ATMOSPHERE };
enum { TF_ROOF = 1, TF_ROOM_HAS_ROOF = 16, TF_COVERED = 32 };
constexpr uint32_t kNone = 0xFFFFFFFFu;

template <class T>
struct View {
  const T* p = nullptr;
  uint32_t n = 0;
  const T& operator[](uint32_t i) const { return p[i]; }
};

struct Db {
  std::vector<uint8_t> bytes;
  View<RLevel> levels;
  View<RInst> inst;
  View<RAlias> alias;
  View<Bg3DbLight> lights;
  View<RTrig> trigs;
  View<float> tpts;   // n = number of (x, z) points; p is the float array
  View<uint32_t> tref;
  View<Bg3DbProbe> probes;
  View<Bg3DbProbeCapture> caps;
  View<Bg3DbPreset> presets;
  View<RTpl> tpl;
  View<RVar> var;
  View<RPrm> prm;
  View<RShd> shd;
  View<uint32_t> sref;
  View<REsh> esh;
  const char* strs = "";
  uint32_t strs_size = 1;
  std::unordered_map<std::string, int> level_by_name;
  std::unordered_map<std::string, int> world_of_sublevel;   // first world that contains it
  std::vector<int32_t> light_level;                          // level of every light

  const char* str(uint32_t ref) const { return ref < strs_size ? strs + ref : ""; }
};

// Spatial hash over light centres.
struct Grid {
  float cell = 16.0f;
  std::unordered_map<int64_t, std::vector<uint32_t>> cells;
  std::vector<uint32_t> big;   // lights whose range exceeds kBigRange: always tested by box queries
  static int64_t key(int x, int y, int z) {
    return (int64_t(x & 0x1FFFFF) << 42) | (int64_t(y & 0x1FFFFF) << 21) | int64_t(z & 0x1FFFFF);
  }
  int c(float v) const { return int(std::floor(v / cell)); }
};
constexpr float kBigRange = 48.0f;

std::mutex g_load_mu;
std::unique_ptr<Db> g_db;
bool g_load_tried = false;

std::shared_mutex g_level_mu;
int g_level = -1;
const char* g_level_source = "none";   // static strings only: handed out to callers
Grid g_grid;          // of g_level
Grid g_vote_grid;     // every light of every level, 0.5 m cells (level detection)
bool g_vote_ready = false;

// ---- loading ----

std::string db_path() {
  char env[MAX_PATH] = {};
  if (GetEnvironmentVariableA("BG3RTX_ASSETDB", env, MAX_PATH)) return env;
  return root_dir() + "\\data\\assetdb\\bg3rtx_assetdb.bin";
}

template <class T>
bool bind(Db& db, const std::unordered_map<std::string, RToc>& toc, const char* tag, size_t rsize, View<T>& v,
          std::string& err) {
  auto it = toc.find(tag);
  if (it == toc.end()) return true;  // an empty section is simply absent
  const RToc& t = it->second;
  if (t.record_size != rsize || t.offset % 4 || t.offset + t.size > db.bytes.size() ||
      uint64_t(t.count) * rsize != t.size) {
    err = std::string("section ") + tag + " has an unexpected layout (rebuild with python -m tools.assets build)";
    return false;
  }
  v.p = reinterpret_cast<const T*>(db.bytes.data() + t.offset);
  v.n = t.count;
  return true;
}

std::unique_ptr<Db> load(std::string& err) {
  const std::string path = db_path();
  FILE* f = nullptr;
  if (fopen_s(&f, path.c_str(), "rb") != 0 || !f) {
    err = "not found: " + path + " (build it with: python -m tools.assets build --out data/assetdb)";
    return nullptr;
  }
  auto db = std::make_unique<Db>();
  _fseeki64(f, 0, SEEK_END);
  long long n = _ftelli64(f);
  _fseeki64(f, 0, SEEK_SET);
  if (n < (long long)sizeof(RHeader) || n > (1ll << 30)) {
    fclose(f);
    err = "bad size";
    return nullptr;
  }
  db->bytes.resize(size_t(n));
  size_t got = fread(db->bytes.data(), 1, db->bytes.size(), f);
  fclose(f);
  if (got != db->bytes.size()) return err = "short read", nullptr;
  RHeader h;
  memcpy(&h, db->bytes.data(), sizeof h);
  if (memcmp(h.magic, "BG3RTXDB", 8) != 0) return err = "not an asset database", nullptr;
  if (h.version != 1) return err = "format version " + std::to_string(h.version) + " (this layer reads 1)", nullptr;
  if (h.file_size != db->bytes.size() || h.toc_offset + uint64_t(h.section_count) * sizeof(RToc) > h.file_size)
    return err = "truncated", nullptr;
  std::unordered_map<std::string, RToc> toc;
  for (uint32_t i = 0; i < h.section_count; ++i) {
    RToc t;
    memcpy(&t, db->bytes.data() + h.toc_offset + i * sizeof(RToc), sizeof t);
    toc[std::string(t.tag, 4)] = t;
  }
  bool ok = bind(*db, toc, "LVLS", sizeof(RLevel), db->levels, err) && bind(*db, toc, "INST", sizeof(RInst), db->inst, err) &&
            bind(*db, toc, "ALIA", sizeof(RAlias), db->alias, err) &&
            bind(*db, toc, "LGHT", sizeof(Bg3DbLight), db->lights, err) &&
            bind(*db, toc, "TRIG", sizeof(RTrig), db->trigs, err) && bind(*db, toc, "TPTS", 8, db->tpts, err) &&
            bind(*db, toc, "TREF", 4, db->tref, err) && bind(*db, toc, "PRBE", sizeof(Bg3DbProbe), db->probes, err) &&
            bind(*db, toc, "PCAP", sizeof(Bg3DbProbeCapture), db->caps, err) &&
            bind(*db, toc, "PRES", sizeof(Bg3DbPreset), db->presets, err) && bind(*db, toc, "MTPL", sizeof(RTpl), db->tpl, err) &&
            bind(*db, toc, "MVAR", sizeof(RVar), db->var, err) && bind(*db, toc, "MPRM", sizeof(RPrm), db->prm, err) &&
            bind(*db, toc, "SHDR", sizeof(RShd), db->shd, err) && bind(*db, toc, "SREF", 4, db->sref, err) &&
            bind(*db, toc, "ESHD", sizeof(REsh), db->esh, err);
  if (!ok) return nullptr;
  auto s = toc.find("STRS");
  if (s == toc.end() || s->second.offset + s->second.size > db->bytes.size() || s->second.size == 0 ||
      db->bytes[s->second.offset + s->second.size - 1] != 0)
    return err = "string section missing or unterminated", nullptr;
  db->strs = reinterpret_cast<const char*>(db->bytes.data() + s->second.offset);
  db->strs_size = uint32_t(s->second.size);
  // Range checks of the cross references the queries follow.
  auto bad = [&](const char* what) { err = std::string("inconsistent ") + what; return false; };
  for (uint32_t i = 0; i < db->levels.n; ++i) {
    const RLevel& l = db->levels[i];
    if (uint64_t(l.first_light) + l.n_light > db->lights.n || uint64_t(l.first_trig) + l.n_trig > db->trigs.n ||
        uint64_t(l.first_probe) + l.n_probe > db->probes.n || uint64_t(l.first_inst) + l.n_inst > db->inst.n)
      return bad("level ranges"), nullptr;
    db->level_by_name[db->str(l.name)] = int(i);
  }
  db->light_level.assign(db->lights.n, -1);
  for (uint32_t i = 0; i < db->levels.n; ++i)
    for (uint32_t k = 0; k < db->levels[i].n_light; ++k) db->light_level[db->levels[i].first_light + k] = int32_t(i);
  for (uint32_t i = 0; i < db->trigs.n; ++i) {
    const RTrig& t = db->trigs[i];
    if (uint64_t(t.first_point) + t.point_count > db->tpts.n || uint64_t(t.first_ref) + t.ref_count > db->tref.n)
      return bad("trigger ranges"), nullptr;
  }
  for (uint32_t i = 0; i < db->probes.n; ++i)
    if (uint64_t(db->probes[i].first_capture) + db->probes[i].capture_count > db->caps.n) return bad("probe ranges"), nullptr;
  for (uint32_t i = 0; i < db->var.n; ++i)
    if (uint64_t(db->var[i].first_param) + db->var[i].param_count > db->prm.n || db->var[i].tpl >= db->tpl.n)
      return bad("material variants"), nullptr;
  for (uint32_t i = 0; i < db->shd.n; ++i) {
    const RShd& e = db->shd[i];
    if (uint64_t(e.first_ref) + e.ref_count > db->sref.n || e.canonical >= db->var.n) return bad("shader refs"), nullptr;
    if (i && db->shd[i - 1].hash >= e.hash) return bad("shader order"), nullptr;
  }
  for (uint32_t i = 1; i < db->esh.n; ++i)
    if (db->esh[i - 1].hash >= db->esh[i].hash) return bad("engine shader order"), nullptr;
  for (uint32_t i = 0; i < db->alias.n; ++i) {
    const RAlias& a = db->alias[i];
    if (a.level < db->levels.n) db->world_of_sublevel.emplace(db->str(a.name), int(a.level));
  }
  log("[assetdb] loaded %s: %.1f MB, %u levels, %u lights, %u triggers, %u probes, %u material shaders, %u engine "
      "shaders",
      path.c_str(), double(db->bytes.size()) / 1e6, db->levels.n, db->lights.n, db->trigs.n, db->probes.n, db->shd.n,
      db->esh.n);
  return db;
}

std::string current_level_from_save();
void set_level(int level, const char* source);

Db* db() {
  std::lock_guard<std::mutex> lock(g_load_mu);
  if (!g_load_tried) {
    g_load_tried = true;
    if (!cfg_int("assetdb", 1)) {
      log("[assetdb] disabled (assetdb=0)");
      return nullptr;
    }
    std::string err;
    g_db = load(err);
    if (!g_db) {
      log("[assetdb] not available: %s", err.c_str());
      return nullptr;
    }
    std::string lv = current_level_from_save();
    if (!lv.empty()) {
      auto it = g_db->level_by_name.find(lv);
      if (it == g_db->level_by_name.end()) {
        auto s = g_db->world_of_sublevel.find(lv);
        if (s != g_db->world_of_sublevel.end()) it = g_db->level_by_name.find(g_db->str(g_db->levels[s->second].name));
      }
      if (it != g_db->level_by_name.end()) {
        set_level(it->second, "save");   // takes g_level_mu only
      } else {
        log("[assetdb] the save's level '%s' is not in the database", lv.c_str());
      }
    }
  }
  return g_db.get();
}

// ---- level state ----

void build_grid(const Db& d, int level, Grid& g) {
  g.cells.clear();
  g.big.clear();
  if (level < 0) return;
  const RLevel& l = d.levels[level];
  for (uint32_t i = l.first_light; i < l.first_light + l.n_light; ++i) {
    const Bg3DbLight& L = d.lights[i];
    g.cells[Grid::key(g.c(L.pos[0]), g.c(L.pos[1]), g.c(L.pos[2]))].push_back(i);
    if (L.range > kBigRange) g.big.push_back(i);
  }
}

void set_level(int level, const char* source) {
  std::unique_lock<std::shared_mutex> lock(g_level_mu);
  if (level == g_level) {
    g_level_source = source;
    return;
  }
  g_level = level;
  g_level_source = source;
  build_grid(*g_db, level, g_grid);
  const RLevel& l = g_db->levels[level];
  log("[assetdb] level %s (from %s): %u lights, %u triggers, %u probes", g_db->str(l.name), source, l.n_light,
      l.n_trig, l.n_probe);
}

// ---- small math ----

void qrot_inv(const float q[4], const float v[3], float out[3]) {
  // rotate by the conjugate: (-x, -y, -z, w)
  const float x = -q[0], y = -q[1], z = -q[2], w = q[3];
  float tx = 2 * (y * v[2] - z * v[1]), ty = 2 * (z * v[0] - x * v[2]), tz = 2 * (x * v[1] - y * v[0]);
  out[0] = v[0] + w * tx + (y * tz - z * ty);
  out[1] = v[1] + w * ty + (z * tx - x * tz);
  out[2] = v[2] + w * tz + (x * ty - y * tx);
}

float dist2(const float a[3], const float b[3]) {
  float x = a[0] - b[0], y = a[1] - b[1], z = a[2] - b[2];
  return x * x + y * y + z * z;
}

bool inside_trigger(const Db& d, const RTrig& t, const float p[3]) {
  float rel[3] = {p[0] - t.pos[0], p[1] - t.pos[1], p[2] - t.pos[2]}, l[3];
  qrot_inv(t.rot, rel, l);
  const float s = t.scale > 1e-6f ? t.scale : 1.0f;
  l[0] /= s;
  l[1] /= s;
  l[2] /= s;
  if (t.shape == 2) {  // box: Extents is the full size, centred
    return std::fabs(l[0]) <= 0.5f * t.extents[0] && std::fabs(l[1]) <= 0.5f * t.extents[1] &&
           std::fabs(l[2]) <= 0.5f * t.extents[2];
  }
  if (t.shape == 1) return l[0] * l[0] + l[1] * l[1] + l[2] * l[2] <= t.radius * t.radius;
  if (t.point_count < 3 || l[1] < 0.0f || l[1] > t.height) return false;
  bool in = false;  // prism: polygon in local x/z, extruded upwards by Height
  const float* P = d.tpts.p + 2 * t.first_point;
  for (uint32_t i = 0, j = t.point_count - 1; i < t.point_count; j = i++) {
    float xi = P[2 * i], zi = P[2 * i + 1], xj = P[2 * j], zj = P[2 * j + 1];
    if ((zi > l[2]) != (zj > l[2]) && l[0] < (xj - xi) * (l[2] - zi) / (zj - zi) + xi) in = !in;
  }
  return in;
}

// ---- API ----

bool api_available() { return db() != nullptr; }

int32_t api_current_level(const char** name, const char** source) {
  Db* d = db();
  std::shared_lock<std::shared_mutex> lock(g_level_mu);
  if (name) *name = (d && g_level >= 0) ? d->str(d->levels[g_level].name) : "";
  if (source) *source = g_level >= 0 ? g_level_source : "none";
  return d ? g_level : -1;
}

uint32_t api_lights_near(const float pos[3], float radius, uint32_t* out, uint32_t max) {
  Db* d = db();
  if (!d) return 0;
  std::shared_lock<std::shared_mutex> lock(g_level_mu);
  if (g_level < 0) return 0;
  std::vector<std::pair<float, uint32_t>> hits;
  const Grid& g = g_grid;
  const float r2 = radius * radius;
  int x0 = g.c(pos[0] - radius), x1 = g.c(pos[0] + radius), y0 = g.c(pos[1] - radius), y1 = g.c(pos[1] + radius),
      z0 = g.c(pos[2] - radius), z1 = g.c(pos[2] + radius);
  if (int64_t(x1 - x0 + 1) * (y1 - y0 + 1) * (z1 - z0 + 1) > 4096) {  // huge radius: scan the level
    const RLevel& l = d->levels[g_level];
    for (uint32_t i = l.first_light; i < l.first_light + l.n_light; ++i)
      if (float q = dist2(pos, d->lights[i].pos); q <= r2) hits.push_back({q, i});
  } else {
    for (int x = x0; x <= x1; ++x)
      for (int y = y0; y <= y1; ++y)
        for (int z = z0; z <= z1; ++z) {
          auto it = g.cells.find(Grid::key(x, y, z));
          if (it == g.cells.end()) continue;
          for (uint32_t i : it->second)
            if (float q = dist2(pos, d->lights[i].pos); q <= r2) hits.push_back({q, i});
        }
  }
  std::sort(hits.begin(), hits.end());
  for (uint32_t k = 0; k < max && k < hits.size(); ++k) out[k] = hits[k].second;
  return uint32_t(hits.size());
}

uint32_t api_lights_in_box(const float bmin[3], const float bmax[3], uint32_t* out, uint32_t max) {
  Db* d = db();
  if (!d) return 0;
  std::shared_lock<std::shared_mutex> lock(g_level_mu);
  if (g_level < 0) return 0;
  uint32_t n = 0;
  auto test = [&](uint32_t i) {
    const Bg3DbLight& L = d->lights[i];
    float q = 0;
    for (int k = 0; k < 3; ++k) {
      float v = std::max(bmin[k] - L.pos[k], std::max(0.0f, L.pos[k] - bmax[k]));
      q += v * v;
    }
    if (q <= L.range * L.range) {
      if (n < max) out[n] = i;
      ++n;
    }
  };
  const Grid& g = g_grid;
  const float m = kBigRange;
  int x0 = g.c(bmin[0] - m), x1 = g.c(bmax[0] + m), y0 = g.c(bmin[1] - m), y1 = g.c(bmax[1] + m),
      z0 = g.c(bmin[2] - m), z1 = g.c(bmax[2] + m);
  if (int64_t(x1 - x0 + 1) * (y1 - y0 + 1) * (z1 - z0 + 1) > 8192) {
    const RLevel& l = d->levels[g_level];
    for (uint32_t i = l.first_light; i < l.first_light + l.n_light; ++i) test(i);
    return n;
  }
  for (int x = x0; x <= x1; ++x)
    for (int y = y0; y <= y1; ++y)
      for (int z = z0; z <= z1; ++z) {
        auto it = g.cells.find(Grid::key(x, y, z));
        if (it == g.cells.end()) continue;
        for (uint32_t i : it->second)
          if (d->lights[i].range <= kBigRange) test(i);
      }
  for (uint32_t i : g.big) test(i);
  return n;
}

const Bg3DbLight* api_light(uint32_t i) {
  Db* d = db();
  return d && i < d->lights.n ? &d->lights[i] : nullptr;
}

const RShd* find_shader(const Db& d, uint64_t h) {
  const RShd* b = d.shd.p;
  const RShd* e = d.shd.p + d.shd.n;
  const RShd* it = std::lower_bound(b, e, h, [](const RShd& s, uint64_t v) { return s.hash < v; });
  return it != e && it->hash == h ? it : nullptr;
}

const REsh* find_engine(const Db& d, uint64_t h) {
  const REsh* b = d.esh.p;
  const REsh* e = d.esh.p + d.esh.n;
  const REsh* it = std::lower_bound(b, e, h, [](const REsh& s, uint64_t v) { return s.hash < v; });
  return it != e && it->hash == h ? it : nullptr;
}

bool api_material(uint64_t hash, Bg3MaterialInfo* out) {
  Db* d = db();
  if (!d || !out) return false;
  const RShd* s = find_shader(*d, hash);
  if (!s) return false;
  const RVar& v = d->var[s->canonical];
  const RTpl& t = d->tpl[v.tpl];
  *out = Bg3MaterialInfo{};
  out->template_name = d->str(t.name);
  out->cls = t.cls;
  out->flags = t.flags;
  out->shading_model = t.shading;
  out->stage = s->stage;
  out->pass = v.variant % 8;
  out->instanced = v.variant >= 8 ? 1 : 0;
  out->aliases = s->ref_count ? s->ref_count - 1u : 0u;
  out->engine_cb_set = v.ecb_set;
  out->engine_cb_binding = v.ecb_binding;
  out->material_cb_set = v.mcb_set;
  out->material_cb_binding = v.mcb_binding;
  out->engine_cb_size = v.ecb_size;
  out->material_cb_size = v.mcb_size;
  for (uint32_t k = v.first_param; k < v.first_param + v.param_count; ++k) {
    const RPrm& p = d->prm[k];
    if (p.role >= BG3_MAT_ROLE_COUNT || out->slot[p.role].kind) continue;  // first of each role
    Bg3MatSlot& o = out->slot[p.role];
    o.kind = p.kind;
    o.set = p.set;
    o.binding = p.binding;
    o.layer = p.layer;
    o.offset = p.offset;
    memcpy(o.value, p.value, sizeof o.value);
    o.name = d->str(p.name);
  }
  return true;
}

const char* api_engine_shader(uint64_t hash) {
  Db* d = db();
  if (!d) return nullptr;
  const REsh* e = find_engine(*d, hash);
  return e ? d->str(e->name) : nullptr;
}

uint32_t api_room_at(const float pos[3], Bg3RoomInfo* out) {
  Bg3RoomInfo r{};
  r.room = kNone;
  r.lighting = r.atmosphere = kNone;
  r.room_name = r.building = "";
  Db* d = db();
  if (d) {
    std::shared_lock<std::shared_mutex> lock(g_level_mu);
    if (g_level >= 0) {
      const RLevel& l = d->levels[g_level];
      r.lighting = l.lighting;
      r.atmosphere = l.atmosphere;
      float best_light = 1e30f, best_atm = 1e30f;
      for (uint32_t i = l.first_trig; i < l.first_trig + l.n_trig; ++i) {
        const RTrig& t = d->trigs[i];
        if (!inside_trigger(*d, t, pos)) continue;
        // smallest containing volume wins for nested lighting/atmosphere triggers
        float vol = t.shape == 2 ? t.extents[0] * t.extents[1] * t.extents[2] : t.height + t.radius;
        switch (t.kind) {
          case KIND_ROOM:
            r.flags |= BG3_ROOM_INSIDE;
            if (t.flags & (TF_ROOM_HAS_ROOF | TF_ROOF)) r.flags |= BG3_ROOM_ROOFED;
            if (r.room == kNone) {
              r.room = i;
              r.room_name = d->str(t.name);
            }
            break;
          case KIND_PORTAL: r.flags |= BG3_ROOM_PORTAL; break;
          case KIND_FLOOR:
            r.floor = t.floor;
            r.building = d->str(t.building);
            if (t.flags & TF_COVERED) r.flags |= BG3_ROOM_COVERED_FLOOR;
            if (t.flags & TF_ROOF) r.flags |= BG3_ROOM_ROOFED;
            break;
          case KIND_LIGHTING:
            if (t.ref_count && vol < best_light) {
              best_light = vol;
              r.lighting = d->tref[t.first_ref];
            }
            break;
          case KIND_ATMOSPHERE:
            if (t.ref_count && vol < best_atm) {
              best_atm = vol;
              r.atmosphere = d->tref[t.first_ref];
            }
            break;
        }
      }
    }
  }
  if (out) *out = r;
  return r.flags;
}

const Bg3DbPreset* api_preset(uint32_t i) {
  Db* d = db();
  return d && i < d->presets.n ? &d->presets[i] : nullptr;
}

uint32_t api_probes_near(const float pos[3], float radius, uint32_t* out, uint32_t max) {
  Db* d = db();
  if (!d) return 0;
  std::shared_lock<std::shared_mutex> lock(g_level_mu);
  if (g_level < 0) return 0;
  const RLevel& l = d->levels[g_level];
  std::vector<std::pair<float, uint32_t>> hits;
  for (uint32_t i = l.first_probe; i < l.first_probe + l.n_probe; ++i)
    if (float q = dist2(pos, d->probes[i].pos); q <= radius * radius) hits.push_back({q, i});
  std::sort(hits.begin(), hits.end());
  for (uint32_t k = 0; k < max && k < hits.size(); ++k) out[k] = hits[k].second;
  return uint32_t(hits.size());
}

const Bg3DbProbe* api_probe(uint32_t i) {
  Db* d = db();
  return d && i < d->probes.n ? &d->probes[i] : nullptr;
}

const Bg3DbProbeCapture* api_probe_capture(uint32_t i) {
  Db* d = db();
  return d && i < d->caps.n ? &d->caps[i] : nullptr;
}

const char* api_string(uint32_t ref) {
  Db* d = db();
  return d ? d->str(ref) : "";
}

const Bg3AssetDbApi g_api = {
    BG3RTX_ASSETDB_API_VERSION, uint32_t(sizeof(Bg3AssetDbApi)), &api_available, &api_current_level,
    &api_lights_near, &api_lights_in_box, &api_light, &api_material, &api_engine_shader, &api_room_at, &api_preset,
    &api_probes_near, &api_probe, &api_probe_capture, &api_string,
};

// ---- level from the save (LSPK v18 file list + SaveInfo.json) ----

bool lz4_block(const uint8_t* src, size_t n, std::vector<uint8_t>& dst, size_t expect) {
  dst.clear();
  dst.reserve(expect);
  size_t i = 0;
  while (i < n) {
    uint8_t token = src[i++];
    size_t lit = token >> 4;
    if (lit == 15)
      for (uint8_t b = 255; b == 255 && i < n;) lit += (b = src[i++]);
    if (i + lit > n) return false;
    dst.insert(dst.end(), src + i, src + i + lit);
    i += lit;
    if (i >= n) break;
    if (i + 2 > n) return false;
    size_t off = src[i] | (size_t(src[i + 1]) << 8);
    i += 2;
    size_t ml = token & 15;
    if (ml == 15)
      for (uint8_t b = 255; b == 255 && i < n;) ml += (b = src[i++]);
    ml += 4;
    if (off == 0 || off > dst.size() || dst.size() + ml > expect) return false;
    size_t from = dst.size() - off;
    for (size_t k = 0; k < ml; ++k) dst.push_back(dst[from + k]);
  }
  return dst.size() == expect;
}

std::string save_info_level(const std::wstring& lsv) {
  HANDLE h = CreateFileW(lsv.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
  if (h == INVALID_HANDLE_VALUE) return {};
  auto rd = [&](uint64_t off, void* p, DWORD n) {
    LARGE_INTEGER li;
    li.QuadPart = LONGLONG(off);
    DWORD got = 0;
    return SetFilePointerEx(h, li, nullptr, FILE_BEGIN) && ReadFile(h, p, n, &got, nullptr) && got == n;
  };
  std::string out;
  uint8_t hdr[40];
  if (rd(0, hdr, 40) && memcmp(hdr, "LSPK", 4) == 0 && *reinterpret_cast<uint32_t*>(hdr + 4) == 18) {
    uint64_t list_off;
    memcpy(&list_off, hdr + 8, 8);
    uint32_t cnt[2];
    if (rd(list_off, cnt, 8) && cnt[0] < 100000 && cnt[1] < (64u << 20)) {
      std::vector<uint8_t> comp(cnt[1]), list;
      if (rd(list_off + 8, comp.data(), cnt[1]) && lz4_block(comp.data(), comp.size(), list, size_t(cnt[0]) * 272)) {
        for (uint32_t i = 0; i < cnt[0]; ++i) {
          const uint8_t* e = list.data() + size_t(i) * 272;
          if (strncmp(reinterpret_cast<const char*>(e), "SaveInfo.json", 256) != 0) continue;
          uint32_t lo, disk, usize;
          uint16_t hi;
          memcpy(&lo, e + 256, 4);
          memcpy(&hi, e + 260, 2);
          uint8_t flags = e[263];
          memcpy(&disk, e + 264, 4);
          memcpy(&usize, e + 268, 4);
          if (disk > (16u << 20) || usize > (16u << 20)) break;
          std::vector<uint8_t> raw(disk), json;
          if (!rd(lo | (uint64_t(hi) << 32), raw.data(), disk)) break;
          if ((flags & 15) == 3) {
            // Patch 8 saves compress SaveInfo.json with zstd; the layer has no zstd decoder,
            // so the level comes from the runtime lights instead.
            log("[assetdb] the save's SaveInfo.json is zstd-compressed: level from runtime lights");
            break;
          }
          if ((flags & 15) == 0) json = raw;
          else if ((flags & 15) != 2 || !lz4_block(raw.data(), raw.size(), json, usize)) break;
          std::string s(json.begin(), json.end());
          size_t k = s.find("\"Current Level\"");
          if (k == std::string::npos) break;
          size_t q0 = s.find('"', s.find(':', k) + 1), q1 = q0 == std::string::npos ? q0 : s.find('"', q0 + 1);
          if (q1 != std::string::npos) out = s.substr(q0 + 1, q1 - q0 - 1);
          break;
        }
      }
    }
  }
  CloseHandle(h);
  return out;
}

std::wstring first_lsv(const std::wstring& folder) {
  WIN32_FIND_DATAW fd;
  HANDLE h = FindFirstFileW((folder + L"\\*.lsv").c_str(), &fd);
  if (h == INVALID_HANDLE_VALUE) return {};
  FindClose(h);
  return folder + L"\\" + fd.cFileName;
}

std::string current_level_from_save() {
  wchar_t la[MAX_PATH];
  if (!GetEnvironmentVariableW(L"LOCALAPPDATA", la, MAX_PATH)) return {};
  const std::wstring root =
      std::wstring(la) + L"\\Larian Studios\\Baldur's Gate 3\\PlayerProfiles\\Public\\Savegames\\Story";
  int argc = 0;
  LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
  std::wstring folder;
  bool cont = false;
  for (int i = 1; argv && i < argc; ++i) {
    if (_wcsicmp(argv[i], L"-loadSaveGame") == 0 && i + 1 < argc) folder = argv[i + 1];
    if (_wcsicmp(argv[i], L"-continueGame") == 0) cont = true;
  }
  if (argv) LocalFree(argv);
  std::wstring lsv;
  const char* how = "-loadSaveGame";
  if (!folder.empty()) {
    lsv = first_lsv(root + L"\\" + folder);
  } else if (cont) {  // the newest save (the game orders by its internal save time; close enough)
    how = "-continueGame (newest save)";
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW((root + L"\\*").c_str(), &fd);
    FILETIME best{};
    if (h != INVALID_HANDLE_VALUE) {
      do {
        if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) || fd.cFileName[0] == L'.') continue;
        std::wstring f = first_lsv(root + L"\\" + fd.cFileName);
        WIN32_FILE_ATTRIBUTE_DATA a;
        if (!f.empty() && GetFileAttributesExW(f.c_str(), GetFileExInfoStandard, &a) &&
            CompareFileTime(&a.ftLastWriteTime, &best) > 0) {
          best = a.ftLastWriteTime;
          lsv = f;
        }
      } while (FindNextFileW(h, &fd));
      FindClose(h);
    }
  }
  if (lsv.empty()) return {};
  std::string lv = save_info_level(lsv);
  log("[assetdb] save hint (%s): level '%s'", how, lv.c_str());
  return lv;
}

// ---- runtime join check ----

PFN_vkCmdCopyBuffer g_copy = nullptr;
VkBuffer g_rb = VK_NULL_HANDLE;
VkDeviceMemory g_rb_mem = VK_NULL_HANDLE;
uint8_t* g_rb_map = nullptr;
constexpr VkDeviceSize kRbSize = 12ull << 20;
// Regions of the readback buffer
constexpr VkDeviceSize kOffCam = 0, kOffScale = 256, kOffLights = 512, kOffProj = kOffLights + (1 << 20),
                       kOffHeads = kOffProj + (1 << 20), kOffIndex = kOffHeads + (1 << 20);
std::mutex g_join_mu;
struct Pending {
  uint64_t frame = 0;
  VkDeviceSize lights = 0, proj = 0, heads = 0, index = 0;   // bytes copied
  bool recorded = false;
};
Pending g_pending;
uint64_t g_next_check = 0;
uint32_t g_checks = 0;
// draw counts of the counting frame
std::mutex g_draw_mu;
std::unordered_map<uint64_t, uint32_t> g_draw_vs, g_draw_fs;

bool readback_ready(DeviceData* dd) {
  if (g_rb) return true;
  if (!g_copy) g_copy = reinterpret_cast<PFN_vkCmdCopyBuffer>(dd->gdpa(dd->device, "vkCmdCopyBuffer"));
  if (!g_copy) return false;
  void* m = nullptr;
  if (!alloc_host_buffer(kRbSize, VK_BUFFER_USAGE_TRANSFER_DST_BIT, &g_rb, &g_rb_mem, &m)) {
    log("[assetdb] join: readback buffer allocation failed");
    return false;
  }
  g_rb_map = static_cast<uint8_t*>(m);
  return true;
}

}  // namespace

const Bg3AssetDbApi* assetdb_api() { return &g_api; }

bool assetdb_select_level(const char* name, const char* source) {
  Db* d = db();
  if (!d || !name) return false;
  auto it = d->level_by_name.find(name);
  if (it == d->level_by_name.end()) {
    auto s = d->world_of_sublevel.find(name);
    if (s == d->world_of_sublevel.end()) return false;
    set_level(s->second, source);
    return true;
  }
  set_level(it->second, source);
  return true;
}

void assetdb_dispatch_pre(VkCommandBuffer cmd) {
  // Recognise the tiled lighting by its light list: set 1 b25 (80-byte lights), b29
  // (128-byte projectors), b23/b24 (froxel headers and index list), b13 (global light
  // scale) and set 0 b2 (camera, position at byte 208).
  Bg3Binding b25{}, b29{}, b23{}, b24{}, b13{}, cam{};
  auto q = [&](uint32_t set, uint32_t b, Bg3Binding& o) {
    return query_binding(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, set, b, 0, &o) && o.buffer && o.range > 0 &&
           o.range != VK_WHOLE_SIZE;
  };
  if (!q(1, 25, b25) || b25.range % 80 || b25.range < 80 * 256) return;
  if (!q(0, 2, cam) || cam.range < 224 || !q(1, 13, b13) || !q(1, 23, b23) || !q(1, 24, b24)) return;
  bool proj = q(1, 29, b29) && b29.range % 128 == 0;
  std::lock_guard<std::mutex> lock(g_join_mu);
  if (!g_assetdb_armed.load()) return;
  DeviceData* dd = device_of(cmd);
  if (!dd || !readback_ready(dd)) {
    g_assetdb_armed = false;
    return;
  }
  g_assetdb_armed = false;
  memset(g_rb_map, 0xFF, 512);  // sentinel: the copy must overwrite it
  VkMemoryBarrier mb{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
  mb.srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT | VK_ACCESS_HOST_WRITE_BIT;
  mb.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
  dd->vk.CmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT | VK_PIPELINE_STAGE_HOST_BIT,
                            VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 1, &mb, 0, nullptr, 0, nullptr);
  Pending p;
  auto copy = [&](const Bg3Binding& b, VkDeviceSize dst, VkDeviceSize cap) {
    VkBufferCopy c{b.offset, dst, std::min<VkDeviceSize>(b.range, cap)};
    g_copy(cmd, b.buffer, g_rb, 1, &c);
    return c.size;
  };
  copy(cam, kOffCam, 256);
  copy(b13, kOffScale, 256);
  p.lights = copy(b25, kOffLights, kOffProj - kOffLights);
  p.proj = proj ? copy(b29, kOffProj, kOffHeads - kOffProj) : 0;
  p.heads = copy(b23, kOffHeads, kOffIndex - kOffHeads);
  p.index = copy(b24, kOffIndex, kRbSize - kOffIndex);
  mb.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
  mb.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
  dd->vk.CmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0, 1, &mb, 0, nullptr, 0,
                            nullptr);
  p.frame = frame_index();
  p.recorded = true;
  g_pending = p;
}

void assetdb_count_draw(uint64_t vs, uint64_t fs) {
  std::lock_guard<std::mutex> lock(g_draw_mu);
  ++g_draw_vs[vs];
  ++g_draw_fs[fs];
}

namespace {

void kelvin_rgb(float t, float out[3]) {  // tools/assets/colour.py kelvin_to_linear_rgb
  t = std::min(std::max(t, 1667.0f), 25000.0f);
  double x, y, T = t;
  if (T <= 4000) x = -0.2661239e9 / (T * T * T) - 0.2343589e6 / (T * T) + 0.8776956e3 / T + 0.179910;
  else x = -3.0258469e9 / (T * T * T) + 2.1070379e6 / (T * T) + 0.2226347e3 / T + 0.240390;
  if (T <= 2222) y = -1.1063814 * x * x * x - 1.34811020 * x * x + 2.18555832 * x - 0.20219683;
  else if (T <= 4000) y = -0.9549476 * x * x * x - 1.37418593 * x * x + 2.09137015 * x - 0.16748867;
  else y = 3.0817580 * x * x * x - 5.87338670 * x * x + 3.75112997 * x - 0.37001483;
  double X = x / y, Z = (1 - x - y) / y;
  double r = 3.2404542 * X - 1.5371385 - 0.4985314 * Z, g = -0.9692660 * X + 1.8760108 + 0.0415560 * Z,
         b = 0.0556434 * X - 0.2040259 + 1.0572252 * Z;
  r = std::max(r, 0.0), g = std::max(g, 0.0), b = std::max(b, 0.0);
  double m = std::max(r, std::max(g, b));
  if (m <= 0) m = 1;
  out[0] = float(r / m), out[1] = float(g / m), out[2] = float(b / m);
}

float srgb_lin(float c) { return c <= 0.04045f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f); }

bool chroma(const float c[3], float o[3]) {
  float m = std::max(c[0], std::max(c[1], c[2]));
  if (!(m > 1e-12f) || !std::isfinite(m)) return false;
  for (int k = 0; k < 3; ++k) o[k] = c[k] / m;
  return true;
}

// Which colour model reproduces the runtime colour's chromaticity (bit per hypothesis).
enum { H_COLOR = 1, H_COLOR_LIN = 2, H_KELVIN = 4, H_COLOR_X_KELVIN = 8, H_LIN_X_KELVIN = 16 };
uint32_t colour_match(const float rt[3], const Bg3DbLight& L, float tol) {
  float a[3];
  if (!chroma(rt, a)) return 0;
  float k[3], h[5][3];
  kelvin_rgb(L.kelvin > 0 ? L.kelvin : 6500.0f, k);
  for (int c = 0; c < 3; ++c) {
    h[0][c] = L.color[c];
    h[1][c] = srgb_lin(L.color[c]);
    h[2][c] = k[c];
    h[3][c] = L.color[c] * k[c];
    h[4][c] = srgb_lin(L.color[c]) * k[c];
  }
  uint32_t bits = 0;
  for (int i = 0; i < 5; ++i) {
    float b[3];
    if (!chroma(h[i], b)) continue;
    float d = std::max(std::fabs(a[0] - b[0]), std::max(std::fabs(a[1] - b[1]), std::fabs(a[2] - b[2])));
    if (d <= tol) bits |= 1u << i;
  }
  return bits;
}

void build_vote_grid(const Db& d) {
  if (g_vote_ready) return;
  g_vote_grid.cell = 0.5f;
  for (uint32_t i = 0; i < d.lights.n; ++i) {
    const Bg3DbLight& L = d.lights[i];
    g_vote_grid.cells[Grid::key(g_vote_grid.c(L.pos[0]), g_vote_grid.c(L.pos[1]), g_vote_grid.c(L.pos[2]))].push_back(i);
  }
  g_vote_ready = true;
}

// Nearest database light within tol of p, among those `pred` accepts (UINT32_MAX if none).
template <class P>
uint32_t nearest(const Db& d, const Grid& g, const float p[3], float tol, float* out_d, P pred) {
  uint32_t best = kNone;
  float bd = tol * tol;
  int x0 = g.c(p[0] - tol), x1 = g.c(p[0] + tol), y0 = g.c(p[1] - tol), y1 = g.c(p[1] + tol), z0 = g.c(p[2] - tol),
      z1 = g.c(p[2] + tol);
  for (int x = x0; x <= x1; ++x)
    for (int y = y0; y <= y1; ++y)
      for (int z = z0; z <= z1; ++z) {
        auto it = g.cells.find(Grid::key(x, y, z));
        if (it == g.cells.end()) continue;
        for (uint32_t i : it->second) {
          float q = dist2(p, d.lights[i].pos);
          float slack = d.lights[i].move_amount;
          if (q <= bd + slack * slack && pred(i)) {
            bd = q;
            best = i;
          }
        }
      }
  if (out_d) *out_d = best == kNone ? -1.0f : std::sqrt(bd);
  return best;
}

uint32_t n_ref_total(const std::vector<uint8_t>& ref) {
  uint32_t n = 0;
  for (uint8_t r : ref) n += r;
  return n;
}

bool inv3(const float m[3][4], float inv[3][3]) {
  double a = m[0][0], b = m[0][1], c = m[0][2], d = m[1][0], e = m[1][1], f = m[1][2], g = m[2][0], h = m[2][1],
         i = m[2][2];
  double det = a * (e * i - f * h) - b * (d * i - f * g) + c * (d * h - e * g);
  if (!(std::fabs(det) > 1e-12) || !std::isfinite(det)) return false;
  inv[0][0] = float((e * i - f * h) / det), inv[0][1] = float((c * h - b * i) / det), inv[0][2] = float((b * f - c * e) / det);
  inv[1][0] = float((f * g - d * i) / det), inv[1][1] = float((a * i - c * g) / det), inv[1][2] = float((c * d - a * f) / det);
  inv[2][0] = float((d * h - e * g) / det), inv[2][1] = float((b * g - a * h) / det), inv[2][2] = float((a * e - b * d) / det);
  return true;
}

void join_check(Db& d, const Pending& p) {
  const uint8_t* rb = g_rb_map;
  uint32_t sentinel;
  memcpy(&sentinel, rb + kOffCam + 208, 4);
  if (sentinel == 0xFFFFFFFFu) {
    log("[assetdb] join: the recorded copy did not run (command buffer not submitted?)");
    return;
  }
  float cam[3], scale;
  memcpy(cam, rb + kOffCam + 208, 12);
  memcpy(&scale, rb + kOffScale, 4);
  const uint32_t slots = uint32_t(p.lights / 80), pslots = uint32_t(p.proj / 128);
  // Lights the froxel lists reference this frame (the in-frustum set), from b23/b24.
  std::vector<uint8_t> ref(slots, 0), pref(pslots, 0);
  const uint32_t heads = uint32_t(p.heads / 16), nidx = uint32_t(p.index / 4);
  const int32_t* idx = reinterpret_cast<const int32_t*>(rb + kOffIndex);
  for (uint32_t f = 0; f < heads; ++f) {
    uint32_t h[4];
    memcpy(h, rb + kOffHeads + size_t(f) * 16, 16);
    uint32_t first = h[0], np = h[1] & 4095u, ns = (h[1] >> 12) & 4095u, npj = h[2] & 1023u;
    for (uint32_t k = 0; k < np + ns + npj && first + k < nidx; ++k) {
      int32_t v = idx[first + k];
      if (k < np + ns) {
        if (v >= 0 && uint32_t(v) < slots) ref[v] = 1;
      } else if (v >= 0 && uint32_t(v) < pslots) {
        pref[v] = 1;
      }
    }
  }
  // Level detection by votes
  build_vote_grid(d);
  const float tol = cfg_float("assetdb_join_tol", 0.05f);
  struct RtLight {
    uint32_t slot;
    float w[3], col[3], inv_r, m3, m4, m5;
    uint32_t chan, flags;
    bool referenced;
  };
  std::vector<RtLight> rt;
  for (uint32_t i = 0; i < slots; ++i) {
    float f[12];
    uint32_t u[2];
    memcpy(f, rb + kOffLights + size_t(i) * 80, 48);
    memcpy(u, rb + kOffLights + size_t(i) * 80 + 60, 8);
    float m3, m4, m5;
    memcpy(&m3, rb + kOffLights + size_t(i) * 80 + 48, 4);
    memcpy(&m4, rb + kOffLights + size_t(i) * 80 + 52, 4);
    memcpy(&m5, rb + kOffLights + size_t(i) * 80 + 56, 4);
    bool ok = true;
    for (int k = 0; k < 8; ++k) ok = ok && std::isfinite(f[k]);
    if (!ok || !(f[3] > 0.0f) || f[3] > 1e4f || std::fabs(f[0]) > 1e5f || (f[4] == 0 && f[5] == 0 && f[6] == 0))
      continue;
    RtLight L{i, {f[0] + cam[0], f[1] + cam[1], f[2] + cam[2]}, {f[4], f[5], f[6]}, f[3], m3, m4, m5, u[0], u[1],
              ref[i] != 0};
    rt.push_back(L);
  }
  // Only the lights the froxel lists use this frame vote: slots outside them hold lights
  // from other places and frames (Grymforge: 1182 valid-looking slots, 39 listed).
  // Projector (box) lights vote too, by the near-face centre of their unit box (the anchor
  // that matches the editor position, see the b29 statistics below): interiors such as the
  // Water Queen's House list only a few point lights.
  std::vector<std::array<float, 3>> voters;
  const bool any_listed = n_ref_total(ref) > 0;
  for (const RtLight& L : rt)
    if (!any_listed || L.referenced) voters.push_back({L.w[0], L.w[1], L.w[2]});
  const bool any_plisted = n_ref_total(pref) > 0;
  for (uint32_t i = 0; i < pslots; ++i) {
    if (any_plisted && !pref[i]) continue;
    float m[3][4], inv[3][3];
    memcpy(m, rb + kOffProj + size_t(i) * 128 + 48, 48);
    bool ok = true;
    for (auto& r : m)
      for (float v : r) ok = ok && std::isfinite(v);
    if (!ok || !inv3(m, inv)) continue;
    float qv[3] = {0.5f - m[0][3], 0.5f - m[1][3], 0.0f - m[2][3]};
    std::array<float, 3> w;
    for (int r = 0; r < 3; ++r) w[r] = inv[r][0] * qv[0] + inv[r][1] * qv[1] + inv[r][2] * qv[2] + cam[r];
    voters.push_back(w);
  }
  std::unordered_map<int, uint32_t> votes;
  for (const auto& V : voters) {
    struct { float w[3]; } L{{V[0], V[1], V[2]}};
    std::vector<int> seen;
    int x0 = g_vote_grid.c(L.w[0] - tol), x1 = g_vote_grid.c(L.w[0] + tol);
    int y0 = g_vote_grid.c(L.w[1] - tol), y1 = g_vote_grid.c(L.w[1] + tol);
    int z0 = g_vote_grid.c(L.w[2] - tol), z1 = g_vote_grid.c(L.w[2] + tol);
    for (int x = x0; x <= x1; ++x)
      for (int y = y0; y <= y1; ++y)
        for (int z = z0; z <= z1; ++z) {
          auto it = g_vote_grid.cells.find(Grid::key(x, y, z));
          if (it == g_vote_grid.cells.end()) continue;
          for (uint32_t i : it->second) {
            float slack = d.lights[i].move_amount;
            if (dist2(L.w, d.lights[i].pos) > tol * tol + slack * slack) continue;
            int lv = d.light_level[i];
            if (lv >= 0 && std::find(seen.begin(), seen.end(), lv) == seen.end()) seen.push_back(lv);
          }
        }
    for (int lv : seen) ++votes[lv];
  }
  int best = -1;
  uint32_t bv = 0;
  for (auto& [lv, n] : votes)
    if (n > bv) best = lv, bv = n;
  int cur;
  {
    std::shared_lock<std::shared_mutex> lock(g_level_mu);
    cur = g_level;
  }
  uint32_t cur_votes = cur >= 0 && votes.count(cur) ? votes[cur] : 0;
  // Switch on 3 votes; with no level yet, a level that is the only one voted for is enough
  // (positions match to float precision, so one exact match is strong evidence).
  if (best >= 0 && best != cur && bv > cur_votes && (bv >= 3 || (cur < 0 && votes.size() == 1)))
    set_level(best, "lights");
  {
    std::shared_lock<std::shared_mutex> lock(g_level_mu);
    cur = g_level;
  }
  if (cur < 0) {
    // Diagnose: nearest database light of any level for each runtime light (brute force, once per check).
    std::string csv = "slot,referenced,x,y,z,inv_r,r,g,b,nearest_dist,nearest_level,nearest_name\n";
    std::vector<float> nd;
    std::unordered_map<int, uint32_t> near_levels;
    for (const RtLight& L : rt) {
      float bd = 1e30f;
      uint32_t bi = kNone;
      for (uint32_t i = 0; i < d.lights.n; ++i)
        if (float q = dist2(L.w, d.lights[i].pos); q < bd) bd = q, bi = i;
      bd = std::sqrt(bd);
      nd.push_back(bd);
      int lvn = bi == kNone ? -1 : d.light_level[bi];
      if (bd < 1.0f) ++near_levels[lvn];
      char line[384];
      snprintf(line, sizeof line, "%u,%d,%.4f,%.4f,%.4f,%.6g,%.6g,%.6g,%.6g,%.4f,%s,%s\n", L.slot, int(L.referenced),
               L.w[0], L.w[1], L.w[2], L.inv_r, L.col[0], L.col[1], L.col[2], bd,
               lvn >= 0 ? d.str(d.levels[lvn].name) : "", bi == kNone ? "" : d.str(d.lights[bi].name));
      csv += line;
    }
    std::sort(nd.begin(), nd.end());
    std::string lv_s;
    for (auto& [lvn, n] : near_levels) lv_s += std::string(" ") + (lvn >= 0 ? d.str(d.levels[lvn].name) : "?") + "=" + std::to_string(n);
    log("[assetdb] join: %zu runtime lights (%u froxel-listed), no database level matches (best %d votes); camera "
        "(%.1f, %.1f, %.1f); nearest database light: median %.2f m, min %.3f m; within 1 m by level:%s",
        rt.size(), n_ref_total(ref), int(bv), cam[0], cam[1], cam[2], nd.empty() ? -1.0 : nd[nd.size() / 2],
        nd.empty() ? -1.0 : nd[0], lv_s.c_str());
    if (cfg_int("assetdb_join_csv", 1)) {
      std::string dir = root_dir() + "\\captures";
      make_dirs(dir);
      std::string path = dir + "\\assetdb_join_" + now_stamp() + "_nomatch.csv";
      write_file(path, csv.data(), csv.size());
    }
    return;
  }
  // Match within the current level
  uint32_t n_ref = 0, m_all = 0, m_ref = 0, col_any = 0, rad_ok = 0, type_ok = 0;
  uint32_t hyp[5] = {};
  std::vector<uint8_t> db_used(d.lights.n, 0);
  std::string csv = "slot,referenced,x,y,z,inv_r,r,g,b,m3,m4,m5,chan,flags,db_index,dist,db_name,db_type,db_range,db_intensity,db_kelvin,db_flags,db_r,db_g,db_b,colour_bits\n";
  std::vector<float> dists;
  for (const RtLight& L : rt) {
    n_ref += L.referenced;
    float dd = -1;
    uint32_t j = nearest(d, g_grid, L.w, tol, &dd, [&](uint32_t) { return true; });
    char line[512];
    uint32_t bits = 0;
    if (j != kNone) {
      const Bg3DbLight& D = d.lights[j];
      ++m_all;
      m_ref += L.referenced;
      dists.push_back(dd);
      db_used[j] = 1;
      bits = colour_match(L.col, D, cfg_float("assetdb_join_color_tol", 0.06f));
      if (bits) ++col_any;
      for (int k = 0; k < 5; ++k) hyp[k] += (bits >> k) & 1;
      if (D.type != BG3_DB_LIGHT_BOX) {
        ++type_ok;
        float r = 1.0f / L.inv_r;
        if (std::fabs(r - D.range) <= 0.02f * std::max(D.range, 0.1f)) ++rad_ok;
      }
      snprintf(line, sizeof line, "%u,%d,%.4f,%.4f,%.4f,%.6g,%.6g,%.6g,%.6g,%.6g,%.6g,%.6g,%u,%u,%u,%.4f,%s,%u,%.4g,%.6g,%.6g,%u,%.4f,%.4f,%.4f,%u\n",
               L.slot, int(L.referenced), L.w[0], L.w[1], L.w[2], L.inv_r, L.col[0], L.col[1], L.col[2], L.m3, L.m4,
               L.m5, L.chan, L.flags, j, dd, d.str(D.name), unsigned(D.type), D.range, D.intensity, D.kelvin, D.flags,
               D.color[0], D.color[1], D.color[2], bits);
    } else {
      snprintf(line, sizeof line, "%u,%d,%.4f,%.4f,%.4f,%.6g,%.6g,%.6g,%.6g,%.6g,%.6g,%.6g,%u,%u,,,,,,,,,,,,\n", L.slot,
               int(L.referenced), L.w[0], L.w[1], L.w[2], L.inv_r, L.col[0], L.col[1], L.col[2], L.m3, L.m4, L.m5,
               L.chan, L.flags);
    }
    csv += line;
  }
  std::sort(dists.begin(), dists.end());
  // Projector (box) lights, b29: box space = M * (p - cam); try three anchor points of
  // the unit box for the authored position: the near-face centre, the centre, the far face.
  const float anchors[3][3] = {{0.5f, 0.5f, 0.0f}, {0.5f, 0.5f, 0.5f}, {0.5f, 0.5f, 1.0f}};
  uint32_t n_proj = 0, n_proj_ref = 0, m_proj[3] = {};
  for (uint32_t i = 0; i < pslots; ++i) {
    float m[3][4];
    memcpy(m, rb + kOffProj + size_t(i) * 128 + 48, 48);
    float inv[3][3];
    bool ok = true;
    for (auto& r : m)
      for (float v : r) ok = ok && std::isfinite(v);
    if (!ok || !inv3(m, inv)) continue;
    ++n_proj;
    n_proj_ref += pref[i];
    for (int a = 0; a < 3; ++a) {
      float qv[3] = {anchors[a][0] - m[0][3], anchors[a][1] - m[1][3], anchors[a][2] - m[2][3]}, w[3];
      for (int r = 0; r < 3; ++r) w[r] = inv[r][0] * qv[0] + inv[r][1] * qv[1] + inv[r][2] * qv[2] + cam[r];
      if (nearest(d, g_grid, w, 2.0f * tol, nullptr,
                  [&](uint32_t j) { return d.lights[j].type == BG3_DB_LIGHT_BOX; }) != kNone)
        ++m_proj[a];
    }
  }
  // Database lights near the camera that the runtime array does not hold
  const RLevel& lv = d.levels[cur];
  uint32_t near_db = 0, near_db_used = 0;
  const float near_r = cfg_float("assetdb_join_near", 25.0f);
  for (uint32_t i = lv.first_light; i < lv.first_light + lv.n_light; ++i)
    if (d.lights[i].type != BG3_DB_LIGHT_BOX && dist2(d.lights[i].pos, cam) < near_r * near_r) {
      ++near_db;
      near_db_used += db_used[i];
    }
  // Shaders: draws of the counted frame, and every module the game created so far
  uint64_t dv = 0, df = 0, dv_mat = 0, df_mat = 0, dv_eng = 0, df_eng = 0;
  {
    std::lock_guard<std::mutex> lock(g_draw_mu);
    for (auto& [h, n] : g_draw_vs) {
      dv += n;
      if (find_shader(d, h)) dv_mat += n;
      else if (find_engine(d, h)) dv_eng += n;
    }
    for (auto& [h, n] : g_draw_fs) {
      df += n;
      if (h == 0) continue;  // depth-only pipelines have no fragment shader
      if (find_shader(d, h)) df_mat += n;
      else if (find_engine(d, h)) df_eng += n;
    }
  }
  uint64_t mods = 0, mods_mat = 0, mods_eng = 0;
  {
    std::shared_lock<std::shared_mutex> lock(tracker().mu);
    std::vector<uint64_t> hs;
    for (auto& [m, h] : tracker().modules) hs.push_back(h);
    std::sort(hs.begin(), hs.end());
    hs.erase(std::unique(hs.begin(), hs.end()), hs.end());
    for (uint64_t h : hs) {
      ++mods;
      if (find_shader(d, h)) ++mods_mat;
      else if (find_engine(d, h)) ++mods_eng;
    }
  }
  auto pct = [](uint64_t a, uint64_t b) { return b ? 100.0 * double(a) / double(b) : 0.0; };
  const char* src;
  const char* lname;
  api_current_level(&lname, &src);
  ++g_checks;
  log("[assetdb] join #%u frame %llu level %s (%s, %u/%zu votes): camera (%.1f, %.1f, %.1f), global scale %.6g",
      g_checks, (unsigned long long)p.frame, lname, src, cur_votes ? cur_votes : bv, voters.size(), cam[0], cam[1],
      cam[2], scale);
  log("[assetdb] join lights b25: %zu valid of %u slots, %u in the froxel lists; matched %u (%.1f%%) within %.0f cm, "
      "froxel-listed matched %u/%u (%.1f%%); median distance %.2f mm, max %.2f mm; colour ok %u (%.1f%% of matched: "
      "Color %u, sRGB->lin %u, Kelvin %u, Color*K %u, lin*K %u); radius within 2%% %u/%u",
      rt.size(), slots, n_ref, m_all, pct(m_all, rt.size()), tol * 100, m_ref, n_ref, pct(m_ref, n_ref),
      dists.empty() ? -1.0 : 1000.0 * dists[dists.size() / 2], dists.empty() ? -1.0 : 1000.0 * dists.back(), col_any,
      pct(col_any, m_all), hyp[0], hyp[1], hyp[2], hyp[3], hyp[4], rad_ok, type_ok);
  log("[assetdb] join lights near camera: %u database point/spot lights within %.0f m, %u of them in the runtime array",
      near_db, near_r, near_db_used);
  log("[assetdb] join projectors b29: %u valid of %u slots, %u in the froxel lists; matched as box lights "
      "(anchor near face / centre / far face): %u / %u / %u",
      n_proj, pslots, n_proj_ref, m_proj[0], m_proj[1], m_proj[2]);
  log("[assetdb] join shaders: frame draws %llu: VS material %.1f%% + engine %.1f%%, FS material %.1f%% + engine "
      "%.1f%%; modules created %llu: material %llu (%.1f%%), engine %llu (%.1f%%), unknown %llu",
      (unsigned long long)dv, pct(dv_mat, dv), pct(dv_eng, dv), pct(df_mat, df), pct(df_eng, df),
      (unsigned long long)mods, (unsigned long long)mods_mat, pct(mods_mat, mods), (unsigned long long)mods_eng,
      pct(mods_eng, mods), (unsigned long long)(mods - mods_mat - mods_eng));
  if (cfg_int("assetdb_join_csv", 1)) {
    std::string dir = root_dir() + "\\captures";
    make_dirs(dir);
    std::string path = dir + "\\assetdb_join_" + now_stamp() + "_" + std::to_string(g_checks) + ".csv";
    write_file(path, csv.data(), csv.size());
  }
}

}  // namespace

void assetdb_on_present(uint64_t frame) {
  // Draw counting covers exactly one frame: the one the capture was recorded in.
  int join = cfg_int("assetdb_join", 0);
  if (!join) return;
  Db* d = db();
  if (!d) return;
  Pending p;
  {
    std::lock_guard<std::mutex> lock(g_join_mu);
    p = g_pending;
  }
  if (g_assetdb_counting && p.recorded && p.frame + 1 <= frame) g_assetdb_counting = false;
  if (p.recorded && frame >= p.frame + 4) {
    {
      std::lock_guard<std::mutex> lock(g_join_mu);
      g_pending = Pending{};
    }
    join_check(*d, p);
    std::lock_guard<std::mutex> lock(g_draw_mu);
    g_draw_vs.clear();
    g_draw_fs.clear();
  }
  const uint64_t interval = uint64_t(std::max(30, cfg_int("assetdb_join_interval", 240)));
  if (!p.recorded && !g_assetdb_armed && frame >= g_next_check) {
    g_next_check = frame + interval;
    {
      std::lock_guard<std::mutex> lock(g_draw_mu);
      g_draw_vs.clear();
      g_draw_fs.clear();
    }
    g_assetdb_counting = true;
    g_assetdb_armed = true;
  }
}

void assetdb_device_destroyed() {
  std::lock_guard<std::mutex> lock(g_join_mu);
  g_assetdb_armed = false;
  g_assetdb_counting = false;
  if (g_rb) {
    defer_destroy(BG3_DEFER_BUFFER, uint64_t(g_rb));
    defer_destroy(BG3_DEFER_MEMORY, uint64_t(g_rb_mem));
    g_rb = VK_NULL_HANDLE;
    g_rb_mem = VK_NULL_HANDLE;
    g_rb_map = nullptr;
  }
}

}  // namespace bg3
