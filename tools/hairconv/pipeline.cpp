// One style through the converter (pipeline.h): grid, root check, trace, dedupe, head, orientation / reorient,
// validation and batch.py's verdict rules, v3 write. Moved from main.cpp unchanged in its arithmetic.
#include "pipeline.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <map>

namespace fs = std::filesystem;

namespace hairconv {

namespace {
// batch.py verdict thresholds.
constexpr double kIouPass = 0.85, kIouReject = 0.70, kRootReject = 0.3, kCoverReview = 0.08, kRadianceReview = 0.15,
                 kReversedReview = 0.25, kMixedReview = kCoverReview;

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
}  // namespace

std::string json_escape(const std::string& s) {
  std::string o;
  for (char c : s) {
    if (c == '"' || c == '\\') { o += '\\'; o += c; }
    else if (static_cast<unsigned char>(c) < 0x20) o += ' ';
    else o += c;
  }
  return o;
}

Rec reject(const std::string& name, const std::string& reason, const std::string& extra_json) {
  Rec rec;
  rec.name = name;
  rec.verdict = "reject";
  rec.reasons.push_back(reason);
  rec.json = "{\"name\": \"" + json_escape(name) + "\", \"verdict\": \"reject\", \"reasons\": [\"" + json_escape(reason) + "\"]" +
             extra_json + "}";
  return rec;
}

Rec process(const std::string& name, const Input& in_arg, const Mesh* grid_ref, const Options& o, const std::string& extra_json) {
  Rec rec;
  rec.name = name;
  std::map<std::string, double> t;
  auto t0 = Clock::now(), all = t0;
  auto lap = [&](const char* k) { t[k] += secs(t0); t0 = Clock::now(); };
  const Input& in = in_arg;
  std::string error;
  int grid[2];
  double fit = 0;
  detect_tile_grid(grid_ref ? *grid_ref : in.mesh, grid, &fit);
  if (in.has_grid) { grid[0] = in.grid[0]; grid[1] = in.grid[1]; }
  std::vector<double> root_corr = root_channel_check(in.mask, grid);
  lap("grid");
  Report rep;
  Groom g = convert(in.mesh, in.mask, grid, rep, o.threads);
  if (rep.tracer == "2d") root_corr = rep.strand_root_corr;
  lap("trace");
  int dropped = 0;
  g = dedupe(g, &dropped, o.threads);
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
  if (o.full && g.strands()) {
    val = validate_full(in.mesh, in.mask, g, head, o.threads);
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
  } else if (!o.full) {
    // Cheap check: the structural rules only (the launcher's full check adds the renders).
    if (rep.samples && double(rep.unmapped_samples) / rep.samples > 0.95) reasons.push_back("almost no strand sample lands on a card");
  }
  rec.verdict = !reasons.empty() ? "reject" : !review.empty() ? "needs-review" : "pass";
  rec.reasons = reasons;
  rec.reasons.insert(rec.reasons.end(), review.begin(), review.end());
  rec.strands = int(g.strands());
  char id[32];
  snprintf(id, sizeof id, "%016llx", (unsigned long long)index_id(in.mesh));
  rec.index_id = id;
  rec.indices = int(in.mesh.tris.size());
  int bits = 0;
  if (rec.verdict == "pass" || (rec.verdict == "needs-review" && o.accept_review)) {
    const std::string file = rec.index_id + ".hgrm";
    fs::create_directories(o.out_dir);
    const std::string dst = (fs::path(o.out_dir) / file).string();
    bits = write_v3(dst, in.mesh, g, &error);
    if (bits) { rec.asset = file; std::error_code ec; rec.bytes = fs::file_size(dst, ec); }
    else { rec.verdict = "reject"; rec.reasons.push_back(error); }
  }
  lap("write");
  rec.seconds = secs(all);
  // The record, in hcap.reference's field names.
  std::string j = "{\"name\": \"" + json_escape(rec.name) + "\", \"verdict\": \"" + rec.verdict + "\", \"reasons\": [";
  for (size_t i = 0; i < rec.reasons.size(); ++i) j += (i ? ", \"" : "\"") + json_escape(rec.reasons[i]) + "\"";
  j += "], \"tile_grid\": [" + std::to_string(grid[0]) + ", " + std::to_string(grid[1]) + "], \"cards\": " + std::to_string(rep.cards) +
       ", \"triangles\": " + std::to_string(rep.triangles) + ", \"cards_in_one_tile\": " + num(std::nearbyint(fit * 1e4) / 1e4) +
       ", \"mixed_winding_cards\": " + std::to_string(rep.mixed_winding_cards) + ", \"uv_overlap_samples\": " +
       std::to_string(rep.uv_overlap_samples) + ", \"unmapped_share\": " +
       num(std::nearbyint(double(rep.unmapped_samples) / std::max(rep.samples, 1LL) * 1e5) / 1e5) +
       ", \"root_row_corr_median\": " + (finite.empty() ? std::string("null") : num(round3(corr_med))) +
       ", \"strands\": " + std::to_string(g.strands()) + ", \"segments\": " + std::to_string(g.points_count() - g.strands()) +
       ", \"dedupe_dropped\": " + std::to_string(dropped) + ", \"tracer\": \"" + rep.tracer + "\"" +
       ", \"index_id\": \"" + rec.index_id + "\", \"indices\": " + std::to_string(rec.indices);
  if (have_orient) j += ", \"reversed_cards\": " + std::to_string(orient.cards_mostly_reversed);
  if (geometry_root) j += ", \"root_source\": \"geometry\", \"root_reversed_strands\": " + std::to_string(reversed) +
                          ", \"root_decided_strands\": " + std::to_string(decided);
  auto arr = [&](const char* k, const std::vector<double>& v) {
    j += std::string(", \"") + k + "\": [";
    for (size_t i = 0; i < v.size(); ++i) j += (i ? ", " : "") + num(round3(v[i]));
    j += "]";
  };
  if (o.full) {
    arr("iou", val.iou); arr("coverage_delta", val.coverage); arr("radiance_delta", val.radiance);
    arr("coverage_ratio", val.ratio);   // 5u: sum of strand opacity / sum of card opacity, the gate bound's quantity
  }
  if (!rec.asset.empty()) j += ", \"asset\": \"" + rec.asset + "\", \"asset_bits\": " + std::to_string(bits);
  j += extra_json;
  j += ", \"seconds\": {";
  bool first = true;
  for (auto& [k, v] : t) { j += (first ? "\"" : ", \"") + k + "\": " + num(v, 4); first = false; }
  j += "}}";
  rec.json = j;
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

}  // namespace hairconv
