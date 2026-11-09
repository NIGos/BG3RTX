// bg3rtx_hairconv: native strand groom converter (docs/RTX_HAIR_ROADMAP.md 2d, 2f). No Python, LSLib or PowerShell.
//
//   bg3rtx_hairconv <file.hcap | dir>... --out <groom dir> [--threads N] [--jobs J] [--check full|cheap]
//                   [--accept-review] [--report-dir <dir>] [--json <file>]
//   bg3rtx_hairconv --all --game "<BG3 install or its Data folder>" --granny "<granny2.dll>" --out <groom dir>
//                   [--report-dir <dir>] ...   (every catalogue style from the game's paks: all.cpp, roadmap 2f)
//
// Per style, the reference pipeline (tools/hair_groom/hcap.py reference = batch.process): grid, root check, trace,
// dedupe, head (the file's or fitted to the roots), orientation / reorient, check, v3 write. --check full adds the
// validation renders and batch.py's verdict rules (pass / needs-review / reject); cheap keeps the structural checks.
// --report-dir: <dir>\state.json (incremental by input name + size + mtime + converter version) and
// <dir>\preparation.json (the launcher's Preparation card) every 30 s and at the end; progress lines every 30 s.
// Inputs are processed in modification order (the order the game saw the styles). BelowNormal priority.
#include "hairconv.h"
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
#include <filesystem>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace fs = std::filesystem;
using namespace hairconv;

namespace {
using Clock = std::chrono::steady_clock;
double secs(Clock::time_point a) { return std::chrono::duration<double>(Clock::now() - a).count(); }

Rec process_file(const std::string& path, const Options& o) {
  const std::string name = fs::path(path).stem().string();
  const auto t0 = Clock::now();
  Input in;
  std::string error;
  if (!read_hcap(path, in, &error)) return reject(name, error);
  Rec r = process(name, in, nullptr, o, ", \"read_s\": " + std::to_string(secs(t0)));
  return r;
}
}  // namespace

int main(int argc, char** argv) {
  for (int i = 1; i < argc; ++i)
    if (!std::strcmp(argv[i], "--all") || !std::strcmp(argv[i], "--dump-gr2")) return run_all(argc - 1, argv + 1);
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
             "                [--report-dir <dir>] [--json <file>]\n"
             "bg3rtx_hairconv --all --game <dir> --granny <granny2.dll> --out <dir> [--report-dir <dir>] ... (--all --help)\n");
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
        Rec r = process_file(todo[i].path, Options{out_dir, threads, full, accept_review});
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
