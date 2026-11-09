// bg3rtx_hairconv --all: every hair and beard style of the game, read from the player's own install
// (docs/RTX_HAIR_ROADMAP.md 2f). Nothing of the game or of Granny is redistributed: the packages are read in place
// (read-only) and the GR2 meshes are opened through a granny2.dll the player supplies (--granny). Without it this
// mode is unavailable (exit 3, preparation.json state "unavailable") and the capture model stays.
//
//   bg3rtx_hairconv --all --game <BG3 folder or its Data> --granny <granny2.dll> --out <groom dir>
//       [--report-dir <dir>] [--threads N] [--jobs J] [--check full|cheap] [--accept-review]
//       [--match <text>]... [--names <file>] [--limit N] [--json <file>] [--export-hcap <dir>]
//   bg3rtx_hairconv --dump-gr2 --game <dir> --granny <dll> <pak path> <out.bin>     (dev: gr2_dump.ps1 layout)
//
// Order: tiers as tools/hair_groom/prepare.py (companion, player, named, hat, other, unused), LOD 0 before LOD 1, name.
// Incremental by the pak entries of the GR2 and the mask (package, offset, part, size), the mesh name, the converter
// version and the check mode: <report-dir>/all_state.json, saved after every style (resumable). The catalogue is
// cached in <report-dir>/all_catalogue.tsv, keyed by the packages' names, sizes and write times.
#include "catalogue.h"
#include "granny.h"
#include "pak.h"
#include "pipeline.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <map>
#include <mutex>
#include <set>
#include <thread>

namespace fs = std::filesystem;

namespace hairconv {

namespace {
using Clock = std::chrono::steady_clock;
double secs(Clock::time_point a) { return std::chrono::duration<double>(Clock::now() - a).count(); }
void say(const std::string& s) { printf("%s\n", s.c_str()); fflush(stdout); }

std::wstring widen(const std::string& s) {
  if (s.empty()) return {};
  const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), int(s.size()), nullptr, 0);
  std::wstring w(size_t(n), L'\0');
  MultiByteToWideChar(CP_UTF8, 0, s.data(), int(s.size()), w.data(), n);
  return w;
}
std::string lower(std::string s) {
  for (char& c : s) c = char(tolower(static_cast<unsigned char>(c)));
  return s;
}
bool keep_name(const std::string& n) {
  const size_t dot = n.find_last_of('.');
  if (dot == std::string::npos) return false;
  const std::string e = lower(n.substr(dot));
  return e == ".lsf" || e == ".lsx" || e == ".gr2" || e == ".dds";
}

// The Data folder: the given folder, or its Data subfolder (the install folder).
std::wstring data_dir(const std::string& game) {
  const fs::path p = widen(game);
  std::error_code ec;
  if (fs::exists(p / L"Data" / L"Shared.pak", ec) || fs::exists(p / L"Data" / L"Gustav.pak", ec)) return (p / L"Data").wstring();
  return p.wstring();
}

// batch.head_proxy from the bind pose: Head_M (+ HeadEnd_M_endBone) of the mesh's bone bindings.
Head head_proxy(const granny::Mesh& m, const granny::File& f) {
  std::map<std::string, const float*> bones;
  for (const granny::Bone& b : f.bones) bones[b.name] = b.inverse_world;
  auto origin = [&](const std::string& name, float o[3]) {
    o[0] = o[1] = o[2] = 0;
    const auto it = bones.find(name);
    if (it == bones.end()) return;
    // LSLib's InverseWorldTransform is row-vector form: the world origin is the inverse's last row.
    double a[4][8];
    for (int r = 0; r < 4; ++r)
      for (int c = 0; c < 8; ++c) a[r][c] = c < 4 ? double(it->second[r * 4 + c]) : (c - 4 == r ? 1.0 : 0.0);
    for (int c = 0; c < 4; ++c) {
      int piv = c;
      for (int r = c + 1; r < 4; ++r) if (std::fabs(a[r][c]) > std::fabs(a[piv][c])) piv = r;
      if (a[piv][c] == 0) return;
      if (piv != c) for (int k = 0; k < 8; ++k) std::swap(a[c][k], a[piv][k]);
      const double d = a[c][c];
      for (int k = 0; k < 8; ++k) a[c][k] /= d;
      for (int r = 0; r < 4; ++r)
        if (r != c && a[r][c] != 0) { const double s = a[r][c]; for (int k = 0; k < 8; ++k) a[r][k] -= s * a[c][k]; }
    }
    for (int k = 0; k < 3; ++k) o[k] = float(a[3][4 + k]);
  };
  Head h;
  const auto head = std::find(m.bindings.begin(), m.bindings.end(), "Head_M");
  if (head == m.bindings.end()) return h;   // no head joints: no proxy (batch.py)
  float c0[3], c1[3];
  origin(*head, c0);
  if (std::find(m.bindings.begin(), m.bindings.end(), "HeadEnd_M_endBone") != m.bindings.end()) {
    origin("HeadEnd_M_endBone", c1);
    for (int k = 0; k < 3; ++k) h.centre[k] = double((c0[k] + c1[k]) / 2.0f);   // numpy float32 arithmetic
  } else {
    for (int k = 0; k < 3; ++k) h.centre[k] = double(c0[k]) + (k == 1 ? 0.08 : 0.0);
  }
  h.radii[0] = 0.066; h.radii[1] = 0.089; h.radii[2] = 0.083;
  return h;
}

const granny::Mesh* find_mesh(const granny::File& f, const std::string& name) {
  for (const granny::Mesh& m : f.meshes)
    if (m.name == name) return &m;
  return nullptr;
}
Mesh to_mesh(const granny::Mesh& g) {
  Mesh m;
  m.pos = g.pos;
  m.uv = g.uv;
  m.tris = g.indices;
  return m;
}

std::string entry_text(const pak::Set& ps, const std::string& name) {
  const pak::Entry* e = ps.find(name);
  if (!e) return "missing:" + name;
  return ps.packages()[e->package].file + "," + std::to_string(e->offset) + "," + std::to_string(e->part) + "," + std::to_string(e->size);
}

bool write_hcap_v1(const fs::path& p, const Input& in) {
  std::vector<uint8_t> o;
  auto u32 = [&](uint32_t v) { const uint8_t* b = reinterpret_cast<const uint8_t*>(&v); o.insert(o.end(), b, b + 4); };
  auto f32 = [&](float v) { const uint8_t* b = reinterpret_cast<const uint8_t*>(&v); o.insert(o.end(), b, b + 4); };
  o.insert(o.end(), {'H', 'C', 'A', 'P'});
  u32(1); u32(uint32_t(in.mesh.tris.size())); u32(in.mesh.vertices()); u32(uint32_t(in.mask.w)); u32(uint32_t(in.mask.h));
  u32((in.has_head ? 1u : 0u) | (in.has_grid ? 2u : 0u)); u32(uint32_t(in.grid[0])); u32(uint32_t(in.grid[1]));
  u32(0); u32(0);
  for (int k = 0; k < 3; ++k) f32(float(in.head.centre[k]));
  for (int k = 0; k < 3; ++k) f32(float(in.head.radii[k]));
  auto blob = [&](const void* d, size_t n) { const uint8_t* b = static_cast<const uint8_t*>(d); o.insert(o.end(), b, b + n); };
  blob(in.mesh.tris.data(), in.mesh.tris.size() * 4);
  blob(in.mesh.pos.data(), in.mesh.pos.size() * 4);
  blob(in.mesh.uv.data(), in.mesh.uv.size() * 4);
  blob(in.mask.rgba.data(), in.mask.rgba.size());
  return write_text(p, std::string(reinterpret_cast<const char*>(o.data()), o.size()));
}

const char* tier_text(int tier) {
  static const char* const t[7] = {"styles seen in game", "companions", "character creation looks", "named characters",
                                   "hair under hats", "other characters", "unused styles"};
  return tier >= 0 && tier < 7 ? t[tier] : "styles";
}

int usage() {
  printf("bg3rtx_hairconv --all --game <BG3 folder or Data> --granny <granny2.dll> --out <groom dir> [--report-dir <dir>]\n"
         "    [--threads N] [--jobs J] [--check full|cheap] [--accept-review] [--match <text>]... [--names <file>] [--limit N]\n"
         "    [--json <file>] [--export-hcap <dir>]\n"
         "  Converts every hair and beard style of the game's catalogue from the installed paks (read-only). granny2.dll is\n"
         "  not part of BG3RTX or of the game: supply your own copy (for example from your LSLib ExportTool download).\n"
         "bg3rtx_hairconv --dump-gr2 --game <dir> --granny <granny2.dll> <pak path> <out.bin>   (dev)\n");
  return 0;
}
}  // namespace

int run_all(int argc, char** argv) {
  std::string game, granny_path, out_dir, report_dir, json_path, names_file, export_dir;
  std::vector<std::string> match, positional;
  int threads = 4, jobs = 1, limit = 0;
  bool full = true, accept_review = false, dump = false;
  for (int i = 0; i < argc; ++i) {
    const std::string a = argv[i];
    auto next = [&]() { return i + 1 < argc ? std::string(argv[++i]) : std::string(); };
    if (a == "--all") {}
    else if (a == "--dump-gr2") dump = true;
    else if (a == "--game") game = next();
    else if (a == "--granny") granny_path = next();
    else if (a == "--out" || a == "--out-dir") out_dir = next();
    else if (a == "--report-dir") report_dir = next();
    else if (a == "--threads") threads = std::max(1, atoi(next().c_str()));
    else if (a == "--jobs") jobs = std::max(1, atoi(next().c_str()));
    else if (a == "--check") full = next() != "cheap";
    else if (a == "--accept-review") accept_review = true;
    else if (a == "--match") match.push_back(lower(next()));
    else if (a == "--names") names_file = next();
    else if (a == "--limit") limit = std::max(0, atoi(next().c_str()));
    else if (a == "--json") json_path = next();
    else if (a == "--export-hcap") export_dir = next();
    else if (a == "--help" || a == "-h") return usage();
    else positional.push_back(a);
  }
  SetPriorityClass(GetCurrentProcess(), BELOW_NORMAL_PRIORITY_CLASS);
  const fs::path rdir = widen(report_dir);
  auto status_unavailable = [&](const std::string& why) {
    fprintf(stderr, "bg3rtx_hairconv --all: %s\n", why.c_str());
    if (!report_dir.empty()) {
      std::error_code ec;
      fs::create_directories(rdir, ec);
      write_text(rdir / "preparation.json",
                 "{\"kind\": \"hair_grooms\", \"source\": \"game\", \"state\": \"unavailable\", \"error\": \"" + json_escape(why) +
                     "\", \"line\": \"Convert all styles is not available.\", \"detail\": \"" + json_escape(why) +
                     "\", \"pid\": " + std::to_string(GetCurrentProcessId()) + "}");
    }
  };
  if (!dump && out_dir.empty()) { fprintf(stderr, "bg3rtx_hairconv --all: --out is required (--all --help)\n"); return 2; }
  if (game.empty()) { status_unavailable("--game <BG3 folder> is required"); return 2; }
  // Granny first: without the player's granny2.dll this mode is unavailable (the capture model stays).
  granny::Library gr;
  std::string error;
  if (granny_path.empty()) {
    status_unavailable("Convert all needs granny2.dll, which BG3RTX cannot ship: give the path of your own copy with --granny "
                       "(for example from your LSLib ExportTool download).");
    return 3;
  }
  if (!gr.load(widen(granny_path), &error)) { status_unavailable(error); return 3; }
  const auto start = Clock::now();
  pak::Set ps;
  ps.set_filter(keep_name);
  if (!ps.open(data_dir(game), &error)) { status_unavailable("game packages: " + error); return 4; }
  say("[hairconv all] granny " + gr.version() + ", " + std::to_string(ps.packages().size()) + " packages, " +
      std::to_string(ps.names().size()) + " entries indexed in " + std::to_string(int(secs(start) * 1000)) + " ms");

  if (dump) {
    if (positional.size() != 2) { fprintf(stderr, "--dump-gr2 <pak path> <out.bin>\n"); return 2; }
    std::vector<uint8_t> raw;
    granny::File f;
    if (!ps.read(positional[0], raw, &error) || !gr.read(raw.data(), raw.size(), f, &error)) { fprintf(stderr, "%s\n", error.c_str()); return 1; }
    const std::vector<uint8_t> bytes = granny::dump_bytes(f);
    return write_text(widen(positional[1]), std::string(reinterpret_cast<const char*>(bytes.data()), bytes.size())) ? 0 : 1;
  }

  // ---- catalogue (cached by the packages' fingerprint)
  std::vector<catalogue::Style> all;
  const std::string fingerprint = ps.fingerprint();
  const fs::path cat_path = rdir / "all_catalogue.tsv";
  if (!report_dir.empty()) fs::create_directories(rdir);
  if (report_dir.empty() || !catalogue::load(cat_path.string(), fingerprint, all)) {
    if (!catalogue::styles(ps, all, say, &error)) { status_unavailable("catalogue: " + error); return 4; }
    catalogue::classify(ps, all, say);
    if (!report_dir.empty()) catalogue::save(cat_path.string(), fingerprint, all);
  } else {
    say("[hairconv all] catalogue: " + std::to_string(all.size()) + " rows (cached)");
  }
  const size_t catalogue_rows = all.size();
  std::vector<catalogue::Style> rows;
  std::set<std::string> wanted;
  if (!names_file.empty()) {
    const std::string text = read_text(widen(names_file));
    size_t a = 0;
    while (a < text.size()) {
      size_t b = text.find('\n', a);
      std::string l = text.substr(a, b == std::string::npos ? std::string::npos : b - a);
      while (!l.empty() && (l.back() == '\r' || l.back() == ' ')) l.pop_back();
      if (!l.empty()) wanted.insert(l);
      a = b == std::string::npos ? text.size() : b + 1;
    }
  }
  for (const auto& s : all) {
    if (!match.empty() && std::none_of(match.begin(), match.end(), [&](const std::string& m) { return lower(s.gr2).find(m) != std::string::npos; }))
      continue;
    if (!wanted.empty() && !wanted.count(s.name)) continue;
    rows.push_back(s);
  }
  if (limit && rows.size() > size_t(limit)) rows.resize(size_t(limit));   // batch.py: --limit on the name order
  catalogue::order(rows);

  // ---- state: one "name \t key \t record" line per style
  std::map<std::string, std::pair<std::string, std::string>> state;
  const fs::path state_path = rdir / "all_state.json";
  if (!report_dir.empty()) {
    const std::string text = read_text(state_path);
    size_t at = 0;
    while (at < text.size()) {
      size_t nl = text.find('\n', at);
      const std::string line = text.substr(at, nl == std::string::npos ? std::string::npos : nl - at);
      at = nl == std::string::npos ? text.size() : nl + 1;
      const size_t a = line.find('\t'), b = a == std::string::npos ? a : line.find('\t', a + 1);
      if (b != std::string::npos) state[line.substr(0, a)] = {line.substr(a + 1, b - a - 1), line.substr(b + 1)};
    }
  }
  auto verdict_of = [](const std::string& rec) -> std::string {
    for (const char* v : {"pass", "needs-review", "reject"})
      if (rec.find(std::string("\"verdict\": \"") + v + "\"") != std::string::npos) return v;
    return {};
  };
  std::vector<std::string> keys(rows.size());
  std::vector<size_t> todo;
  for (size_t i = 0; i < rows.size(); ++i) {
    const auto& s = rows[i];
    keys[i] = std::string(kVersion) + (full ? "|full" : "|cheap") + (accept_review ? "|review" : "") + "|" + s.mesh + "|" +
              entry_text(ps, s.gr2) + "|" + (s.mask.empty() ? std::string("no mask") : entry_text(ps, s.mask));
    const auto it = state.find(s.name);
    if (it == state.end() || it->second.first != keys[i]) todo.push_back(i);
  }
  const Options opt{out_dir, threads, full, accept_review};
  std::mutex mu;
  std::atomic<size_t> next{0};
  size_t done = 0, rejects = 0;
  std::vector<std::string> records;
  std::vector<double> run_seconds;
  auto last = Clock::now();
  const auto run_start = Clock::now();
  auto save = [&](bool running) {   // under mu
    if (report_dir.empty()) return;
    std::string text;
    for (auto& [n, v] : state) text += n + "\t" + v.first + "\t" + v.second + "\n";
    write_text(state_path, text);
    uint64_t bytes = 0;
    std::error_code ec;
    for (auto& e : fs::directory_iterator(widen(out_dir), ec))
      if (e.path().extension() == ".hgrm") bytes += e.file_size(ec);
    int per_total[7] = {}, per_done[7] = {};
    size_t ready = 0, finished = 0, review = 0, rej = 0;
    for (size_t i = 0; i < rows.size(); ++i) {
      ++per_total[rows[i].tier];
      const auto it = state.find(rows[i].name);
      if (it == state.end() || it->second.first != keys[i]) continue;
      ++per_done[rows[i].tier];
      ++finished;
      const std::string v = verdict_of(it->second.second);
      ready += v == "pass";
      review += v == "needs-review";
      rej += v == "reject";
    }
    const size_t left = rows.size() - finished;
    double per = 0;
    if (!run_seconds.empty()) { std::vector<double> s = run_seconds; std::nth_element(s.begin(), s.begin() + s.size() / 2, s.end()); per = s[s.size() / 2]; }
    const double rate = done ? double(done) / std::max(1e-3, secs(run_start)) : 0;
    const double eta = rate > 0 ? left / rate : per * left / std::max(1, jobs);
    int next_tier = -1;
    std::string tiers;
    for (int t = 0; t < 7; ++t) {
      if (!per_total[t]) continue;
      if (next_tier < 0 && per_done[t] < per_total[t]) next_tier = t;
      tiers += std::string(tiers.empty() ? "" : ", ") + "{\"tier\": \"" + catalogue::kTierNames[t] + "\", \"total\": " +
               std::to_string(per_total[t]) + ", \"finished\": " + std::to_string(per_done[t]) + "}";
    }
    const char* st = running && left ? "running" : left ? "waiting" : "done";
    char size[32], line[200], detail[200], eta_text[64];
    if (bytes >= 100000000) snprintf(size, sizeof size, "%.1f GB", bytes / 1e9);
    else snprintf(size, sizeof size, "%.0f MB", bytes / 1e6);
    snprintf(line, sizeof line, "%zu of %zu styles ready, %s on disk.", ready, rows.size(), size);
    if (eta < 5400) snprintf(eta_text, sizeof eta_text, "about %d min left", std::max(1, int(std::lround(eta / 60))));
    else snprintf(eta_text, sizeof eta_text, "about %.1f h left", eta / 3600);
    if (!left) snprintf(detail, sizeof detail, "All styles are done.");
    else if (running) snprintf(detail, sizeof detail, "Converting %s, %s.", tier_text(next_tier), eta_text);
    else snprintf(detail, sizeof detail, "%zu styles left, %s.", left, eta_text);
    std::string j = "{\"kind\": \"hair_grooms\", \"source\": \"game\", \"ready\": " + std::to_string(ready) +
                    ", \"finished\": " + std::to_string(finished) + ", \"needed\": " + std::to_string(rows.size()) +
                    ", \"all\": " + std::to_string(catalogue_rows) + ", \"convert_all\": true, \"review\": " + std::to_string(review) +
                    ", \"rejects\": " + std::to_string(rej) + ", \"bytes\": " + std::to_string(bytes) + ", \"eta_s\": " +
                    std::to_string(int(std::lround(eta))) + ", \"state\": \"" + st + "\", \"next_tier\": " +
                    (next_tier < 0 ? std::string("null") : "\"" + std::string(catalogue::kTierNames[next_tier]) + "\"") +
                    ", \"tiers\": [" + tiers + "], \"line\": \"" + line + "\", \"detail\": \"" + detail + "\", \"run_done\": " +
                    std::to_string(done) + ", \"run_total\": " + std::to_string(todo.size()) + ", \"granny\": \"" +
                    json_escape(gr.version()) + "\", \"pid\": " + std::to_string(GetCurrentProcessId()) + ", \"updated\": " +
                    std::to_string((long long)time(nullptr)) + "}";
    write_text(rdir / "preparation.json", j);
  };
  say("[hairconv all] " + std::to_string(todo.size()) + " styles to convert (" + std::to_string(rows.size() - todo.size()) +
      " unchanged) of " + std::to_string(rows.size()) + " (" + std::to_string(catalogue_rows) + " in the catalogue), " +
      std::to_string(jobs) + " jobs x " + std::to_string(threads) + " threads, " + (full ? "full" : "cheap") + " check, out " + out_dir);
  {
    std::lock_guard<std::mutex> lock(mu);
    save(true);
  }
  if (!export_dir.empty()) fs::create_directories(widen(export_dir));
  auto convert_one = [&](const catalogue::Style& s, const std::string& extra) -> Rec {
    std::vector<uint8_t> raw;
    std::string e;
    if (!ps.read(s.gr2, raw, &e)) return reject(s.name, e, extra);
    granny::File f;
    if (!gr.read(raw.data(), raw.size(), f, &e)) return reject(s.name, "GR2: " + e, extra);
    raw.clear();
    const granny::Mesh* gm = find_mesh(f, s.mesh);
    if (!gm) return reject(s.name, "KeyError: no mesh " + s.mesh, extra);
    if (gm->indices.size() < 3 || gm->indices.size() % 3) return reject(s.name, "mesh has no triangle list", extra);
    if (s.mask.empty() || !ps.contains(s.mask))
      return reject(s.name, "mask texture not found (" + (s.mask.empty() ? std::string("no ID_Depth_Root_Alpha_MSKA") : s.mask) + ")", extra);
    Input in;
    in.mesh = to_mesh(*gm);
    for (uint32_t i : in.mesh.tris)
      if (i >= in.mesh.vertices()) return reject(s.name, "index out of range", extra);
    if (!ps.read(s.mask, raw, &e) || !decode_dds(raw.data(), raw.size(), in.mask, &e)) return reject(s.name, "mask: " + e, extra);
    in.has_head = true;   // batch.py: the skeleton's head proxy, or none (never fitted to the roots)
    in.head = head_proxy(*gm, f);
    // LOD1 spans several tiles: the grid is detected on its LOD0 mesh when the GR2 has it (batch.py read_cards).
    Mesh ref;
    const Mesh* grid_ref = nullptr;
    if (s.lod && s.mesh.size() > 5 && s.mesh.compare(s.mesh.size() - 5, 5, "_LOD1") == 0)
      if (const granny::Mesh* g0 = find_mesh(f, s.mesh.substr(0, s.mesh.size() - 5))) { ref = to_mesh(*g0); grid_ref = &ref; }
    if (!export_dir.empty()) {
      Input x = in;
      if (grid_ref) { double fit; detect_tile_grid(ref, x.grid, &fit); x.has_grid = true; }
      std::string safe;
      for (char c : s.name) safe += (isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '_') ? c : '_';
      write_hcap_v1(fs::path(widen(export_dir)) / widen(safe + ".hcap"), x);
    }
    return process(s.name, in, grid_ref, opt, extra);
  };
  std::vector<std::thread> pool;
  for (int w = 0; w < std::min<int>(jobs, int(todo.size())); ++w)
    pool.emplace_back([&] {
      for (size_t k = next++; k < todo.size(); k = next++) {
        const size_t i = todo[k];
        const auto& s = rows[i];
        const auto t0 = Clock::now();
        const std::string extra = ", \"gr2\": \"" + json_escape(s.gr2) + "\", \"mesh\": \"" + json_escape(s.mesh) + "\", \"lod\": " +
                                  std::to_string(s.lod) + ", \"mask\": \"" + json_escape(s.mask) + "\", \"visual\": \"" +
                                  json_escape(s.visual) + "\", \"category\": \"" + json_escape(s.category) + "\", \"tier\": \"" +
                                  catalogue::kTierNames[s.tier] + "\"";
        Rec r = convert_one(s, extra);
        const double took = secs(t0);
        std::lock_guard<std::mutex> lock(mu);
        state[s.name] = {keys[i], r.json};
        records.push_back(r.json);
        run_seconds.push_back(took);
        ++done;
        rejects += r.verdict == "reject";
        if (!report_dir.empty()) {
          std::string text;
          for (auto& [n, v] : state) text += n + "\t" + v.first + "\t" + v.second + "\n";
          write_text(state_path, text);   // resumable after every style
        }
        if (secs(last) >= 30 || done == todo.size()) {
          const double rate = done / std::max(1e-3, secs(run_start));
          char b[240];
          snprintf(b, sizeof b, "[hairconv all] %zu/%zu done, %.1f/min, ETA %.0fs, rejects %zu (last: %s, %s, %.1fs)", done, todo.size(),
                   rate * 60, (todo.size() - done) / std::max(rate, 1e-9), rejects, s.name.c_str(), r.verdict.c_str(), took);
          say(b);
          save(true);
          last = Clock::now();
        }
      }
    });
  for (auto& t : pool) t.join();
  {
    std::lock_guard<std::mutex> lock(mu);
    save(false);
  }
  if (!json_path.empty()) {
    std::string j = "[\n";
    for (size_t i = 0; i < records.size(); ++i) j += (i ? ",\n" : "") + records[i];
    write_text(widen(json_path), j + "\n]\n");
  }
  say("[hairconv all] complete in " + std::to_string(int(secs(start))) + "s: " + std::to_string(done) + " styles converted, " +
      std::to_string(rejects) + " rejects");
  return 0;
}

}  // namespace hairconv
