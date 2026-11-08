// bg3rtx_hairconv core: geom.py, strands2d.py, convert.py (cards, grid, locate, convert, run_radii, dedupe, thin),
// analysis.py (root check, orientation, reorient) and capture.head_from_roots. Same arithmetic as the reference where
// it decides a discrete outcome; the reference's float32 inputs (positions, UV) are widened exactly where numpy does.
#include "hairconv.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <numeric>
#include <thread>
#include <unordered_map>

namespace hairconv {

namespace {
constexpr double kEps = 1e-4;
constexpr double kMaxRadius = 0.002;
constexpr float kMaxThinRadius = 0.009f;
constexpr double kCurlyRootCorr = 0.3;

template <class F>
void parallel(int n, int threads, F&& fn) {
  threads = std::max(1, std::min(threads, n));
  if (threads <= 1) {
    for (int i = 0; i < n; ++i) fn(i);
    return;
  }
  std::atomic<int> next{0};
  std::vector<std::thread> pool;
  for (int t = 0; t < threads; ++t)
    pool.emplace_back([&] {
      for (int i = next++; i < n; i = next++) fn(i);
    });
  for (auto& t : pool) t.join();
}

double median(std::vector<double> v) {
  if (v.empty()) return std::nan("");
  const size_t n = v.size();
  std::sort(v.begin(), v.end());
  return n % 2 ? v[n / 2] : (v[n / 2 - 1] + v[n / 2]) / 2;
}
}  // namespace

// ---------------------------------------------------------------- geom.py / convert.cards / detect_tile_grid
int cards(const Mesh& m, std::vector<int32_t>& card_of) {
  const uint32_t V = m.vertices(), T = m.triangles();
  std::vector<uint32_t> parent(V);
  std::iota(parent.begin(), parent.end(), 0u);
  auto find = [&](uint32_t a) {
    while (parent[a] != a) { parent[a] = parent[parent[a]]; a = parent[a]; }
    return a;
  };
  for (uint32_t t = 0; t < T; ++t)
    for (int k = 0; k < 3; ++k) {
      uint32_t a = find(m.tris[t * 3 + k]), b = find(m.tris[t * 3 + (k + 1) % 3]);
      if (a != b) parent[std::max(a, b)] = std::min(a, b);   // the root is the component's smallest vertex
    }
  // np.unique(parent) labels: components in order of their smallest vertex (isolated vertices count too).
  std::vector<int32_t> label(V, -1);
  int count = 0;
  for (uint32_t v = 0; v < V; ++v) {
    const uint32_t r = find(v);
    if (r == v) label[v] = count++;
  }
  card_of.resize(T);
  for (uint32_t t = 0; t < T; ++t) card_of[t] = label[find(m.tris[t * 3])];
  return count;
}

void detect_tile_grid(const Mesh& m, int grid[2], double* fit) {
  std::vector<int32_t> card_of;
  const int count = cards(m, card_of);
  std::vector<double> lo(size_t(count) * 2, INFINITY), hi(size_t(count) * 2, -INFINITY);
  for (uint32_t t = 0; t < m.triangles(); ++t)
    for (int k = 0; k < 3; ++k) {
      const uint32_t v = m.tris[t * 3 + k];
      for (int a = 0; a < 2; ++a) {
        const double x = m.uv[v * 2 + a];
        double& l = lo[size_t(card_of[t]) * 2 + a];
        double& h = hi[size_t(card_of[t]) * 2 + a];
        l = std::min(l, x);
        h = std::max(h, x);
      }
    }
  for (int a = 0; a < 2; ++a) {
    std::vector<double> span(count);
    for (int c = 0; c < count; ++c) span[c] = hi[size_t(c) * 2 + a] - lo[size_t(c) * 2 + a];
    const double s = median(span);
    grid[a] = int(std::clamp(std::nearbyint(1.0 / std::max(s, 1e-6)), 1.0, 16.0));
  }
  int fits = 0;
  for (int c = 0; c < count; ++c) {
    bool ok = true;
    for (int a = 0; a < 2; ++a)
      ok &= std::floor(lo[size_t(c) * 2 + a] * grid[a] + kEps) == std::floor(hi[size_t(c) * 2 + a] * grid[a] - kEps);
    fits += ok;
  }
  if (fit) *fit = count ? double(fits) / count : 0.0;
}

std::vector<std::pair<uint32_t, uint32_t>> pairs_within(const float* p, size_t n, double distance, int threads) {
  std::vector<std::pair<uint32_t, uint32_t>> out;
  if (n < 2) return out;
  double lo[3] = {INFINITY, INFINITY, INFINITY};
  for (size_t i = 0; i < n; ++i)
    for (int a = 0; a < 3; ++a) lo[a] = std::min(lo[a], double(p[i * 3 + a]));
  // Spatial hash: cells of `distance`, sorted by key; each point looks at its own and the 26 neighbouring cells.
  auto cell = [&](size_t i, int a) { return int64_t(std::floor((double(p[i * 3 + a]) - lo[a]) / distance)); };
  auto key = [](int64_t x, int64_t y, int64_t z) { return (uint64_t(x) * 73856093ull) ^ (uint64_t(y) * 19349663ull) ^ (uint64_t(z) * 83492791ull); };
  std::vector<int64_t> cx(n * 3);
  for (size_t i = 0; i < n; ++i)
    for (int a = 0; a < 3; ++a) cx[i * 3 + a] = cell(i, a);
  std::unordered_map<uint64_t, std::vector<uint32_t>> grid;
  grid.reserve(n / 2);
  for (size_t i = 0; i < n; ++i) grid[key(cx[i * 3], cx[i * 3 + 1], cx[i * 3 + 2])].push_back(uint32_t(i));
  const double d2 = distance * distance;
  const int T = std::max(1, threads);
  std::vector<std::vector<std::pair<uint32_t, uint32_t>>> parts(T);
  const size_t chunk = (n + T - 1) / T;
  parallel(T, T, [&](int t) {
    auto& o = parts[t];
    for (size_t i = size_t(t) * chunk; i < std::min(n, size_t(t + 1) * chunk); ++i) {
      for (int dx = -1; dx <= 1; ++dx)
        for (int dy = -1; dy <= 1; ++dy)
          for (int dz = -1; dz <= 1; ++dz) {
            const auto it = grid.find(key(cx[i * 3] + dx, cx[i * 3 + 1] + dy, cx[i * 3 + 2] + dz));
            if (it == grid.end()) continue;
            for (uint32_t j : it->second) {
              if (j <= i) continue;
              // The neighbour must really be in that cell (hash collisions).
              if (cx[j * 3] != cx[i * 3] + dx || cx[j * 3 + 1] != cx[i * 3 + 1] + dy || cx[j * 3 + 2] != cx[i * 3 + 2] + dz) continue;
              const double a = double(p[i * 3]) - p[j * 3], b = double(p[i * 3 + 1]) - p[j * 3 + 1], c = double(p[i * 3 + 2]) - p[j * 3 + 2];
              if (a * a + b * b + c * c < d2) o.emplace_back(uint32_t(i), j);
            }
          }
    }
  });
  for (auto& o : parts) out.insert(out.end(), o.begin(), o.end());
  return out;
}

// ---------------------------------------------------------------- strands2d.py
namespace {
struct S2 { std::vector<double> x, w; std::vector<int> y; int gap = 0; };

void row_runs(const uint8_t* alpha, int stride, int w, int threshold, std::vector<int>& s, std::vector<int>& e) {
  s.clear(); e.clear();
  bool in = false;
  for (int x = 0; x < w; ++x) {
    const bool c = alpha[size_t(x) * stride] >= threshold;
    if (c && !in) s.push_back(x);
    if (!c && in) e.push_back(x - 1);
    in = c;
  }
  if (in) e.push_back(w - 1);
}

double typical_width(const Mask& m, int x0, int y0, int tw, int th, int threshold) {
  std::vector<double> widths;
  std::vector<int> s, e;
  const int step = std::max(1, th / 64);
  for (int y = 0; y < th; y += step) {
    row_runs(&m.rgba[(size_t(y0 + y) * m.w + x0) * 4 + 3], 4, tw, threshold, s, e);
    for (size_t k = 0; k < s.size(); ++k) widths.push_back(double(e[k] - s[k] + 1));
  }
  return widths.empty() ? 1.0 : median(widths);
}

double predict(const S2& s, int history = 4) {
  const size_t n = s.x.size();
  if (n < 2) return s.x.back();
  const size_t k = std::min<size_t>(history, n - 1);
  const double slope = (s.x[n - 1] - s.x[n - 1 - k]) / double(std::max(s.y[n - 1] - s.y[n - 1 - k], 1));
  return s.x[n - 1] + slope * (1 + s.gap);
}

std::vector<S2> stitch(std::vector<S2>& strands, int max_gap = 24, double max_dx = 3.0, int history = 8) {
  const size_t n = strands.size();
  std::vector<int> order(n), ends(n);
  std::iota(order.begin(), order.end(), 0);
  std::iota(ends.begin(), ends.end(), 0);
  std::stable_sort(order.begin(), order.end(), [&](int a, int b) { return strands[a].y.front() < strands[b].y.front(); });
  std::stable_sort(ends.begin(), ends.end(), [&](int a, int b) { return strands[a].y.back() < strands[b].y.back(); });
  std::vector<int> start_y(n);
  for (size_t k = 0; k < n; ++k) start_y[k] = strands[order[k]].y.front();
  std::vector<char> used(n, 0);
  std::vector<int> nxt(n, -1);
  for (int i : ends) {
    const S2& s = strands[i];
    const size_t m = s.x.size();
    const size_t k = std::min<size_t>(history, m - 1);
    const double slope = k ? (s.x[m - 1] - s.x[m - 1 - k]) / double(std::max(s.y[m - 1] - s.y[m - 1 - k], 1)) : 0.0;
    const size_t lo = std::lower_bound(start_y.begin(), start_y.end(), s.y.back() + 1) - start_y.begin();
    const size_t hi = std::lower_bound(start_y.begin(), start_y.end(), s.y.back() + max_gap + 1) - start_y.begin();
    int best = -1;
    double best_d = max_dx;
    for (size_t q = lo; q < hi; ++q) {
      const int j = order[q];
      if (j == i || used[j]) continue;
      const double d = std::fabs(strands[j].x.front() - (s.x.back() + slope * (strands[j].y.front() - s.y.back())));
      if (d < best_d) { best = j; best_d = d; }
    }
    if (best >= 0) { nxt[i] = best; used[best] = 1; }
  }
  std::vector<S2> out;
  for (int i : order) {
    if (used[i]) continue;
    S2 chain = strands[i];
    for (int j = nxt[i]; j >= 0; j = nxt[j]) {
      chain.x.insert(chain.x.end(), strands[j].x.begin(), strands[j].x.end());
      chain.y.insert(chain.y.end(), strands[j].y.begin(), strands[j].y.end());
      chain.w.insert(chain.w.end(), strands[j].w.begin(), strands[j].w.end());
    }
    out.push_back(std::move(chain));
  }
  return out;
}

// strands2d.trace on one tile.
std::vector<S2> trace_tile(const Mask& m, int x0, int y0, int tw, int th, int threshold, double* typical_out,
                           double max_dx = 2.5, int max_gap = 3, size_t min_rows = 12) {
  const double typical = typical_width(m, x0, y0, tw, th, threshold);
  *typical_out = typical;
  std::vector<S2> active, done;
  std::vector<int> rs, re;
  std::vector<double> cx, cw, last;
  std::vector<char> taken, usedv;
  struct Cand { double d; size_t flat; };
  std::vector<Cand> cand;
  for (int y = 0; y < th; ++y) {
    row_runs(&m.rgba[(size_t(y0 + y) * m.w + x0) * 4 + 3], 4, tw, threshold, rs, re);
    cx.clear(); cw.clear();
    for (size_t k = 0; k < rs.size(); ++k) {
      const int width = re[k] - rs[k] + 1;
      const int count = width > 1.5 * typical ? std::max(1, int(std::nearbyint(width / typical))) : 1;
      const double part = double(width) / count;
      for (int q = 0; q < count; ++q) { cx.push_back(rs[k] - 0.5 + part * (q + 0.5)); cw.push_back(part); }
    }
    taken.assign(cx.size(), 0);
    if (!active.empty() && !cx.empty()) {
      last.resize(active.size());
      for (size_t i = 0; i < active.size(); ++i) last[i] = predict(active[i]);
      // argsort(dist) over the flat matrix, cut at max_dx: only pairs within max_dx can be taken. Ties by flat index.
      cand.clear();
      for (size_t i = 0; i < active.size(); ++i) {
        const double l = last[i];
        const auto lo = std::lower_bound(cx.begin(), cx.end(), l - max_dx - 1e-9) - cx.begin();
        for (size_t j = size_t(lo); j < cx.size() && cx[j] <= l + max_dx + 1e-9; ++j) {
          const double d = std::fabs(l - cx[j]);
          if (d <= max_dx) cand.push_back({d, i * cx.size() + j});
        }
      }
      std::sort(cand.begin(), cand.end(), [](const Cand& a, const Cand& b) { return a.d < b.d || (a.d == b.d && a.flat < b.flat); });
      usedv.assign(active.size(), 0);
      for (const Cand& c : cand) {
        const size_t i = c.flat / cx.size(), j = c.flat % cx.size();
        if (usedv[i] || taken[j]) continue;
        usedv[i] = taken[j] = 1;
        S2& s = active[i];
        s.x.push_back(cx[j]); s.y.push_back(y); s.w.push_back(cw[j]); s.gap = 0;
      }
    }
    std::vector<S2> survivors;
    survivors.reserve(active.size() + cx.size());
    for (S2& s : active) {
      if (s.y.back() != y) {
        if (++s.gap > max_gap) { done.push_back(std::move(s)); continue; }
      }
      survivors.push_back(std::move(s));
    }
    active.swap(survivors);
    for (size_t j = 0; j < cx.size(); ++j)
      if (!taken[j]) { S2 s; s.x.push_back(cx[j]); s.y.push_back(y); s.w.push_back(cw[j]); active.push_back(std::move(s)); }
  }
  for (S2& s : active) done.push_back(std::move(s));
  std::vector<S2> st = stitch(done), out;
  for (S2& s : st)
    if (s.y.size() >= min_rows) out.push_back(std::move(s));
  return out;
}

// np.gradient(x, y) with edge_order 1.
void gradient(const std::vector<double>& f, const std::vector<double>& x, std::vector<double>& out) {
  const size_t n = f.size();
  out.assign(n, 0.0);
  if (n < 2) return;
  out[0] = (f[1] - f[0]) / (x[1] - x[0]);
  out[n - 1] = (f[n - 1] - f[n - 2]) / (x[n - 1] - x[n - 2]);
  for (size_t i = 1; i + 1 < n; ++i) {
    const double dx1 = x[i] - x[i - 1], dx2 = x[i + 1] - x[i];
    const double a = -(dx2) / (dx1 * (dx1 + dx2)), b = (dx2 - dx1) / (dx1 * dx2), c = dx1 / (dx2 * (dx1 + dx2));
    out[i] = a * f[i - 1] + b * f[i] + c * f[i + 1];
  }
}
}  // namespace

void tile_strands(const Mask& mask, const int grid[2], int step, int threshold, std::vector<std::vector<Row>>& per_tile,
                  std::map<std::pair<int, int>, TileStat>& stats, int threads) {
  const int gx = grid[0], gy = grid[1], tw = mask.w / gx, th = mask.h / gy;
  per_tile.assign(size_t(gx) * gy, {});
  std::vector<TileStat> st(size_t(gx) * gy);
  parallel(gx * gy, threads, [&](int t) {
    const int tx = t % gx, ty = t / gx;
    double typical = 0;
    std::vector<S2> traced = trace_tile(mask, tx * tw, ty * th, tw, th, threshold, &typical);
    st[t] = {int(traced.size()), typical, 0.0};
    auto& rows = per_tile[t];
    rows.reserve(traced.size());
    std::vector<double> y, slope;
    for (const S2& s : traced) {
      // strands2d.resample: every `step` rows from root to tip and the tip.
      std::vector<size_t> keep;
      const double y0 = s.y.front(), y1 = s.y.back();
      const size_t nsteps = y1 > y0 ? size_t(std::ceil((y1 - y0) / step)) : 0;
      for (size_t k = 0; k < nsteps; ++k) {
        const double target = y0 + double(k) * step;
        keep.push_back(size_t(std::lower_bound(s.y.begin(), s.y.end(), target, [](int a, double b) { return a < b; }) - s.y.begin()));
      }
      keep.push_back(s.y.size() - 1);
      std::sort(keep.begin(), keep.end());
      keep.erase(std::unique(keep.begin(), keep.end()), keep.end());
      Row r;
      std::vector<double> xs;
      y.clear();
      for (size_t k : keep) { xs.push_back(s.x[k]); y.push_back(s.y[k]); r.wd.push_back(s.w[k] / mask.w); }
      if (xs.size() > 1) gradient(xs, y, slope);
      else slope.assign(xs.size(), 0.0);
      for (size_t k = 0; k < xs.size(); ++k) {
        const int xi = int(std::clamp(std::nearbyint(xs[k]), 0.0, double(tw - 1))), yi = int(y[k]);
        r.u.push_back((tx * tw + xs[k] + 0.5) / mask.w);
        r.v.push_back((ty * th + y[k] + 0.5) / mask.h);
        r.slope.push_back(slope[k]);
        r.root.push_back(mask.at(tx * tw + xi, ty * th + yi, 2) / 255.0);
        r.depth.push_back(mask.at(tx * tw + xi, ty * th + yi, 1) / 255.0);
      }
      rows.push_back(std::move(r));
    }
  });
  for (int t = 0; t < gx * gy; ++t) stats[{t % gx, t / gx}] = st[t];
}

// ---------------------------------------------------------------- convert.py
namespace {
struct Locate { std::vector<int> hit; std::vector<double> bary; std::vector<int> inside; };

// convert.locate: the first triangle (card order) containing each point (barycentrics >= -EPS), and how many contain
// it strictly. Triangle edges and determinant in float32 (numpy: float32 UV), the rest float64.
void locate(const Mesh& m, const std::vector<uint32_t>& tris, const std::vector<double>& pu, const std::vector<double>& pv, Locate& L) {
  const size_t n = pu.size(), T = tris.size() / 3;
  L.hit.assign(n, -1);
  L.bary.assign(n * 3, 0.0);
  L.inside.assign(n, 0);
  std::vector<uint32_t> order(n);
  std::iota(order.begin(), order.end(), 0u);
  std::stable_sort(order.begin(), order.end(), [&](uint32_t a, uint32_t b) { return pv[a] < pv[b]; });
  std::vector<double> sv(n);
  for (size_t k = 0; k < n; ++k) sv[k] = pv[order[k]];
  for (size_t t = 0; t < T; ++t) {
    const float* a = &m.uv[tris[t * 3] * 2];
    const float* b = &m.uv[tris[t * 3 + 1] * 2];
    const float* c = &m.uv[tris[t * 3 + 2] * 2];
    const float vmin = std::min({a[1], b[1], c[1]}), vmax = std::max({a[1], b[1], c[1]});
    const size_t lo = std::lower_bound(sv.begin(), sv.end(), double(vmin) - kEps) - sv.begin();
    const size_t hi = std::lower_bound(sv.begin(), sv.end(), double(vmax) + kEps) - sv.begin();
    const float e0x = b[0] - a[0], e0y = b[1] - a[1], e1x = c[0] - a[0], e1y = c[1] - a[1];
    float detf = e0x * e1y - e0y * e1x;
    const double det = std::fabs(double(detf)) < 1e-12 ? NAN : double(detf);
    for (size_t q = lo; q < hi; ++q) {
      const uint32_t i = order[q];
      const double dx = pu[i] - double(a[0]), dy = pv[i] - double(a[1]);
      const double l1 = (dx * double(e1y) - dy * double(e1x)) / det;
      const double l2 = (double(e0x) * dy - double(e0y) * dx) / det;
      const double l0 = 1 - l1 - l2;
      if (l0 > kEps && l1 > kEps && l2 > kEps) ++L.inside[i];
      if (l0 >= -kEps && l1 >= -kEps && l2 >= -kEps && (L.hit[i] < 0 || int(t) < L.hit[i])) {
        L.hit[i] = int(t);
        L.bary[i * 3] = l0; L.bary[i * 3 + 1] = l1; L.bary[i * 3 + 2] = l2;
      }
    }
  }
}

// dP/du and dP/dv of a triangle (convert.jacobian_u / jacobian_v).
void jacobians(const Mesh& m, const uint32_t* tv, double ju[3], double jv[3]) {
  double p[3][3], t[3][2];
  for (int k = 0; k < 3; ++k) {
    for (int a = 0; a < 3; ++a) p[k][a] = m.pos[tv[k] * 3 + a];
    for (int a = 0; a < 2; ++a) t[k][a] = m.uv[tv[k] * 2 + a];
  }
  const double du1[2] = {t[1][0] - t[0][0], t[1][1] - t[0][1]}, du2[2] = {t[2][0] - t[0][0], t[2][1] - t[0][1]};
  double det = du1[0] * du2[1] - du1[1] * du2[0];
  if (std::fabs(det) < 1e-12) det = NAN;
  for (int a = 0; a < 3; ++a) {
    const double dp1 = p[1][a] - p[0][a], dp2 = p[2][a] - p[0][a];
    ju[a] = (dp1 * du2[1] - dp2 * du1[1]) / det;
    jv[a] = (dp2 * du1[0] - dp1 * du2[0]) / det;
  }
}
inline double norm3(const double v[3]) { return std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]); }

struct CardOut {
  std::vector<float> points, radii, root, bary;
  std::vector<uint32_t> triangle, lengths;
  int strands_without_width = 0;
  long long samples = 0, unmapped = 0, overlap = 0;
  bool spanning = false, mixed = false;
};

// convert.run_radii + the per-card output.
void runs(const Locate& L, const std::vector<int>& sid, const std::vector<double>& width, const std::vector<double>& rp,
          const std::vector<double>& px, const std::vector<uint32_t>& tri_ids, CardOut& o) {
  const size_t n = sid.size();
  size_t a = 0;
  std::vector<double> r, meas;
  while (a < n) {
    size_t b = a + 1;
    const bool ok = L.hit[a] >= 0;
    while (b < n && sid[b] == sid[a] && (L.hit[b] >= 0) == ok) ++b;
    if (ok && b - a >= 2) {
      r.clear(); meas.clear();
      for (size_t i = a; i < b; ++i) {
        const double x = width[i] / 2;
        r.push_back(x);
        if (std::isfinite(x) && x > 0) meas.push_back(x);
      }
      if (meas.empty()) {
        ++o.strands_without_width;
      } else {
        // The median as the reference computes it (sorted, mean of the two middles when even).
        std::sort(meas.begin(), meas.end());
        const size_t c = meas.size();
        const double med = c % 2 ? meas[c / 2] : (meas[c / 2 - 1] + meas[c / 2]) / 2;
        for (size_t i = a; i < b; ++i) {
          double x = r[i - a];
          if (!(std::isfinite(x) && x > 0)) x = med;
          x = std::min(std::min(x, 3 * med), kMaxRadius);
          o.radii.push_back(float(x));
          for (int k = 0; k < 3; ++k) o.points.push_back(float(px[i * 3 + k]));
          o.root.push_back(float(rp[i]));
          o.triangle.push_back(tri_ids[size_t(L.hit[i])]);
          for (int k = 0; k < 3; ++k) o.bary.push_back(float(L.bary[i * 3 + k]));
        }
        o.lengths.push_back(uint32_t(b - a));
      }
    }
    a = b;
  }
}
}  // namespace

Groom convert(const Mesh& m, const Mask& mask, const int grid[2], Report& rep, int threads) {
  std::vector<int32_t> card_of;
  const int count = cards(m, card_of);
  rep = {};
  rep.cards = count;
  rep.triangles = int(m.triangles());
  double corr = 0;
  const bool curly = root_row_correlation(mask, grid, 128, &corr) && corr < kCurlyRootCorr;
  std::vector<std::vector<Row>> per_tile;
  std::vector<Row> curve_rows;
  std::vector<double> box_lo, box_hi;
  if (curly) {
    curve_strands(mask, grid, 16, 128, curve_rows, box_lo, box_hi, rep.tiles, rep.strand_root_corr, threads);
    rep.tracer = "2d";
  } else {
    tile_strands(mask, grid, 16, 128, per_tile, rep.tiles, threads);
  }
  const int W = mask.w, H = mask.h;
  std::vector<std::vector<uint32_t>> card_tris(count);
  for (uint32_t t = 0; t < m.triangles(); ++t) card_tris[card_of[t]].push_back(t);
  std::vector<CardOut> outs(count);
  parallel(count, threads, [&](int c) {
    CardOut& o = outs[c];
    const std::vector<uint32_t>& tri_ids = card_tris[c];
    if (tri_ids.empty()) return;
    std::vector<uint32_t> tris;
    tris.reserve(tri_ids.size() * 3);
    float lo[2] = {INFINITY, INFINITY}, hi[2] = {-INFINITY, -INFINITY};
    int pos_sign = 0, neg_sign = 0;
    for (uint32_t t : tri_ids) {
      for (int k = 0; k < 3; ++k) {
        const uint32_t v = m.tris[t * 3 + k];
        tris.push_back(v);
        for (int a = 0; a < 2; ++a) { lo[a] = std::min(lo[a], m.uv[v * 2 + a]); hi[a] = std::max(hi[a], m.uv[v * 2 + a]); }
      }
      const float* a = &m.uv[m.tris[t * 3] * 2];
      const float* b = &m.uv[m.tris[t * 3 + 1] * 2];
      const float* cc = &m.uv[m.tris[t * 3 + 2] * 2];
      const float s = (b[0] - a[0]) * (cc[1] - a[1]) - (b[1] - a[1]) * (cc[0] - a[0]);
      pos_sign += s > 0; neg_sign += s < 0;
    }
    int t0[2], t1[2];
    for (int a = 0; a < 2; ++a) {
      t0[a] = int(std::clamp(std::floor(double(lo[a]) * grid[a] + kEps), 0.0, double(grid[a] - 1)));
      t1[a] = int(std::clamp(std::floor(double(hi[a]) * grid[a] - kEps), 0.0, double(grid[a] - 1)));
    }
    o.spanning = t0[0] != t1[0] || t0[1] != t1[1];
    o.mixed = pos_sign && neg_sign;
    std::vector<const Row*> rows;
    if (curly) {
      for (size_t i = 0; i < curve_rows.size(); ++i)
        if (box_lo[i * 2] <= hi[0] && box_lo[i * 2 + 1] <= hi[1] && box_hi[i * 2] >= lo[0] && box_hi[i * 2 + 1] >= lo[1])
          rows.push_back(&curve_rows[i]);
    } else {
      for (int tx = t0[0]; tx <= t1[0]; ++tx)
        for (int ty = t0[1]; ty <= t1[1]; ++ty)
          for (const Row& r : per_tile[size_t(ty) * grid[0] + tx]) rows.push_back(&r);
    }
    if (rows.empty()) return;
    std::vector<double> u, v, wd, slope, rp, nx, ny;
    std::vector<int> sid;
    for (size_t k = 0; k < rows.size(); ++k) {
      const Row& r = *rows[k];
      if (!curly) {
        u.insert(u.end(), r.u.begin(), r.u.end()); v.insert(v.end(), r.v.begin(), r.v.end());
        wd.insert(wd.end(), r.wd.begin(), r.wd.end()); slope.insert(slope.end(), r.slope.begin(), r.slope.end());
        rp.insert(rp.end(), r.root.begin(), r.root.end());
        sid.insert(sid.end(), r.u.size(), int(k));
      } else {
        // Samples inside this card's UV box; each inside stretch a strand of its own.
        bool prev = false;
        for (size_t q = 0; q < r.u.size(); ++q) {
          const bool inb = r.u[q] >= lo[0] - kEps && r.u[q] <= hi[0] + kEps && r.v[q] >= lo[1] - kEps && r.v[q] <= hi[1] + kEps;
          if (!inb) { prev = false; continue; }
          if (!prev) sid.push_back(sid.empty() ? 0 : sid.back() + 1);
          else sid.push_back(sid.back());
          prev = true;
          u.push_back(r.u[q]); v.push_back(r.v[q]); wd.push_back(r.wd[q]); slope.push_back(r.slope[q]);
          rp.push_back(r.root[q]); nx.push_back(r.nx[q]); ny.push_back(r.ny[q]);
        }
      }
    }
    if (u.empty()) return;
    Locate L;
    locate(m, tris, u, v, L);
    o.samples = (long long)u.size();
    for (size_t i = 0; i < u.size(); ++i) { o.unmapped += L.hit[i] < 0; o.overlap += L.inside[i] > 1; }
    // Jacobians per card triangle, then the world width per sample and the point.
    const size_t T = tri_ids.size();
    std::vector<double> ju(T * 3), jv(T * 3), scale(T);
    for (size_t t = 0; t < T; ++t) { jacobians(m, &tris[t * 3], &ju[t * 3], &jv[t * 3]); scale[t] = norm3(&ju[t * 3]); }
    std::vector<double> width(u.size()), px(u.size() * 3);
    for (size_t i = 0; i < u.size(); ++i) {
      const size_t t = size_t(std::max(L.hit[i], 0));
      if (!curly) {
        const double cosv = 1 / std::sqrt(1 + slope[i] * slope[i]);
        width[i] = wd[i] * cosv * scale[t];
      } else {
        const double across = wd[i] * W, w[3] = {ju[t * 3] * (across * nx[i] / W) + jv[t * 3] * (across * ny[i] / H),
                                                 ju[t * 3 + 1] * (across * nx[i] / W) + jv[t * 3 + 1] * (across * ny[i] / H),
                                                 ju[t * 3 + 2] * (across * nx[i] / W) + jv[t * 3 + 2] * (across * ny[i] / H)};
        width[i] = norm3(w);
      }
      for (int a = 0; a < 3; ++a) {
        double s = 0;
        for (int k = 0; k < 3; ++k) s += L.bary[i * 3 + k] * double(m.pos[tris[t * 3 + k] * 3 + a]);
        px[i * 3 + a] = s;
      }
    }
    runs(L, sid, width, rp, px, tri_ids, o);
  });
  Groom g;
  g.offsets.push_back(0);
  for (int c = 0; c < count; ++c) {
    const CardOut& o = outs[c];
    rep.samples += o.samples; rep.unmapped_samples += o.unmapped; rep.uv_overlap_samples += o.overlap;
    rep.cards_spanning_tiles += o.spanning; rep.mixed_winding_cards += o.mixed;
    rep.strands_without_width += o.strands_without_width;
    g.points.insert(g.points.end(), o.points.begin(), o.points.end());
    g.radii.insert(g.radii.end(), o.radii.begin(), o.radii.end());
    g.root.insert(g.root.end(), o.root.begin(), o.root.end());
    g.triangle.insert(g.triangle.end(), o.triangle.begin(), o.triangle.end());
    g.bary.insert(g.bary.end(), o.bary.begin(), o.bary.end());
    for (uint32_t len : o.lengths) { g.offsets.push_back(g.offsets.back() + len); g.card.push_back(c); }
  }
  return g;
}

namespace {
Groom select(const Groom& g, const std::vector<char>& keep) {
  Groom o;
  o.offsets.push_back(0);
  for (uint32_t s = 0; s < g.strands(); ++s) {
    if (!keep[s]) continue;
    const uint32_t a = g.offsets[s], b = g.offsets[s + 1];
    o.points.insert(o.points.end(), g.points.begin() + size_t(a) * 3, g.points.begin() + size_t(b) * 3);
    o.radii.insert(o.radii.end(), g.radii.begin() + a, g.radii.begin() + b);
    o.root.insert(o.root.end(), g.root.begin() + a, g.root.begin() + b);
    o.triangle.insert(o.triangle.end(), g.triangle.begin() + a, g.triangle.begin() + b);
    o.bary.insert(o.bary.end(), g.bary.begin() + size_t(a) * 3, g.bary.begin() + size_t(b) * 3);
    o.offsets.push_back(o.offsets.back() + (b - a));
    o.card.push_back(g.card[s]);
  }
  return o;
}
}  // namespace

Groom dedupe(const Groom& g, int* dropped, int threads, double distance, double share) {
  const uint32_t P = g.points_count(), S = g.strands();
  std::vector<uint32_t> strand_of(P);
  for (uint32_t s = 0; s < S; ++s)
    for (uint32_t i = g.offsets[s]; i < g.offsets[s + 1]; ++i) strand_of[i] = s;
  auto pairs = pairs_within(g.points.data(), P, distance, threads);
  // Distinct (point, neighbouring strand of another card) keys, both directions.
  std::vector<uint64_t> keys;
  keys.reserve(pairs.size() * 2);
  for (auto [i, j] : pairs) {
    if (g.card[strand_of[i]] == g.card[strand_of[j]]) continue;
    keys.push_back(uint64_t(i) * S + strand_of[j]);
    keys.push_back(uint64_t(j) * S + strand_of[i]);
  }
  std::sort(keys.begin(), keys.end());
  keys.erase(std::unique(keys.begin(), keys.end()), keys.end());
  // Count per (strand of the point, other strand).
  std::vector<uint64_t> pk;
  pk.reserve(keys.size());
  for (uint64_t k : keys) pk.push_back(uint64_t(strand_of[k / S]) * S + k % S);
  std::sort(pk.begin(), pk.end());
  std::vector<char> keep(S, 1);
  for (size_t a = 0; a < pk.size();) {
    size_t b = a;
    while (b < pk.size() && pk[b] == pk[a]) ++b;
    const uint64_t s = pk[a] / S, other = pk[a] % S;
    const double len = double(g.offsets[s + 1] - g.offsets[s]);
    if (double(b - a) > share * len && keep[s] && keep[other] && other < s) keep[s] = 0;
    a = b;
  }
  int d = 0;
  for (char k : keep) d += !k;
  if (dropped) *dropped = d;
  return select(g, keep);
}

Groom thin(const Groom& g, float keep) {
  if (keep >= 1) return g;
  std::vector<char> sel(g.strands());
  for (uint32_t s = 0; s < g.strands(); ++s) {
    const uint64_t h = (uint64_t(s) * 0x9E3779B9ull + 0x632BE59BD9B4E019ull) & 0xFFFFFFFFull;
    sel[s] = double(h) / 4294967296.0 < double(keep);
  }
  Groom o = select(g, sel);
  for (float& r : o.radii) r = std::min(r / keep, kMaxThinRadius);
  return o;
}

// ---------------------------------------------------------------- analysis.py
std::vector<double> root_channel_check(const Mask& mask, const int grid[2], int threshold) {
  const int tw = mask.w / grid[0], th = mask.h / grid[1];
  std::vector<double> out;
  for (int ty = 0; ty < grid[1]; ++ty)
    for (int tx = 0; tx < grid[0]; ++tx) {
      long long n = 0;
      double sy = 0, sb = 0;
      for (int y = 0; y < th; ++y)
        for (int x = 0; x < tw; ++x)
          if (mask.at(tx * tw + x, ty * th + y, 3) >= threshold) { ++n; sy += y; sb += mask.at(tx * tw + x, ty * th + y, 2); }
      if (n <= 10) continue;
      const double my = sy / n, mb = sb / n;
      double cyy = 0, cbb = 0, cyb = 0;
      for (int y = 0; y < th; ++y)
        for (int x = 0; x < tw; ++x)
          if (mask.at(tx * tw + x, ty * th + y, 3) >= threshold) {
            const double a = y - my, b = mask.at(tx * tw + x, ty * th + y, 2) - mb;
            cyy += a * a; cbb += b * b; cyb += a * b;
          }
      const double c = cyb / std::sqrt(cyy) / std::sqrt(cbb);
      out.push_back(std::isfinite(c) ? std::clamp(c, -1.0, 1.0) : NAN);
    }
  return out;
}

namespace {
void flags(const Groom& g, const double c[3], double margin, std::vector<char>& rev, std::vector<char>& dec) {
  const uint32_t S = g.strands();
  rev.assign(S, 0); dec.assign(S, 0);
  for (uint32_t s = 0; s < S; ++s) {
    const float* a = &g.points[size_t(g.offsets[s]) * 3];
    const float* b = &g.points[size_t(g.offsets[s + 1] - 1) * 3];
    double dr = 0, dt = 0, ch = 0;
    for (int k = 0; k < 3; ++k) {
      dr += (a[k] - c[k]) * (a[k] - c[k]); dt += (b[k] - c[k]) * (b[k] - c[k]);
      const double e = double(b[k]) - a[k];
      ch += e * e;
    }
    dr = std::sqrt(dr); dt = std::sqrt(dt); ch = std::sqrt(ch);
    rev[s] = dt < dr - margin;
    dec[s] = std::fabs(dt - dr) > std::max(margin, ch / std::sqrt(2.0));
  }
}
}  // namespace

Orientation orientation(const Groom& g, const double centre[3], double margin) {
  std::vector<char> rev, dec;
  flags(g, centre, margin, rev, dec);
  Orientation o;
  std::map<int, std::pair<int, int>> per_card;   // reversed, decided
  for (uint32_t s = 0; s < g.strands(); ++s) {
    o.strands_reversed += rev[s];
    o.strands_decided += dec[s];
    if (dec[s]) { auto& pc = per_card[g.card[s]]; pc.first += rev[s]; pc.second += 1; }
  }
  o.cards_decided = int(per_card.size());
  for (auto& [c, v] : per_card) o.cards_mostly_reversed += double(v.first) / v.second > 0.5;
  return o;
}

Groom reorient(const Groom& g, const double centre[3], int* flipped) {
  std::vector<char> rev, dec;
  flags(g, centre, 0.005, rev, dec);
  Groom o = g;
  int f = 0;
  for (uint32_t s = 0; s < g.strands(); ++s) {
    const uint32_t a = g.offsets[s], b = g.offsets[s + 1];
    if (rev[s] && dec[s]) {
      ++f;
      for (uint32_t i = a; i < b; ++i) {
        const uint32_t j = a + b - 1 - i;
        for (int k = 0; k < 3; ++k) { o.points[i * 3 + k] = g.points[j * 3 + k]; o.bary[i * 3 + k] = g.bary[j * 3 + k]; }
        o.radii[i] = g.radii[j]; o.triangle[i] = g.triangle[j];
      }
    }
    // Arc-length root parameters (evenly spaced for a collapsed strand).
    double total = 0;
    std::vector<double> arc(b - a, 0.0);
    for (uint32_t i = a + 1; i < b; ++i) {
      double d = 0;
      for (int k = 0; k < 3; ++k) { const double e = double(o.points[i * 3 + k]) - o.points[(i - 1) * 3 + k]; d += e * e; }
      total += std::sqrt(d);
      arc[i - a] = total;
    }
    for (uint32_t i = a; i < b; ++i)
      o.root[i] = float(total > 0 ? arc[i - a] / total : double(i - a) / double(b - a - 1));
  }
  if (flipped) *flipped = f;
  return o;
}

Head head_from_roots(const Groom& g) {
  Head h;
  std::vector<double> r;
  for (uint32_t s = 0; s < g.strands(); ++s) {
    const uint32_t a = g.offsets[s];
    if (g.root[a] < 0.2) for (int k = 0; k < 3; ++k) r.push_back(g.points[size_t(a) * 3 + k]);
  }
  const size_t n = r.size() / 3;
  if (n < 16) return h;
  // Least squares sphere: [2x 2y 2z 1] s = x^2+y^2+z^2, normal equations (4x4).
  double A[4][4] = {}, B[4] = {};
  for (size_t i = 0; i < n; ++i) {
    const double row[4] = {2 * r[i * 3], 2 * r[i * 3 + 1], 2 * r[i * 3 + 2], 1.0};
    const double rhs = r[i * 3] * r[i * 3] + r[i * 3 + 1] * r[i * 3 + 1] + r[i * 3 + 2] * r[i * 3 + 2];
    for (int p = 0; p < 4; ++p) { B[p] += row[p] * rhs; for (int q = 0; q < 4; ++q) A[p][q] += row[p] * row[q]; }
  }
  for (int p = 0; p < 4; ++p) {   // Gaussian elimination with partial pivoting
    int piv = p;
    for (int q = p + 1; q < 4; ++q) if (std::fabs(A[q][p]) > std::fabs(A[piv][p])) piv = q;
    std::swap(A[p], A[piv]); std::swap(B[p], B[piv]);
    if (std::fabs(A[p][p]) < 1e-30) return h;
    for (int q = p + 1; q < 4; ++q) {
      const double f = A[q][p] / A[p][p];
      for (int k = p; k < 4; ++k) A[q][k] -= f * A[p][k];
      B[q] -= f * B[p];
    }
  }
  double x[4];
  for (int p = 3; p >= 0; --p) {
    double s = B[p];
    for (int k = p + 1; k < 4; ++k) s -= A[p][k] * x[k];
    x[p] = s / A[p][p];
  }
  const double rr = std::sqrt(std::max(x[3] + x[0] * x[0] + x[1] * x[1] + x[2] * x[2], 1e-6));
  for (int k = 0; k < 3; ++k) { h.centre[k] = x[k]; h.radii[k] = rr; }
  return h;
}

}  // namespace hairconv
