// bg3rtx_hairconv: native strand groom converter (docs/RTX_HAIR_ROADMAP.md 2d). No Python, LSLib or granny2.
//
//   bg3rtx_hairconv <file.hcap | dir>... --out <groom dir> [--threads N] [--jobs J] [--check full|cheap]
//                   [--accept-review] [--report-dir <dir>] [--json <file>]
//
// Per style, the reference pipeline (tools/hair_groom/hcap.py reference = batch.process): grid, root check, trace,
// dedupe, head (the file's or fitted to the roots), orientation / reorient, check, v3 write. --check full adds the
// validation renders and batch.py's verdict rules (pass / needs-review / reject); cheap keeps the structural checks.
// --report-dir: <dir>\state.json (incremental by input name + size + mtime + converter version) and
// <dir>\preparation.json (the launcher's Preparation card) every 30 s and at the end; progress lines every 30 s.
// Inputs are processed in modification order (the order the game saw the styles). BelowNormal priority.
#include "hairconv.h"

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
#include <filesystem>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace fs = std::filesystem;
using namespace hairconv;

namespace {
constexpr const char* kVersion = "hairconv-1";
// batch.py verdict thresholds.
constexpr double kIouPass = 0.85, kIouReject = 0.70, kRootReject = 0.3, kCoverReview = 0.08, kRadianceReview = 0.15,
                 kReversedReview = 0.25, kMixedReview = kCoverReview;

std::string esc(const std::string& s) {
  std::string o;
  for (char c : s) {
    if (c == '"' || c == '\\') { o += '\\'; o += c; }
    else if (static_cast<unsigned char>(c) < 0x20) o += ' ';
    else o += c;
  }
  return o;
}
std::string num(double v, int digits = 6) {
  if (!std::isfinite(v)) return "null";
  char b[64];
  snprintf(b, sizeof b, "%.*g", digits, v);
  return b;
}
std::string fmt(const char* f, double v) { char b[64]; snprintf(b, sizeof b, f, v); return b; }
double round3(double v) { return std::nearbyint(v * 1000) / 1000; }
double median(std::vector<double> v) {
  std::sort(v.begin(), v.end());
  const size_t n = v.size();
  return n % 2 ? v[n / 2] : (v[n / 2 - 1] + v[n / 2]) / 2;
}
using Clock = std::chrono::steady_clock;
double secs(Clock::time_point a) { return std::chrono::duration<double>(Clock::now() - a).count(); }

struct Rec {
  std::string name, verdict, asset, json;
  std::vector<std::string> reasons;
  int strands = 0;
  uint64_t bytes = 0;
  double seconds = 0;
};

Rec process(const std::string& path, const std::string& out_dir, int threads, bool full, bool accept_review) {
  Rec rec;
  rec.name = fs::path(path).stem().string();
  std::map<std::string, double> t;
  auto t0 = Clock::now(), all = t0;
  auto lap = [&](const char* k) { t[k] += secs(t0); t0 = Clock::now(); };
  Input in;
  std::string error;
  if (!read_hcap(path, in, &error)) {
    rec.verdict = "reject";
    rec.reasons.push_back(error);
  } else {
    lap("read");
    int grid[2];
    double fit = 0;
    detect_tile_grid(in.mesh, grid, &fit);
    if (in.has_grid) { grid[0] = in.grid[0]; grid[1] = in.grid[1]; }
    std::vector<double> root_corr = root_channel_check(in.mask, grid);
    lap("grid");
    Report rep;
    Groom g = convert(in.mesh, in.mask, grid, rep, threads);
    if (rep.tracer == "2d") root_corr = rep.strand_root_corr;
    lap("trace");
    int dropped = 0;
    g = dedupe(g, &dropped, threads);
    lap("dedupe");
    Head head = in.has_head ? in.head : head_from_roots(g);
    std::vector<double> finite;
    for (double c : root_corr) finite.push_back(c);
    const double corr_med = finite.empty() ? NAN : median(finite);
    const bool bad_root = !finite.empty() && corr_med < kRootReject;
    const bool geometry_root = bad_root && head.valid();
    int decided = 0, reversed = 0;
    if (geometry_root) {
      decided = orientation(g, head.centre).strands_decided;
      g = reorient(g, head.centre, &reversed);
    }
    Orientation orient;
    const bool have_orient = head.valid();
    if (have_orient) orient = orientation(g, head.centre);
    lap("orientation");
    std::vector<std::string> reasons, review;
    if (!g.strands()) reasons.push_back("no strands");
    if (bad_root && !geometry_root) reasons.push_back(fmt("root channel does not run along the rows (median corr %.2f)", corr_med));
    if (geometry_root && decided < g.strands() / 2.0)
      review.push_back(fmt("root from geometry (mask root channel does not run along the rows, median corr %.2f)", corr_med));
    Validation val;
    if (full && g.strands()) {
      val = validate_full(in.mesh, in.mask, g, head, threads);
      lap("validation");
      const double iou_min = *std::min_element(val.iou.begin(), val.iou.end());
      double cov = 0, light = 0;
      for (double c : val.coverage) if (std::fabs(c) > std::fabs(cov)) cov = c;
      for (double l : val.radiance) if (std::fabs(l) > std::fabs(light)) light = l;
      if (iou_min < kIouReject) reasons.push_back(fmt("silhouette IoU %.2f", iou_min));
      if (std::fabs(cov) > kCoverReview) review.push_back(fmt("coverage differs by %+.2f", cov));
      if (std::fabs(light) > kRadianceReview) review.push_back(fmt("mean radiance differs by %+.0f%%", light * 100));
      if (rep.mixed_winding_cards > kMixedReview * std::max(rep.cards, 1))
        review.push_back(std::to_string(rep.mixed_winding_cards) + " cards with mirrored UV triangles");
      if (iou_min < kIouPass) review.push_back(fmt("silhouette IoU %.2f", iou_min));
      if (have_orient && orient.cards_decided && orient.cards_mostly_reversed > kReversedReview * orient.cards_decided)
        review.push_back(std::to_string(orient.cards_mostly_reversed) + " cards with the root away from the head");
    } else if (!full) {
      // Cheap check: the structural rules only (the launcher's full check adds the renders).
      if (rep.samples && double(rep.unmapped_samples) / rep.samples > 0.95) reasons.push_back("almost no strand sample lands on a card");
    }
    rec.verdict = !reasons.empty() ? "reject" : !review.empty() ? "needs-review" : "pass";
    rec.reasons = reasons;
    rec.reasons.insert(rec.reasons.end(), review.begin(), review.end());
    rec.strands = int(g.strands());
    int bits = 0;
    if (rec.verdict == "pass" || (rec.verdict == "needs-review" && accept_review)) {
      char name[32];
      snprintf(name, sizeof name, "%016llx.hgrm", (unsigned long long)index_id(in.mesh));
      fs::create_directories(out_dir);
      const std::string dst = (fs::path(out_dir) / name).string();
      bits = write_v3(dst, in.mesh, g, &error);
      if (bits) { rec.asset = name; std::error_code ec; rec.bytes = fs::file_size(dst, ec); }
      else { rec.verdict = "reject"; rec.reasons.push_back(error); }
    }
    lap("write");
    rec.seconds = secs(all);
    // The record, in hcap.reference's field names.
    std::string j = "{\"name\": \"" + esc(rec.name) + "\", \"verdict\": \"" + rec.verdict + "\", \"reasons\": [";
    for (size_t i = 0; i < rec.reasons.size(); ++i) j += (i ? ", \"" : "\"") + esc(rec.reasons[i]) + "\"";
    j += "], \"tile_grid\": [" + std::to_string(grid[0]) + ", " + std::to_string(grid[1]) + "], \"cards\": " + std::to_string(rep.cards) +
         ", \"triangles\": " + std::to_string(rep.triangles) + ", \"cards_in_one_tile\": " + num(std::nearbyint(fit * 1e4) / 1e4) +
         ", \"mixed_winding_cards\": " + std::to_string(rep.mixed_winding_cards) + ", \"uv_overlap_samples\": " +
         std::to_string(rep.uv_overlap_samples) + ", \"unmapped_share\": " +
         num(std::nearbyint(double(rep.unmapped_samples) / std::max(rep.samples, 1LL) * 1e5) / 1e5) +
         ", \"root_row_corr_median\": " + (finite.empty() ? std::string("null") : num(round3(corr_med))) +
         ", \"strands\": " + std::to_string(g.strands()) + ", \"segments\": " + std::to_string(g.points_count() - g.strands()) +
         ", \"dedupe_dropped\": " + std::to_string(dropped) + ", \"tracer\": \"" + rep.tracer + "\"";
    if (have_orient) j += ", \"reversed_cards\": " + std::to_string(orient.cards_mostly_reversed);
    if (geometry_root) j += ", \"root_source\": \"geometry\", \"root_reversed_strands\": " + std::to_string(reversed) +
                            ", \"root_decided_strands\": " + std::to_string(decided);
    auto arr = [&](const char* k, const std::vector<double>& v) {
      j += std::string(", \"") + k + "\": [";
      for (size_t i = 0; i < v.size(); ++i) j += (i ? ", " : "") + num(round3(v[i]));
      j += "]";
    };
    if (full) { arr("iou", val.iou); arr("coverage_delta", val.coverage); arr("radiance_delta", val.radiance); }
    if (!rec.asset.empty()) j += ", \"asset\": \"" + rec.asset + "\", \"asset_bits\": " + std::to_string(bits);
    j += ", \"seconds\": {";
    bool first = true;
    for (auto& [k, v] : t) { j += (first ? "\"" : ", \"") + k + "\": " + num(v, 4); first = false; }
    j += "}}";
    rec.json = j;
    return rec;
  }
  rec.seconds = secs(all);
  rec.json = "{\"name\": \"" + esc(rec.name) + "\", \"verdict\": \"reject\", \"reasons\": [\"" + esc(error) + "\"]}";
  return rec;
}

bool write_text(const fs::path& p, const std::string& text) {
  const fs::path tmp = p.string() + "." + std::to_string(GetCurrentProcessId()) + ".tmp";
  FILE* f = nullptr;
  if (_wfopen_s(&f, tmp.c_str(), L"wb") != 0 || !f) return false;
  const bool ok = fwrite(text.data(), 1, text.size(), f) == text.size();
  fclose(f);
  return ok && MoveFileExW(tmp.c_str(), p.c_str(), MOVEFILE_REPLACE_EXISTING);
}
std::string read_text(const fs::path& p) {
  FILE* f = nullptr;
  if (_wfopen_s(&f, p.c_str(), L"rb") != 0 || !f) return {};
  std::string s;
  char buf[65536];
  size_t n;
  while ((n = fread(buf, 1, sizeof buf, f)) > 0) s.append(buf, n);
  fclose(f);
  return s;
}
}  // namespace

int main(int argc, char** argv) {
  std::vector<std::string> inputs;
  std::string out_dir, report_dir, json_path;
  int threads = 4, jobs = 1;
  bool full = true, accept_review = false;
  for (int i = 1; i < argc; ++i) {
    const std::string a = argv[i];
    auto next = [&]() { return i + 1 < argc ? std::string(argv[++i]) : std::string(); };
    if (a == "--out" || a == "--out-dir") out_dir = next();
    else if (a == "--threads") threads = std::max(1, atoi(next().c_str()));
    else if (a == "--jobs") jobs = std::max(1, atoi(next().c_str()));
    else if (a == "--check") full = next() != "cheap";
    else if (a == "--accept-review") accept_review = true;
    else if (a == "--report-dir") report_dir = next();
    else if (a == "--json") json_path = next();
    else if (a == "--help" || a == "-h") {
      printf("bg3rtx_hairconv <file.hcap|dir>... --out <dir> [--threads N] [--jobs J] [--check full|cheap] [--accept-review]\n"
             "                [--report-dir <dir>] [--json <file>]\n");
      return 0;
    } else inputs.push_back(a);
  }
  if (inputs.empty() || out_dir.empty()) { fprintf(stderr, "bg3rtx_hairconv: inputs and --out are required (--help)\n"); return 2; }
  SetPriorityClass(GetCurrentProcess(), BELOW_NORMAL_PRIORITY_CLASS);
  // Inputs: files and the .hcap files of directories, oldest first (the order the game saw them).
  struct In { std::string path, key; fs::file_time_type time; uint64_t size; };
  std::vector<In> files;
  auto add = [&](const fs::path& p) {
    std::error_code ec;
    In f{p.string(), "", fs::last_write_time(p, ec), uint64_t(fs::file_size(p, ec))};
    f.key = std::string(kVersion) + "|" + std::to_string(f.size) + "|" + std::to_string(f.time.time_since_epoch().count()) +
            (full ? "|full" : "|cheap");
    files.push_back(f);
  };
  for (const std::string& s : inputs) {
    std::error_code ec;
    if (fs::is_directory(s, ec)) {
      for (auto& e : fs::directory_iterator(s, ec))
        if (e.path().extension() == ".hcap") add(e.path());
    } else add(s);
  }
  std::stable_sort(files.begin(), files.end(), [](const In& a, const In& b) { return a.time < b.time; });
  // Incremental: state.json maps name -> key and the record.
  std::map<std::string, std::pair<std::string, std::string>> state;   // name -> (key, record json)
  const fs::path state_path = report_dir.empty() ? fs::path() : fs::path(report_dir) / "state.json";
  if (!report_dir.empty()) {
    fs::create_directories(report_dir);
    const std::string text = read_text(state_path);
    // One "name\tkey\trecord" line per style (a flat, append-friendly layout).
    size_t at = 0;
    while (at < text.size()) {
      size_t nl = text.find('\n', at);
      const std::string line = text.substr(at, nl == std::string::npos ? std::string::npos : nl - at);
      at = nl == std::string::npos ? text.size() : nl + 1;
      const size_t a = line.find('\t'), b = a == std::string::npos ? a : line.find('\t', a + 1);
      if (b != std::string::npos) state[line.substr(0, a)] = {line.substr(a + 1, b - a - 1), line.substr(b + 1)};
    }
  }
  std::vector<In> todo;
  for (const In& f : files) {
    const auto it = state.find(fs::path(f.path).stem().string());
    if (it == state.end() || it->second.first != f.key) todo.push_back(f);
  }
  std::mutex mu;
  std::atomic<int> next{0}, done{0}, ready{0};
  for (auto& [n, v] : state) ready += v.second.find("\"verdict\": \"pass\"") != std::string::npos;
  const auto start = Clock::now();
  auto last = start;
  std::vector<std::string> records;
  auto save = [&](bool running) {
    if (report_dir.empty()) return;
    std::string text;
    for (auto& [n, v] : state) text += n + "\t" + v.first + "\t" + v.second + "\n";
    write_text(state_path, text);
    uint64_t bytes = 0;
    std::error_code ec;
    for (auto& e : fs::directory_iterator(out_dir, ec))
      if (e.path().extension() == ".hgrm") bytes += e.file_size(ec);
    const int left = int(todo.size()) - done.load();
    const double rate = done ? double(done) / std::max(1e-3, secs(start)) : 0;
    const double eta = rate > 0 ? left / rate : 0;
    const int needed = int(files.size());
    char line[160], detail[160];
    snprintf(line, sizeof line, "%d of %d styles ready, %.1f %s on disk.", ready.load(), needed,
             bytes >= 100000000 ? bytes / 1e9 : bytes / 1e6, bytes >= 100000000 ? "GB" : "MB");
    if (!left) snprintf(detail, sizeof detail, "All captured styles are done.");
    else if (running) snprintf(detail, sizeof detail, "Converting styles seen in game, about %d min left.", std::max(1, int(std::lround(eta / 60))));
    else snprintf(detail, sizeof detail, "%d styles left.", left);
    std::string j = "{\"kind\": \"hair_grooms\", \"ready\": " + std::to_string(ready.load()) + ", \"finished\": " +
                    std::to_string(needed - left) + ", \"needed\": " + std::to_string(needed) + ", \"bytes\": " +
                    std::to_string(bytes) + ", \"eta_s\": " + std::to_string(int(eta)) + ", \"state\": \"" +
                    (running && left ? "running" : left ? "waiting" : "done") + "\", \"line\": \"" + line +
                    "\", \"detail\": \"" + detail + "\", \"pid\": " + std::to_string(GetCurrentProcessId()) + "}";
    write_text(fs::path(report_dir) / "preparation.json", j);
  };
  printf("[hairconv] %zu styles (%zu unchanged), %d jobs x %d threads, %s check, out %s\n", todo.size(),
         files.size() - todo.size(), jobs, threads, full ? "full" : "cheap", out_dir.c_str());
  fflush(stdout);
  save(true);
  std::vector<std::thread> pool;
  for (int w = 0; w < std::min<int>(jobs, int(todo.size())); ++w)
    pool.emplace_back([&] {
      for (int i = next++; i < int(todo.size()); i = next++) {
        Rec r = process(todo[i].path, out_dir, threads, full, accept_review);
        std::lock_guard<std::mutex> lock(mu);
        const auto old = state.find(r.name);
        if (old != state.end() && old->second.second.find("\"verdict\": \"pass\"") != std::string::npos) --ready;
        state[r.name] = {todo[i].key, r.json};
        ready += r.verdict == "pass";
        records.push_back(r.json);
        ++done;
        if (secs(last) >= 30 || done == int(todo.size())) {
          const double rate = done / std::max(1e-3, secs(start));
          printf("[hairconv] %d/%zu done, %.1f/min, ETA %.0fs\n", done.load(), todo.size(), rate * 60,
                 (todo.size() - done) / std::max(rate, 1e-9));
          fflush(stdout);
          save(true);
          last = Clock::now();
        }
      }
    });
  for (auto& t : pool) t.join();
  save(false);
  if (!json_path.empty()) {
    std::string j = "[\n";
    for (size_t i = 0; i < records.size(); ++i) j += (i ? ",\n" : "") + records[i];
    write_text(json_path, j + "\n]\n");
  }
  printf("[hairconv] complete in %.1fs: %d styles converted\n", secs(start), done.load());
  return 0;
}
