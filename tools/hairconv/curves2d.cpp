// curves2d: the 2-D curve tracer of curly hair atlases (port of tools/hair_groom/curves2d.py) and
// convert.curve_strands without its memo. Same arithmetic order as the reference: float where numpy works in float32
// (blur, orientation field, the ID channel and its medians), double elsewhere, round half to even (std::nearbyint)
// where numpy uses rint / round, numpy's pairwise summation where a sum decides a discrete outcome.
#include "hairconv.h"

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstring>
#include <limits>

namespace hairconv {
namespace {

constexpr double kEdgeTol = 1.5;
constexpr int kChord = 6;
constexpr int kBlocks[2] = {8, 8};  // convert.CURVE_BLOCKS
constexpr double kPi = 3.141592653589793;

// numpy's pairwise sum (add.reduce over a contiguous axis).
template <class T>
T pairwise_sum(const T* a, size_t n) {
  if (n < 8) {
    T r = T(0);
    for (size_t i = 0; i < n; ++i) r += a[i];
    return r;
  }
  if (n <= 128) {
    T r[8];
    for (int j = 0; j < 8; ++j) r[j] = a[j];
    size_t i = 8;
    for (; i < n - (n % 8); i += 8)
      for (int j = 0; j < 8; ++j) r[j] += a[i + j];
    T res = ((r[0] + r[1]) + (r[2] + r[3])) + ((r[4] + r[5]) + (r[6] + r[7]));
    for (; i < n; ++i) res += a[i];
    return res;
  }
  size_t n2 = n / 2;
  n2 -= n2 % 8;
  return pairwise_sum(a, n2) + pairwise_sum(a + n2, n - n2);
}

// np.median of float64 values (sorts `v`).
double median_d(std::vector<double>& v) {
  size_t n = v.size();
  if (!n) return std::numeric_limits<double>::quiet_NaN();
  for (double x : v)
    if (std::isnan(x)) return x;
  std::sort(v.begin(), v.end());
  return n % 2 ? v[n / 2] : (v[n / 2 - 1] + v[n / 2]) / 2.0;
}

// np.median of a float32 array: the mean of the two middle values in float32.
float median_f(std::vector<float>& v) {
  size_t n = v.size();
  std::sort(v.begin(), v.end());
  return n % 2 ? v[n / 2] : (v[n / 2 - 1] + v[n / 2]) / 2.0f;
}

// curves2d._median of float32 values: the two middle values added in float32, halved in double.
double median_py_f(float* v, size_t n) {
  std::sort(v, v + n);
  return n % 2 ? double(v[n / 2]) : 0.5 * double(float(v[n / 2 - 1] + v[n / 2]));
}
double median_py_d(double* v, size_t n) {
  std::sort(v, v + n);
  return n % 2 ? v[n / 2] : 0.5 * (v[n / 2 - 1] + v[n / 2]);
}

// np.corrcoef(x, y)[0, 1].
double corrcoef(const std::vector<double>& x, const std::vector<double>& y) {
  size_t n = x.size();
  double mx = pairwise_sum(x.data(), n) / double(n), my = pairwise_sum(y.data(), n) / double(n);
  double sxx = 0, syy = 0, sxy = 0;
  for (size_t i = 0; i < n; ++i) {
    double a = x[i] - mx, b = y[i] - my;
    sxx += a * a;
    syy += b * b;
    sxy += a * b;
  }
  double inv = 1.0 / double(n - 1);
  double c = ((sxy * inv) / std::sqrt(sxx * inv)) / std::sqrt(syy * inv);
  if (std::isnan(c)) return c;
  return std::min(std::max(c, -1.0), 1.0);
}

double py_mod(double a, double b) {
  double m = std::fmod(a, b);
  if (m != 0 && ((b < 0) != (m < 0))) m += b;
  return m;
}

// strands2d.row_runs on a 0/1 array: inclusive (start, end) pairs.
int row_runs(const uint8_t* c, int n, int* s, int* e) {
  int k = 0;
  for (int i = 0; i < n; ++i) {
    if (c[i] && (i == 0 || !c[i - 1])) s[k] = i;
    if (c[i] && (i == n - 1 || !c[i + 1])) e[k++] = i;
  }
  return k;
}

// strands2d.typical_width of the block [x0, x1) x [y0, y1).
float typical_width(const Mask& m, int x0, int y0, int x1, int y1, int threshold) {
  int bh = y1 - y0, stride = std::max(1, bh / 64);
  std::vector<double> widths;
  for (int y = y0; y < y1; y += stride) {
    int start = -1;
    for (int x = x0; x <= x1; ++x) {
      bool c = x < x1 && m.at(x, y, 3) >= threshold;
      if (c && start < 0) start = x;
      if (!c && start >= 0) {
        widths.push_back(double(x - start));
        start = -1;
      }
    }
  }
  return widths.empty() ? 1.0f : float(median_d(widths));
}

// curves2d.typical_map with tile_grid = CURVE_BLOCKS.
void typical_map(const Mask& m, int threshold, std::vector<float>& out) {
  int h = m.h, w = m.w, gx = kBlocks[0], gy = kBlocks[1], th = h / gy, tw = w / gx;
  out.assign(size_t(h) * w, 0.0f);
  for (int ty = 0; ty < gy; ++ty)
    for (int tx = 0; tx < gx; ++tx) {
      int y1 = ty == gy - 1 ? h : (ty + 1) * th, x1 = tx == gx - 1 ? w : (tx + 1) * tw;
      float t = typical_width(m, tx * tw, ty * th, x1, y1, threshold);
      for (int y = ty * th; y < y1; ++y)
        for (int x = tx * tw; x < x1; ++x) out[size_t(y) * w + x] = t;
    }
}

// curves2d.blur: separable Gaussian, float32, edges clamped.
void blur(std::vector<float>& img, int h, int w, double sigma) {
  int r = std::max(1, int(3 * sigma));
  std::vector<float> k(2 * r + 1);
  for (int i = -r; i <= r; ++i) {
    double t = double(i) / sigma;
    k[i + r] = float(std::exp(-0.5 * (t * t)));
  }
  float ks = pairwise_sum(k.data(), k.size());
  for (float& v : k) v /= ks;
  std::vector<float> acc(img.size());
  // axis 0
  for (int y = 0; y < h; ++y) {
    float* o = &acc[size_t(y) * w];
    std::fill(o, o + w, 0.0f);
    for (int i = 0; i <= 2 * r; ++i) {
      int sy = std::min(std::max(y + i - r, 0), h - 1);
      const float* p = &img[size_t(sy) * w];
      float kv = k[i];
      for (int x = 0; x < w; ++x) o[x] += kv * p[x];
    }
  }
  img.swap(acc);
  // axis 1
  std::vector<float> row(w + 2 * r);
  for (int y = 0; y < h; ++y) {
    const float* src = &img[size_t(y) * w];
    for (int x = 0; x < w + 2 * r; ++x) row[x] = src[std::min(std::max(x - r, 0), w - 1)];
    float* o = &acc[size_t(y) * w];
    std::fill(o, o + w, 0.0f);
    for (int i = 0; i <= 2 * r; ++i) {
      float kv = k[i];
      const float* p = &row[i];
      for (int x = 0; x < w; ++x) o[x] += kv * p[x];
    }
  }
  img.swap(acc);
}

// numpy's float32 sin / cos on x86-64-v3 (loops_trigonometric: Cody-Waite reduction by pi/2 and minimax polynomials,
// fused multiply-adds), so the orientation field matches the reference bit for bit. |x| <= 13176794 (theta is in
// [0, pi] here; numpy falls back to libm beyond).
void sincos_np(float x, float* s, float* c) {
  const float two_over_pi = 0x1.45f306p-1f, magic = 0x1.800000p+23f;
  float q = x * two_over_pi;
  q = (q + magic) - magic;
  float r = std::fmaf(q, -0x1.921fb0p+00f, x);
  r = std::fmaf(q, -0x1.5110b4p-22f, r);
  r = std::fmaf(q, -0x1.846988p-48f, r);
  float r2 = r * r;
  float pc = std::fmaf(0x1.98e616p-16f, r2, -0x1.6c06dcp-10f);
  pc = std::fmaf(pc, r2, 0x1.55553cp-05f);
  pc = std::fmaf(pc, r2, -0x1.000000p-01f);
  pc = std::fmaf(pc, r2, 0x1.000000p+00f);
  float ps = std::fmaf(0x1.7d3bbcp-19f, r2, -0x1.a06bbap-13f);
  ps = std::fmaf(ps, r2, 0x1.11119ap-07f);
  ps = std::fmaf(ps, r2, -0x1.555556p-03f);
  ps = ps * r2;
  ps = std::fmaf(ps, r, r);
  int iq = int(std::nearbyint(q));
  for (int k = 0; k < 2; ++k) {
    int j = iq + k;  // k = 1: cosine
    float v = (j & 1) == 0 ? ps : pc;
    if (j & 2) v = -v;
    (k ? *c : *s) = v;
  }
}

// curves2d.orientation_field.
void orientation_field(const Mask& m, double sigma, std::vector<float>& fx, std::vector<float>& fy) {
  int h = m.h, w = m.w;
  size_t n = size_t(h) * w;
  std::vector<float> a(n), gx(n), gy(n);
  for (size_t i = 0; i < n; ++i) a[i] = float(m.rgba[i * 4 + 3]) / 255.0f;
  for (int y = 0; y < h; ++y)
    for (int x = 0; x < w; ++x) {
      size_t i = size_t(y) * w + x;
      gx[i] = x == 0 ? a[i + 1] - a[i] : x == w - 1 ? a[i] - a[i - 1] : (a[i + 1] - a[i - 1]) / 2.0f;
      gy[i] = y == 0 ? a[i + w] - a[i] : y == h - 1 ? a[i] - a[i - w] : (a[i + w] - a[i - w]) / 2.0f;
    }
  std::vector<float> jxx(n), jxy(n), jyy(n);
  for (size_t i = 0; i < n; ++i) {
    jxx[i] = gx[i] * gx[i];
    jxy[i] = gx[i] * gy[i];
    jyy[i] = gy[i] * gy[i];
  }
  blur(jxx, h, w, sigma);
  blur(jxy, h, w, sigma);
  blur(jyy, h, w, sigma);
  fx.resize(n);
  fy.resize(n);
  const float half_pi = float(kPi / 2);
  for (size_t i = 0; i < n; ++i) {
    float theta = 0.5f * std::atan2(2.0f * jxy[i], jxx[i] - jyy[i]) + half_pi;
    sincos_np(theta, &fy[i], &fx[i]);
  }
}

struct Curve {
  std::vector<double> x, y, w;
  double root_corr = 0;
};

struct Tracer {
  const Mask& m;
  int H, W, K, P, L, PW, min_len = 12;
  std::vector<float> typ, fx, fy, rid;
  std::vector<uint8_t> cov, tried;
  std::vector<int32_t> owner, owner_step;
  std::vector<double> offs;
  std::vector<size_t> log;
  double t = 0;
  int max_gap = 3;
  // scratch (profiles of length L, runs, medians)
  std::vector<uint8_t> pc, pc2, idm;
  std::vector<int> px, py, kx, ky, rs, re, rs2, re2;
  std::vector<float> fbuf;
  std::vector<double> dbuf;

  Tracer(const Mask& mask, int threshold) : m(mask), H(mask.h), W(mask.w) {
    typical_map(m, threshold, typ);
    float tmax = *std::max_element(typ.begin(), typ.end());
    K = int(std::ceil(std::max(4.0, 3.0 * double(tmax))));
    P = K + 2;
    L = 2 * K + 1;
    PW = W + 2 * P;
    int PH = H + 2 * P;
    offs.resize(L);
    for (int i = 0; i < L; ++i) offs[i] = double(i - K);
    size_t pn = size_t(PH) * PW;
    cov.assign(pn, 0);
    rid.assign(pn, 0.0f);
    for (int y = 0; y < H; ++y)
      for (int x = 0; x < W; ++x) {
        size_t i = size_t(y + P) * PW + x + P;
        cov[i] = m.at(x, y, 3) >= threshold;
        rid[i] = float(m.at(x, y, 0));
      }
    std::vector<float> tmp(typ);
    double med = double(median_f(tmp));
    orientation_field(m, std::max(1.0, med), fx, fy);
    owner.assign(pn, -1);
    owner_step.assign(pn, 0);
    tried.assign(pn, 0);
    pc.resize(L); pc2.resize(L); idm.resize(L);
    px.resize(L); py.resize(L); kx.resize(L); ky.resize(L);
    rs.resize(L); re.resize(L); rs2.resize(L); re2.resize(L);
    fbuf.resize(2 * L);
    dbuf.resize(64);
    set_typical(med);
  }

  void set_typical(double tv) {
    t = tv;
    max_gap = int(std::ceil(std::max(3.0, 2.0 * tv)));
  }
  size_t at(int x, int y) const { return size_t(y) * PW + x; }

  void profile(double qx, double qy, double nx, double ny, uint8_t* c, int* ox, int* oy) const {
    double bx = qx + P, by = qy + P;
    for (int i = 0; i < L; ++i) {
      ox[i] = int(std::nearbyint(bx + offs[i] * nx));
      oy[i] = int(std::nearbyint(by + offs[i] * ny));
      if (c) c[i] = cov[at(ox[i], oy[i])];
    }
  }
  bool narrow(double run_w, double w) const { return run_w <= 1.5 * std::max(w, t) + 1; }

  void id_tol(const std::vector<double>& hist, double* med, double* tol) {
    size_t n = std::min<size_t>(hist.size(), 32), s = hist.size() - n;
    for (size_t i = 0; i < n; ++i) dbuf[i] = hist[s + i];
    double md = median_py_d(dbuf.data(), n);
    for (size_t i = 0; i < n; ++i) dbuf[i] = std::abs(hist[s + i] - md);
    double v = 3.0 * 1.4826 * median_py_d(dbuf.data(), n);
    *med = md;
    *tol = v > 8.0 ? v : 8.0;
  }
  void band(double centre, double width, int* lo, int* hi) const {
    int l = std::max(int(std::floor(double(K) + centre - width / 2 + 0.5)), 0);
    *lo = l;
    *hi = std::min(std::max(int(std::floor(double(K) + centre + width / 2 - 0.5)), l), L - 1);
  }
  double band_median(int lo, int hi) {
    int n = 0;
    for (int i = lo; i <= hi; ++i) fbuf[n++] = rid[at(px[i], py[i])];
    return median_py_f(fbuf.data(), n);
  }
  bool foreign(int lo, int hi, int sid, long long step) const {
    int n = hi - lo + 1;
    if (n <= 0) return false;
    int f = 0;
    for (int i = lo; i <= hi; ++i) {
      size_t j = at(px[i], py[i]);
      int o = owner[j];
      if (o >= 0 && (o != sid || std::llabs((long long)owner_step[j] - step) > 2LL * K + 2)) ++f;
    }
    return 2 * f > n;
  }

  struct Half { std::vector<double> x, y, w; std::vector<uint8_t> own; };

  void march(int sid, double x, double y, double dx, double dy, double w, std::vector<double>& rid_hist,
             long long step, Half& out) {
    out.x.clear(); out.y.clear(); out.w.clear(); out.own.clear();
    int gap = 0, coast_run = 0;
    const int k0 = K;
    double cx = x, cy = y;
    size_t max_steps = size_t(4) * (H + W);
    double med, tol;
    id_tol(rid_hist, &med, &tol);
    int fresh = 0;
    std::vector<double> hx{x}, hy{y};
    double omega = 0.0;
    double max_turn = 1.0 / std::max(w, 2.0);
    bool last_fit = true;
    while (out.x.size() < max_steps) {
      if (omega != 0) {
        double co = std::cos(omega), so = std::sin(omega);
        double ndx = dx * co - dy * so, ndy = dx * so + dy * co;
        dx = ndx;
        dy = ndy;
        if (!last_fit) omega *= 0.8;
      }
      last_fit = false;
      double qx = cx + dx, qy = cy + dy;
      if (!(0 <= qx && qx < W - 0.5 && 0 <= qy && qy < H - 0.5)) break;
      double nx = -dy, ny = dx;
      profile(qx, qy, nx, ny, pc.data(), px.data(), py.data());
      double reach = 0.75 * w + 1.5;
      int nr = row_runs(pc.data(), L, rs.data(), re.data());
      int ba = -1, bb = -1;
      double best_d = 0;
      for (int r = 0; r < nr; ++r) {
        int a = rs[r], b = re[r];
        if (a - k0 <= reach && k0 - b <= reach) {
          double d = std::abs(double(a + b) / 2 - k0);
          if (ba < 0 || d < best_d) { ba = a; bb = b; best_d = d; }
        }
      }
      if (ba < 0) {
        if (++gap > max_gap) break;
        cx = qx; cy = qy;
        continue;
      }
      int a = ba, b = bb;
      double le = a - k0 - 0.5, re_ = b - k0 + 0.5;
      double half = w / 2;
      double mid = (le + re_) / 2, run_w = re_ - le;
      bool left = std::abs(le + half) <= kEdgeTol, right = std::abs(re_ - half) <= kEdgeTol;
      bool have = false, fits = true, clean = false;
      double centre = 0, width = w;
      if ((left && right) || (run_w <= w + kEdgeTol && le < 0 && 0 < re_)) {
        centre = mid; width = run_w; clean = true; have = true;
      } else if (le < -0.5 && re_ > 0.5) {
        if (rid_hist.size() >= 8) {
          float fm = float(med), ft = float(tol);
          int n = b - a + 1;
          for (int i = 0; i < n; ++i) idm[i] = std::abs(rid[at(px[a + i], py[a + i])] - fm) <= ft;
          int n2 = row_runs(idm.data(), n, rs2.data(), re2.data());
          for (int r = 0; r < n2; ++r) {
            int a2 = rs2[r] + a, b2 = re2[r] + a;
            double m2 = double(a2 + b2) / 2 - k0, w2 = b2 - a2 + 1.0;
            if (std::abs(m2) <= kEdgeTol && w2 <= w + kEdgeTol) {
              centre = m2; width = w2; have = true;
              break;
            }
          }
        }
        if (!have) { centre = 0.0; fits = false; have = true; }
      }
      if (!have) {
        if (++gap > max_gap) break;
        cx = qx; cy = qy;
        continue;
      }
      int lo, hi;
      band(centre, width, &lo, &hi);
      if (rid_hist.size() >= 8 && hi >= lo && std::abs(band_median(lo, hi) - med) > tol) {
        centre = 0.0; width = w; fits = false;
        band(centre, width, &lo, &hi);
      }
      bool own = fits && !foreign(lo, hi, sid, step);
      coast_run = own ? 0 : coast_run + 1;
      if (coast_run > max_gap) break;
      band(centre, width, &lo, &hi);
      profile(qx - 0.5 * dx, qy - 0.5 * dy, nx, ny, nullptr, kx.data(), ky.data());
      {
        // select over the whole band first, then claim (as the vectorised reference)
        size_t sel[2 * 64];
        size_t* sp = sel;
        std::vector<size_t> big;
        int cnt = 2 * (hi - lo + 1);
        if (cnt > 128) { big.resize(cnt); sp = big.data(); }
        int ns = 0;
        for (int pass = 0; pass < 2; ++pass)
          for (int i = lo; i <= hi; ++i) {
            size_t j = pass == 0 ? at(px[i], py[i]) : at(kx[i], ky[i]);
            if (cov[j] && owner[j] < 0) sp[ns++] = j;
          }
        for (int i = 0; i < ns; ++i) {
          owner[sp[i]] = sid;
          owner_step[sp[i]] = int32_t(step);
          log.push_back(sp[i]);
        }
      }
      if (own) {
        rid_hist.push_back(band_median(lo, hi));
        ++fresh;
        if (fresh >= 4 || rid_hist.size() <= 8) {
          id_tol(rid_hist, &med, &tol);
          fresh = 0;
        }
        w = 0.8 * w + 0.2 * width;
      }
      double px_ = qx + centre * nx, py_ = qy + centre * ny;
      int iy = std::min(std::max(int(std::nearbyint(py_)), 0), H - 1);
      int ix = std::min(std::max(int(std::nearbyint(px_)), 0), W - 1);
      double ffx = fx[size_t(iy) * W + ix], ffy = fy[size_t(iy) * W + ix];
      if (ffx * dx + ffy * dy < 0) { ffx = -ffx; ffy = -ffy; }
      clean = clean && fits;
      if (clean) { hx.push_back(px_); hy.push_back(py_); }
      size_t nh = hx.size();
      if (clean && nh > size_t(kChord)) {
        double ang = std::atan2(hy[nh - 1] - hy[nh - 1 - kChord], hx[nh - 1] - hx[nh - 1 - kChord]);
        if (nh > size_t(2 * kChord)) {
          double prev = std::atan2(hy[nh - 1 - kChord] - hy[nh - 1 - 2 * kChord],
                                   hx[nh - 1 - kChord] - hx[nh - 1 - 2 * kChord]);
          double turn = py_mod(ang - prev + kPi, 2 * kPi) - kPi;
          omega = std::min(std::max(0.5 * omega + 0.5 * turn / kChord, -max_turn), max_turn);
        }
        ang += omega * kChord / 2;
        double ndx = std::cos(ang) + 0.2 * ffx, ndy = std::sin(ang) + 0.2 * ffy;
        double nl = std::pow(ndx * ndx + ndy * ndy, 0.5);
        dx = ndx / nl; dy = ndy / nl;
      } else if (clean) {
        double mx = px_ - cx, my = py_ - cy;
        double ml = std::pow(mx * mx + my * my, 0.5);
        if (ml > 1e-6) { mx = mx / ml; my = my / ml; }
        double ndx = dx + 0.3 * mx + 0.2 * ffx, ndy = dy + 0.3 * my + 0.2 * ffy;
        double nl = std::pow(ndx * ndx + ndy * ndy, 0.5);
        dx = ndx / nl; dy = ndy / nl;
      }
      cx = px_; cy = py_;
      out.x.push_back(cx); out.y.push_back(cy); out.w.push_back(width); out.own.push_back(own);
      last_fit = fits;
      gap = 0;
      ++step;
    }
    while (!out.own.empty() && !out.own.back()) {
      out.x.pop_back(); out.y.pop_back(); out.w.pop_back(); out.own.pop_back();
    }
  }

  Half fwd, bwd;
  std::vector<double> hist, arc, bv;

  bool trace_from(int sid, int x0, int y0, Curve& c) {
    set_typical(double(typ[size_t(y0) * W + x0]));
    double dx = fx[size_t(y0) * W + x0], dy = fy[size_t(y0) * W + x0];
    profile(double(x0), double(y0), -dy, dx, pc.data(), px.data(), py.data());
    int nr = row_runs(pc.data(), L, rs.data(), re.data());
    int k0 = K;
    double off = 0.0, w = t;
    for (int r = 0; r < nr; ++r) {
      if (rs[r] <= k0 && k0 <= re[r]) {
        int a = rs[r], b = re[r];
        if (narrow(double(b - a + 1), t)) {
          off = double(a + b) / 2 - k0;
          double bw = double(b - a + 1);
          w = t > bw ? t : bw;
        }
        break;
      }
    }
    double x = x0 - dy * off, y = y0 + dx * off;
    log.clear();
    double seed_id = rid[at(x0 + P, y0 + P)];
    hist.assign(1, seed_id);
    march(sid, x, y, dx, dy, w, hist, 1, fwd);
    hist.assign(1, seed_id);
    march(sid, x, y, -dx, -dy, w, hist, -(1LL << 30), bwd);
    size_t n = fwd.x.size() + bwd.x.size() + 1;
    size_t owned = 1;
    for (uint8_t o : fwd.own) owned += o;
    for (uint8_t o : bwd.own) owned += o;
    if (n < 2 || owned < size_t(min_len)) {
      for (size_t j : log) owner[j] = -1;
      return false;
    }
    c.x.resize(n); c.y.resize(n); c.w.resize(n);
    size_t nb = bwd.x.size();
    for (size_t i = 0; i < nb; ++i) {
      c.x[i] = bwd.x[nb - 1 - i]; c.y[i] = bwd.y[nb - 1 - i]; c.w[i] = bwd.w[nb - 1 - i];
    }
    c.x[nb] = x; c.y[nb] = y; c.w[nb] = w;
    for (size_t i = 0; i < fwd.x.size(); ++i) {
      c.x[nb + 1 + i] = fwd.x[i]; c.y[nb + 1 + i] = fwd.y[i]; c.w[nb + 1 + i] = fwd.w[i];
    }
    arc.resize(n);
    bv.resize(n);
    arc[0] = 0;
    for (size_t i = 1; i < n; ++i) arc[i] = arc[i - 1] + std::hypot(c.x[i] - c.x[i - 1], c.y[i] - c.y[i - 1]);
    bool varies = false;
    for (size_t i = 0; i < n; ++i) {
      int yi = std::min(std::max(int(std::nearbyint(c.y[i])), 0), H - 1);
      int xi = std::min(std::max(int(std::nearbyint(c.x[i])), 0), W - 1);
      bv[i] = m.at(xi, yi, 2);
      varies = varies || bv[i] != bv[0];
    }
    double corr = varies ? corrcoef(arc, bv) : 0.0;
    if (corr < 0) {
      std::reverse(c.x.begin(), c.x.end());
      std::reverse(c.y.begin(), c.y.end());
      std::reverse(c.w.begin(), c.w.end());
      corr = -corr;
    }
    c.root_corr = corr;
    return true;
  }
};

// curves2d.trace: curves root to tip; claimed = covered texels a kept strand took.
void trace(Tracer& tr, std::vector<Curve>& curves) {
  int H = tr.H, W = tr.W, P = tr.P;
  // roots first (B ascending), then raster order: a stable counting sort of the covered texels.
  std::vector<int> count(257, 0);
  for (int y = 0; y < H; ++y)
    for (int x = 0; x < W; ++x)
      if (tr.cov[tr.at(x + P, y + P)]) ++count[tr.m.at(x, y, 2) + 1];
  for (int i = 1; i < 257; ++i) count[i] += count[i - 1];
  std::vector<int> order(count[256]);
  for (int y = 0; y < H; ++y)
    for (int x = 0; x < W; ++x)
      if (tr.cov[tr.at(x + P, y + P)]) order[count[tr.m.at(x, y, 2)]++] = y * W + x;
  int sid = 0;
  Curve c;
  for (int i : order) {
    int y0 = i / W, x0 = i % W;
    size_t j = tr.at(x0 + P, y0 + P);
    if (tr.owner[j] >= 0 || tr.tried[j]) continue;
    tr.tried[j] = 1;
    bool ok = tr.trace_from(sid, x0, y0, c);
    ++sid;
    if (ok) curves.push_back(c);
  }
}

// curves2d.simplify: kept indices.
void simplify(const std::vector<double>& x, const std::vector<double>& y, double eps, double max_step,
              std::vector<int>& idx) {
  int n = int(x.size());
  std::vector<uint8_t> keep(n, 0);
  keep[0] = keep[n - 1] = 1;
  std::vector<double> seg(n, 0.0);
  for (int i = 1; i < n; ++i) seg[i] = seg[i - 1] + std::hypot(x[i] - x[i - 1], y[i] - y[i - 1]);
  std::vector<std::pair<int, int>> stack{{0, n - 1}};
  while (!stack.empty()) {
    auto [a, b] = stack.back();
    stack.pop_back();
    if (b - a < 2) continue;
    double ex = x[b] - x[a], ey = y[b] - y[a];
    double el = std::hypot(ex, ey);
    int k = 0;
    double dk = -std::numeric_limits<double>::infinity();
    for (int i = a + 1; i < b; ++i) {
      double d = el < 1e-9 ? std::hypot(x[i] - x[a], y[i] - y[a])
                           : std::abs((x[i] - x[a]) * ey - (y[i] - y[a]) * ex) / el;
      if (d > dk || (std::isnan(d) && !std::isnan(dk))) { dk = d; k = i - a - 1; }
    }
    int mm;
    if (dk > eps) mm = a + 1 + k;
    else if (seg[b] - seg[a] > max_step) mm = (a + b) / 2;
    else continue;
    keep[mm] = 1;
    stack.push_back({a, mm});
    stack.push_back({mm, b});
  }
  idx.clear();
  for (int i = 0; i < n; ++i)
    if (keep[i]) idx.push_back(i);
}

// curves2d.resample: x, y, width, normal (nx, ny) of the kept points.
void resample(const Curve& c, double step, double eps, std::vector<double>& ox, std::vector<double>& oy,
              std::vector<double>& ow, std::vector<double>& onx, std::vector<double>& ony) {
  int n = int(c.x.size());
  std::vector<double> xs(c.x), ys(c.y);
  if (n >= 5) {
    for (int i = 0; i < n; ++i) {
      double sx = 0, sy = 0;
      for (int j = 0; j < 5; ++j) {
        int s = std::min(std::max(i + j - 2, 0), n - 1);
        sx += c.x[s] * 0.2;
        sy += c.y[s] * 0.2;
      }
      xs[i] = sx;
      ys[i] = sy;
    }
    xs[0] = c.x[0]; ys[0] = c.y[0]; xs[n - 1] = c.x[n - 1]; ys[n - 1] = c.y[n - 1];
  }
  std::vector<int> idx;
  simplify(xs, ys, eps, step, idx);
  ox.clear(); oy.clear(); ow.clear(); onx.clear(); ony.clear();
  double win[5];
  for (int i : idx) {
    double tx = i == 0 ? xs[1] - xs[0] : i == n - 1 ? xs[n - 1] - xs[n - 2] : (xs[i + 1] - xs[i - 1]) / 2.0;
    double ty = i == 0 ? ys[1] - ys[0] : i == n - 1 ? ys[n - 1] - ys[n - 2] : (ys[i + 1] - ys[i - 1]) / 2.0;
    double tl = std::max(std::hypot(tx, ty), 1e-9);
    int a = std::max(0, i - 2), b = std::min(n, i + 3), k = b - a;
    for (int j = 0; j < k; ++j) win[j] = c.w[a + j];
    std::sort(win, win + k);
    double wm = k % 2 ? win[k / 2] : (win[k / 2 - 1] + win[k / 2]) / 2.0;
    ox.push_back(xs[i]); oy.push_back(ys[i]); ow.push_back(wm);
    onx.push_back(-ty / tl); ony.push_back(tx / tl);
  }
}

}  // namespace

bool root_row_correlation(const Mask& mask, const int grid[2], int threshold, double* corr) {
  int h = mask.h, w = mask.w, th = h / grid[1], tw = w / grid[0];
  std::vector<double> out, yy, bb;
  for (int ty = 0; ty < grid[1]; ++ty)
    for (int tx = 0; tx < grid[0]; ++tx) {
      yy.clear(); bb.clear();
      for (int y = 0; y < th; ++y)
        for (int x = 0; x < tw; ++x)
          if (mask.at(tx * tw + x, ty * th + y, 3) >= threshold) {
            yy.push_back(double(y));
            bb.push_back(double(mask.at(tx * tw + x, ty * th + y, 2)));
          }
      if (yy.size() > 10) out.push_back(corrcoef(yy, bb));
    }
  if (out.empty()) return false;
  *corr = median_d(out);
  return true;
}

void curve_strands(const Mask& mask, const int grid[2], int step, int threshold, std::vector<Row>& rows,
                   std::vector<double>& lo, std::vector<double>& hi, std::map<std::pair<int, int>, TileStat>& stats,
                   std::vector<double>& corr, int threads) {
  (void)threads;
  rows.clear(); lo.clear(); hi.clear(); stats.clear(); corr.clear();
  int h = mask.h, w = mask.w;
  Tracer tr(mask, threshold);
  std::vector<Curve> curves;
  trace(tr, curves);
  std::vector<double> length(curves.size()), d;
  std::vector<double> x, y, wd, nx, ny;
  for (size_t ci = 0; ci < curves.size(); ++ci) {
    const Curve& c = curves[ci];
    double t = tr.typ[size_t(int(std::nearbyint(c.y[0]))) * w + int(std::nearbyint(c.x[0]))];
    double eps = t / 4 > 0.5 ? t / 4 : 0.5;
    resample(c, double(step), eps, x, y, wd, nx, ny);
    Row r;
    size_t n = x.size();
    r.u.resize(n); r.v.resize(n); r.wd.resize(n); r.slope.assign(n, 0.0); r.root.resize(n); r.depth.resize(n);
    r.nx = nx; r.ny = ny;
    double ulo = DBL_MAX, vlo = DBL_MAX, uhi = -DBL_MAX, vhi = -DBL_MAX;
    for (size_t i = 0; i < n; ++i) {
      int xi = std::min(std::max(int(std::nearbyint(x[i])), 0), w - 1);
      int yi = std::min(std::max(int(std::nearbyint(y[i])), 0), h - 1);
      r.u[i] = (x[i] + 0.5) / w;
      r.v[i] = (y[i] + 0.5) / h;
      r.wd[i] = wd[i] / w;
      r.root[i] = mask.at(xi, yi, 2) / 255.0;
      r.depth[i] = mask.at(xi, yi, 1) / 255.0;
      ulo = std::min(ulo, r.u[i]); vlo = std::min(vlo, r.v[i]);
      uhi = std::max(uhi, r.u[i]); vhi = std::max(vhi, r.v[i]);
    }
    lo.push_back(ulo); lo.push_back(vlo);
    hi.push_back(uhi); hi.push_back(vhi);
    rows.push_back(std::move(r));
    d.resize(c.x.size() - 1);
    for (size_t i = 0; i + 1 < c.x.size(); ++i) d[i] = std::hypot(c.x[i + 1] - c.x[i], c.y[i + 1] - c.y[i]);
    length[ci] = pairwise_sum(d.data(), d.size());
  }
  int gx = grid[0], gy = grid[1], th = h / gy, tw = w / gx;
  std::vector<std::vector<int>> mine(size_t(gx) * gy);
  for (size_t ci = 0; ci < curves.size(); ++ci) {
    int rx = std::min(int(curves[ci].x[0] * gx / w), gx - 1), ry = std::min(int(curves[ci].y[0] * gy / h), gy - 1);
    if (rx >= 0 && ry >= 0) mine[size_t(ry) * gx + rx].push_back(int(ci));
  }
  std::vector<float> tt;
  std::vector<int> o;
  std::vector<double> cs;
  for (int ty = 0; ty < gy; ++ty)
    for (int tx = 0; tx < gx; ++tx) {
      long long covn = 0, got = 0;
      tt.clear();
      for (int yy = ty * th; yy < (ty + 1) * th; ++yy)
        for (int xx = tx * tw; xx < (tx + 1) * tw; ++xx) {
          tt.push_back(tr.typ[size_t(yy) * w + xx]);
          if (mask.at(xx, yy, 3) >= threshold) {
            ++covn;
            got += tr.owner[tr.at(xx + tr.P, yy + tr.P)] >= 0;
          }
        }
      const std::vector<int>& mi = mine[size_t(ty) * gx + tx];
      TileStat s;
      s.strands = int(mi.size());
      s.typical_px = tt.empty() ? std::numeric_limits<double>::quiet_NaN() : double(median_f(tt));
      s.recall = double(got) / double(std::max(covn, 1LL));
      stats[{tx, ty}] = s;
      if (!mi.empty()) {
        size_t n = mi.size();
        o.resize(n);
        for (size_t i = 0; i < n; ++i) o[i] = int(i);
        std::stable_sort(o.begin(), o.end(),
                         [&](int a, int b) { return curves[mi[a]].root_corr < curves[mi[b]].root_corr; });
        std::vector<double> ll(n);
        for (size_t i = 0; i < n; ++i) ll[i] = length[mi[i]];
        double halfl = pairwise_sum(ll.data(), n) / 2;
        cs.resize(n);
        double acc = 0;
        size_t k = n;
        for (size_t i = 0; i < n; ++i) {
          acc += ll[o[i]];
          if (k == n && acc >= halfl) k = i;
        }
        corr.push_back(curves[mi[o[std::min(k, n - 1)]]].root_corr);
      }
    }
}

}  // namespace hairconv
