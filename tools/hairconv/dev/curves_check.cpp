// Dev check of tools/hairconv/curves2d.cpp against the Python reference (tools/hair_groom/curves2d.py,
// convert.curve_strands). Inputs: <dir>/<name>.mask (h*w*4 uint8), .json (w, h, grid, root_row_corr, stats, corr),
// .rows / .box (float64), .fields (typ, fx, fy float32) as written by a dumper of the reference.
// Usage: hairconv_curves_check <dir> <name> [<name>...]; exit 0 when every case matches.
#include "../curves2d.cpp"  // the anonymous-namespace stages are checked too

#include <chrono>
#include <cstdio>
#include <fstream>
#include <sstream>

using namespace hairconv;

static bool read_file(const std::string& p, std::string& s) {
  std::ifstream f(p, std::ios::binary);
  if (!f) return false;
  std::stringstream ss;
  ss << f.rdbuf();
  s = ss.str();
  return true;
}

// All numbers after "key": up to the bracket that closes the value (null -> NaN).
static std::vector<double> json_numbers(const std::string& js, const std::string& key) {
  std::vector<double> out;
  size_t p = js.find("\"" + key + "\":");
  if (p == std::string::npos) return out;
  p += key.size() + 3;
  int depth = 0;
  while (p < js.size()) {
    char c = js[p];
    if (c == '[') ++depth;
    else if (c == ']') { if (--depth <= 0) break; }
    else if ((c == ',' || c == '}') && depth == 0) break;
    else if (c == 'n' && js.compare(p, 4, "null") == 0) { out.push_back(NAN); p += 4; continue; }
    else if (c == 'N' && js.compare(p, 3, "NaN") == 0) { out.push_back(NAN); p += 3; continue; }
    else if (c == '-' || (c >= '0' && c <= '9')) {
      char* e;
      out.push_back(std::strtod(js.c_str() + p, &e));
      p = size_t(e - js.c_str());
      continue;
    }
    ++p;
  }
  return out;
}

static bool same(double a, double b, double tol) { return (std::isnan(a) && std::isnan(b)) || std::abs(a - b) <= tol; }

int main(int argc, char** argv) {
  if (argc < 3) { std::printf("usage: %s <dir> <name>...\n", argv[0]); return 2; }
  std::string dir = argv[1];
  int fails = 0;
  for (int ai = 2; ai < argc; ++ai) {
    std::string name = argv[ai], base = dir + "/" + name, js, raw;
    if (!read_file(base + ".json", js) || !read_file(base + ".mask", raw)) { std::printf("%s: missing input\n", name.c_str()); return 2; }
    Mask m;
    m.w = int(json_numbers(js, "w")[0]);
    m.h = int(json_numbers(js, "h")[0]);
    m.rgba.assign(raw.begin(), raw.end());
    std::vector<double> g = json_numbers(js, "grid");
    int grid[2] = {int(g[0]), int(g[1])};
    double rc = NAN;
    auto t0 = std::chrono::steady_clock::now();
    bool has = root_row_correlation(m, grid, 128, &rc);
    double t_rrc = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    double ref_rc = json_numbers(js, "root_row_corr")[0];
    bool ok_rc = has ? same(rc, ref_rc, 1e-9) : std::isnan(ref_rc);
    std::printf("%s: root_row_corr %.12f ref %.12f %s (%.1f ms)\n", name.c_str(), rc, ref_rc, ok_rc ? "ok" : "MISMATCH",
                t_rrc * 1e3);
    fails += !ok_rc;
    std::string fields, rowsb, boxb;
    if (!read_file(base + ".fields", fields)) continue;
    // stage check: typical map and orientation field
    {
      size_t n = size_t(m.w) * m.h;
      const float* ref = reinterpret_cast<const float*>(fields.data());
      Tracer tr(m, 128);
      size_t dt = 0, dfx = 0, dfy = 0;
      float mfx = 0, mfy = 0;
      for (size_t i = 0; i < n; ++i) {
        dt += tr.typ[i] != ref[i];
        if (tr.fx[i] != ref[n + i]) { ++dfx; mfx = std::max(mfx, std::abs(tr.fx[i] - ref[n + i])); }
        if (tr.fy[i] != ref[2 * n + i]) { ++dfy; mfy = std::max(mfy, std::abs(tr.fy[i] - ref[2 * n + i])); }
      }
      std::printf("  fields: typ diff %zu, fx diff %zu (max %.3g), fy diff %zu (max %.3g) of %zu\n", dt, dfx, mfx, dfy,
                  mfy, n);
    }
    read_file(base + ".rows", rowsb);
    read_file(base + ".box", boxb);
    const double* rr = reinterpret_cast<const double*>(rowsb.data());
    const double* bx = reinterpret_cast<const double*>(boxb.data());
    std::vector<Row> rows;
    std::vector<double> lo, hi, corr;
    std::map<std::pair<int, int>, TileStat> stats;
    t0 = std::chrono::steady_clock::now();
    curve_strands(m, grid, 16, 128, rows, lo, hi, stats, corr, 1);
    double t_cs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    size_t nref = size_t(rr[0]), p = 1;
    std::printf("  rows %zu ref %zu, %.1f ms\n", rows.size(), nref, t_cs * 1e3);
    bool ok = rows.size() == nref;
    double max_px = 0, max_other = 0;
    size_t bad_rows = 0, first_bad = SIZE_MAX;
    for (size_t r = 0; r < nref && r < rows.size(); ++r) {
      size_t n = size_t(rr[p++]);
      const Row& R = rows[r];
      bool rok = R.u.size() == n;
      if (rok) {
        const std::vector<double>* cols[8] = {&R.u, &R.v, &R.wd, &R.slope, &R.root, &R.depth, &R.nx, &R.ny};
        for (int c = 0; c < 8; ++c)
          for (size_t i = 0; i < n; ++i) {
            double a = (*cols[c])[i], b = rr[p + c * n + i];
            double dpx = c == 0 || c == 2 ? std::abs(a - b) * m.w : c == 1 ? std::abs(a - b) * m.h : std::abs(a - b);
            if (c <= 1) max_px = std::max(max_px, dpx);
            else max_other = std::max(max_other, dpx);
            if (dpx > 1e-6) rok = false;
          }
      }
      p += 8 * n;
      if (!rok) { ++bad_rows; if (first_bad == SIZE_MAX) first_bad = r; }
    }
    double max_box = 0;
    if (rows.size() == nref)
      for (size_t i = 0; i < 2 * nref; ++i) {
        max_box = std::max(max_box, std::abs(lo[i] - bx[i]));
        max_box = std::max(max_box, std::abs(hi[i] - bx[2 * nref + i]));
      }
    std::vector<double> st = json_numbers(js, "stats"), rc2 = json_numbers(js, "corr");
    double max_stat = 0;
    bool ok_stats = st.size() == 5 * stats.size();
    for (size_t i = 0; ok_stats && i < stats.size(); ++i) {
      auto it = stats.find({int(st[5 * i]), int(st[5 * i + 1])});
      if (it == stats.end() || it->second.strands != int(st[5 * i + 2])) { ok_stats = false; break; }
      max_stat = std::max({max_stat, std::abs(it->second.typical_px - st[5 * i + 3]), std::abs(it->second.recall - st[5 * i + 4])});
    }
    bool ok_corr = rc2.size() == corr.size();
    double max_corr = 0;
    for (size_t i = 0; ok_corr && i < corr.size(); ++i) max_corr = std::max(max_corr, std::abs(corr[i] - rc2[i]));
    ok = ok && !bad_rows && max_box <= 1e-9 && ok_stats && max_stat <= 1e-9 && ok_corr && max_corr <= 1e-9;
    std::printf("  bad rows %zu (first %lld), max point diff %.3g px, other cols %.3g, box %.3g, stats %s %.3g, corr %zu/%zu %.3g -> %s\n",
                bad_rows, first_bad == SIZE_MAX ? -1LL : (long long)first_bad, max_px, max_other, max_box,
                ok_stats ? "ok" : "MISMATCH", max_stat, corr.size(), rc2.size(), max_corr, ok ? "PASS" : "FAIL");
    fails += !ok;
  }
  return fails ? 1 : 0;
}
