// Validation renders of the native converter: a port of tools/hair_groom/render.py and the validation loop of
// tools/hair_groom/hcap.py reference() (batch.bbox_views, card vs strand coverage and radiance per view).
// Same arithmetic as the reference where it can change a number: float64 everywhere numpy uses float64, float32
// where numpy keeps float32 (the plain cards' mask opacity, and with it their compositing cumsum), the same
// fragment order (generation order, then a stable pixel/depth sort, ties by generation order) because compositing is
// order dependent. Deliberate differences, all at rounding level (checked against the Python reference on 12 styles:
// iou equal, coverage within 1e-14, radiance within 1e-8): the coverage-only render (fibre cards) sums optical depth
// in generation order instead of depth order, x ** 40 is a product chain, and log1p / exp are the C runtime's.
#include "hairconv.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <immintrin.h>
#include <limits>
#include <condition_variable>
#include <mutex>
#include <thread>

namespace hairconv {
namespace {


constexpr double kHair[3] = {0.30, 0.20, 0.14};
constexpr double kSkin[3] = {0.55, 0.42, 0.36};
constexpr int kSlabs = 64;

// A small pool for one validate_full call. parallel_for(n, fn) runs fn(i) for i in [0, n): the caller and any idle
// worker take indices; nested calls (a view task splitting its fragments) are fine because a caller only waits for
// indices already being run. Results go to per-index slots, so the outcome never depends on the schedule.
class Pool;
Pool*& tl_pool_ref() {
  thread_local Pool* p = nullptr;
  return p;
}

class Pool {
 public:
  explicit Pool(int threads) {
    for (int t = 1; t < threads; ++t) workers_.emplace_back([this] {
      tl_pool_ref() = this;
      loop();
    });
  }
  ~Pool() {
    {
      std::lock_guard<std::mutex> l(mu_);
      stop_ = true;
    }
    cv_.notify_all();
    for (auto& t : workers_) t.join();
  }
  template <class F>
  void run(int n, F& fn) {
    if (n <= 0) return;
    if (workers_.empty() || n == 1) {
      for (int i = 0; i < n; ++i) fn(i);
      return;
    }
    Job job;
    job.n = n;
    job.call = [](void* f, int i) { (*static_cast<F*>(f))(i); };
    job.fn = &fn;
    {
      std::lock_guard<std::mutex> l(mu_);
      jobs_.push_back(&job);
    }
    cv_.notify_all();
    work(job);
    std::unique_lock<std::mutex> l(mu_);
    jobs_.erase(std::remove(jobs_.begin(), jobs_.end(), &job), jobs_.end());
    done_cv_.wait(l, [&] { return job.done.load() == job.n && job.users == 0; });
  }

 private:
  struct Job {
    int n = 0;
    void (*call)(void*, int) = nullptr;
    void* fn = nullptr;
    std::atomic<int> next{0}, done{0};
    int users = 0;  // workers holding the pointer (guarded by mu_)
  };
  void work(Job& job) {
    for (int i; (i = job.next.fetch_add(1)) < job.n;) {
      job.call(job.fn, i);
      if (job.done.fetch_add(1) + 1 == job.n) {
        std::lock_guard<std::mutex> l(mu_);
        done_cv_.notify_all();
      }
    }
  }
  void loop() {
    std::unique_lock<std::mutex> l(mu_);
    for (;;) {
      Job* job = nullptr;
      cv_.wait(l, [&] {
        if (stop_) return true;
        for (Job* j : jobs_)
          if (j->next.load() < j->n) {
            job = j;
            return true;
          }
        return false;
      });
      if (!job) return;
      ++job->users;
      l.unlock();
      work(*job);
      l.lock();
      if (--job->users == 0) done_cv_.notify_all();
    }
  }
  std::vector<std::thread> workers_;
  std::mutex mu_;
  std::condition_variable cv_, done_cv_;
  std::vector<Job*> jobs_;
  bool stop_ = false;
};


// On the pool of the running validate_full (inline without one).
template <class F>
void parallel_for(int n, F&& fn) {
  if (Pool* p = tl_pool_ref()) {
    p->run(n, fn);
    return;
  }
  for (int i = 0; i < n; ++i) fn(i);
}
inline double dot3(const double a[3], const double b[3]) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }
inline void cross3(const double a[3], const double b[3], double o[3]) {  // np.cross order
  o[0] = a[1] * b[2] - a[2] * b[1];
  o[1] = a[2] * b[0] - a[0] * b[2];
  o[2] = a[0] * b[1] - a[1] * b[0];
}

struct View {
  double c[3], r[3], u[3], f[3];
  double pixel;
  int w, h;
  double hw, hh;  // size / 2
  void project(double px, double py, double pz, double& x, double& y, double& d) const {
    const double dx = px - c[0], dy = py - c[1], dz = pz - c[2];
    x = (dx * r[0] + dy * r[1] + dz * r[2]) / pixel + hw;
    y = -(dx * u[0] + dy * u[1] + dz * u[2]) / pixel + hh;
    d = dx * f[0] + dy * f[1] + dz * f[2];
  }
  // render._keep: floor inside the image (NaN is outside).
  bool keep(double x, double y, int32_t& pix) const {
    if (!(x >= 0 && x < w && y >= 0 && y < h)) return false;
    pix = int32_t(int64_t(std::floor(y)) * w + int64_t(std::floor(x)));
    return true;
  }
  int npix() const { return w * h; }
};

View make_view(const double centre[3], const double forward[3], const double up[3], double pixel, int w, int h) {
  View v{};
  for (int k = 0; k < 3; ++k) v.c[k] = centre[k];
  const double fn = std::sqrt(dot3(forward, forward));
  for (int k = 0; k < 3; ++k) v.f[k] = forward[k] / fn;
  cross3(up, v.f, v.r);
  const double rn = std::sqrt(dot3(v.r, v.r));
  for (int k = 0; k < 3; ++k) v.r[k] /= rn;
  cross3(v.f, v.r, v.u);
  v.pixel = pixel;
  v.w = w;
  v.h = h;
  v.hw = w / 2.0;
  v.hh = h / 2.0;
  return v;
}

// render.light_view_for; ld is the normalised light direction.
View light_view_for(const View& view, const double ld[3], double pixel) {
  const double up_a[3] = {0, 1.0, 0}, up_b[3] = {0, 0, 1.0};
  const double fwd[3] = {-ld[0], -ld[1], -ld[2]};
  const int side = int(0.8 / pixel);
  return make_view(view.c, fwd, std::abs(ld[1]) < 0.9 ? up_a : up_b, pixel, side, side);
}

// render.Fragments, one 64-byte record per fragment (the shading pass gathers it in pixel order: one cache line).
// Plain card opacity is float32 in numpy (FragList::f32): kept as the exact float value in the double.
// pix < 0 marks a strand sample outside the image (the reference drops it; every consumer skips it, order unchanged).
struct Frag {
  double depth, alpha, root, tan[3];
  float world[3];
  int32_t pix;
};
static_assert(sizeof(Frag) == 64, "one cache line");
// Light-view fragments only feed the shadow map.
struct LFrag {
  double depth, alpha;
  int32_t pix;
};

inline void fill(Frag& o, int32_t pix, double d, double a, const double t[3], double root, double wx, double wy,
                 double wz) {
  o.depth = d;
  o.alpha = a;
  o.root = root;
  o.tan[0] = t[0];
  o.tan[1] = t[1];
  o.tan[2] = t[2];
  o.world[0] = float(wx);
  o.world[1] = float(wy);
  o.world[2] = float(wz);
  o.pix = pix;
}
inline void fill(LFrag& o, int32_t pix, double d, double a, const double*, double, double, double, double) {
  o.depth = d;
  o.alpha = a;
  o.pix = pix;
}

template <class F>
struct FragList {
  std::vector<F> v;
  bool f32 = false;
  size_t size() const { return v.size(); }
};

// Concatenates chunk outputs in chunk order (the generation order of the reference).
template <class T>
void concat(std::vector<std::vector<T>>& parts, std::vector<T>& out) {
  std::vector<size_t> at(parts.size() + 1, 0);
  for (size_t i = 0; i < parts.size(); ++i) at[i + 1] = at[i] + parts[i].size();
  out.resize(at.back());
  parallel_for(int(parts.size()), [&](int i) {
    if (!parts[i].empty()) std::memcpy(&out[at[i]], parts[i].data(), parts[i].size() * sizeof(T));
  });
}

// ---- strands (render.strand_fragments / _strand_chunk) ----
// Two passes: samples per segment and chunk offsets, then every chunk writes its samples in place.
template <class F>
void strand_fragments(const Groom& g, const View& v, FragList<F>& out, double spacing = 0.5) {
  const size_t P = g.points_count();
  std::vector<uint8_t> start(P, 0);
  for (size_t s = 0; s + 1 < g.offsets.size(); ++s)
    if (g.offsets[s] < P) start[g.offsets[s]] = 1;
  std::vector<double> px(P), py(P);
  std::vector<int64_t> nseg(P, 0);
  const int chunks = int(std::min<size_t>(256, std::max<size_t>(1, P / 4096)));
  std::vector<size_t> at(chunks + 1, 0);
  parallel_for(chunks, [&](int ci) {
    const size_t e0 = P * size_t(ci) / chunks, e1 = P * size_t(ci + 1) / chunks;
    double d;
    for (size_t e = e0; e < e1; ++e)
      v.project(double(g.points[e * 3]), double(g.points[e * 3 + 1]), double(g.points[e * 3 + 2]), px[e], py[e], d);
  });
  parallel_for(chunks, [&](int ci) {
    const size_t e0 = P * size_t(ci) / chunks, e1 = P * size_t(ci + 1) / chunks;
    size_t total = 0;
    for (size_t e = std::max<size_t>(e0, 1); e < e1; ++e) {
      if (start[e]) continue;
      const double hyp = std::hypot(px[e] - px[e - 1], py[e] - py[e - 1]);
      nseg[e] = std::max<int64_t>(1, int64_t(std::ceil(hyp / spacing - 1e-6)));
      total += size_t(nseg[e]);
    }
    at[ci + 1] = total;
  });
  for (int ci = 0; ci < chunks; ++ci) at[ci + 1] += at[ci];
  out.f32 = false;
  out.v.resize(at.back());
  parallel_for(chunks, [&](int ci) {
    const size_t e0 = P * size_t(ci) / chunks, e1 = P * size_t(ci + 1) / chunks;
    F* o = out.v.data() + at[ci];
    const double pp = v.pixel * v.pixel;
    for (size_t e = std::max<size_t>(e0, 1); e < e1; ++e) {
      const int64_t n = nseg[e];
      if (!n) continue;
      const float* a = &g.points[(e - 1) * 3];
      const float* b = &g.points[e * 3];
      const double p0[3] = {a[0], a[1], a[2]}, p1[3] = {b[0], b[1], b[2]};
      const double hyp = std::hypot(px[e] - px[e - 1], py[e] - py[e - 1]);
      const double dp[3] = {p1[0] - p0[0], p1[1] - p0[1], p1[2] - p0[2]};
      double tan[3] = {dp[0], dp[1], dp[2]};
      const double tn = std::max(std::sqrt(tan[0] * tan[0] + tan[1] * tan[1] + tan[2] * tan[2]), 1e-12);
      for (double& t : tan) t /= tn;
      const double r0 = g.radii[e - 1], r1 = g.radii[e];
      const double q0 = g.root[e - 1], q1 = g.root[e];
      const double proj_len = hyp * v.pixel / double(n);
      for (int64_t j = 0; j < n; ++j, ++o) {
        const double t = (double(j) + 0.5) / double(n);
        const double p[3] = {p0[0] + dp[0] * t, p0[1] + dp[1] * t, p0[2] + dp[2] * t};
        double x, y, d;
        v.project(p[0], p[1], p[2], x, y, d);
        int32_t pix;
        if (!v.keep(x, y, pix)) {
          o->pix = -1;
          continue;
        }
        const double r = r0 * (1 - t) + r1 * t;
        fill(*o, pix, d, std::min(1.0, 2 * r * proj_len / pp), tan, q0 * (1 - t) + q1 * t, p[0], p[1], p[2]);
      }
    }
  });
}

// ---- cards (render._card_fragments with both=True) ----
struct CardTex {
  int tw = 0, th = 0;
  std::vector<float> alpha, root;
};

// render.box_filter of one mask channel.
void box_filter(const Mask& m, int channel, int factor, int& tw, int& th, std::vector<float>& out) {
  if (factor <= 1) {
    tw = m.w; th = m.h;
    out.resize(size_t(tw) * th);
    for (int y = 0; y < th; ++y)
      for (int x = 0; x < tw; ++x) out[size_t(y) * tw + x] = float(m.at(x, y, channel)) / 255.0f;
    return;
  }
  tw = m.w / factor; th = m.h / factor;
  out.resize(size_t(tw) * th);
  const double count = double(factor) * factor;
  for (int y = 0; y < th; ++y)
    for (int x = 0; x < tw; ++x) {
      uint32_t s = 0;
      for (int yy = 0; yy < factor; ++yy)
        for (int xx = 0; xx < factor; ++xx) s += m.at(x * factor + xx, y * factor + yy, channel);
      out[size_t(y) * tw + x] = float(double(s) / count) / 255.0f;
    }
}

CardTex card_texture(const Mask& m, double pixel) {
  const double texel = 0.03 / (m.w / 8.0);
  const int factor = int(std::pow(2.0, std::max(0.0, std::floor(std::log2(pixel / texel)))));
  CardTex t;
  box_filter(m, 3, factor, t.tw, t.th, t.alpha);
  int tw, th;
  box_filter(m, 2, factor, tw, th, t.root);
  return t;
}

// Plain fragments (float32 opacity) and, if `fibre` is given, the fibre opacity of each of them (the reference's
// fibre set is the subset with fibre opacity > 0, in the same order).
template <class F>
void card_fragments(const Mesh& m, const CardTex& tex, const View& v, FragList<F>& plain, std::vector<double>* fibre) {
  const uint32_t V = m.vertices(), T = m.triangles();
  std::vector<double> px_(V), py_(V), pd_(V);
  parallel_for(int((V + 4095) / 4096), [&](int ci) {
    for (uint32_t i = ci * 4096u; i < std::min(V, ci * 4096u + 4096u); ++i)
      v.project(double(m.pos[i * 3]), double(m.pos[i * 3 + 1]), double(m.pos[i * 3 + 2]), px_[i], py_[i], pd_[i]);
  });
  const int chunks = int(std::max<uint32_t>(1, std::min<uint32_t>(256, (T + 15) / 16)));
  std::vector<std::vector<F>> parts(chunks);
  std::vector<std::vector<double>> fparts(chunks);
  const int tw = tex.tw, th = tex.th;
  parallel_for(chunks, [&](int ci) {
    const uint32_t tri0 = uint32_t(uint64_t(T) * ci / chunks), tri1 = uint32_t(uint64_t(T) * (ci + 1) / chunks);
    std::vector<F>& o = parts[ci];
    std::vector<double>& fo = fparts[ci];
    for (uint32_t tri = tri0; tri < tri1; ++tri) {
      const uint32_t* iv = &m.tris[size_t(tri) * 3];
      const double tx[3] = {px_[iv[0]], px_[iv[1]], px_[iv[2]]};
      const double ty[3] = {py_[iv[0]], py_[iv[1]], py_[iv[2]]};
      const double td[3] = {pd_[iv[0]], pd_[iv[1]], pd_[iv[2]]};
      int64_t lo_x = int64_t(std::floor(std::min({tx[0], tx[1], tx[2]})));
      int64_t hi_x = int64_t(std::ceil(std::max({tx[0], tx[1], tx[2]})));
      int64_t lo_y = int64_t(std::floor(std::min({ty[0], ty[1], ty[2]})));
      int64_t hi_y = int64_t(std::ceil(std::max({ty[0], ty[1], ty[2]})));
      lo_x = std::max<int64_t>(lo_x, 0); hi_x = std::min<int64_t>(hi_x, v.w - 1);
      lo_y = std::max<int64_t>(lo_y, 0); hi_y = std::min<int64_t>(hi_y, v.h - 1);
      if (hi_x < lo_x || hi_y < lo_y) continue;
      const double ax = tx[0], ay = ty[0];
      const double dx1 = tx[1] - ax, dy1 = ty[1] - ay, dx2 = tx[2] - ax, dy2 = ty[2] - ay;
      const double det = dx1 * dy2 - dy1 * dx2;
      if (std::abs(det) < 1e-12) continue;  // NaN in the reference: nothing inside
      // Per triangle: positions, tangent dP/dv, fibre factor.
      double P[3][3], U[3][2];
      for (int k = 0; k < 3; ++k) {
        for (int c = 0; c < 3; ++c) P[k][c] = double(m.pos[size_t(iv[k]) * 3 + c]);
        for (int c = 0; c < 2; ++c) U[k][c] = double(m.uv[size_t(iv[k]) * 2 + c]);
      }
      const double e1[3] = {P[1][0] - P[0][0], P[1][1] - P[0][1], P[1][2] - P[0][2]};
      const double e2[3] = {P[2][0] - P[0][0], P[2][1] - P[0][1], P[2][2] - P[0][2]};
      const double t1[2] = {U[1][0] - U[0][0], U[1][1] - U[0][1]};
      const double t2[2] = {U[2][0] - U[0][0], U[2][1] - U[0][1]};
      double tan[3];
      for (int c = 0; c < 3; ++c) tan[c] = e2[c] * t1[0] - e1[c] * t2[0];
      const double sd = t1[0] * t2[1] - t1[1] * t2[0];
      const double sg = sd > 0 ? 1.0 : sd < 0 ? -1.0 : 0.0;
      for (double& x : tan) x *= sg;
      const double tn = std::max(std::sqrt(tan[0] * tan[0] + tan[1] * tan[1] + tan[2] * tan[2]), 1e-12);
      for (double& x : tan) x /= tn;
      double nrm[3];
      cross3(e1, e2, nrm);
      const double nn = std::max(std::sqrt(nrm[0] * nrm[0] + nrm[1] * nrm[1] + nrm[2] * nrm[2]), 1e-12);
      for (double& x : nrm) x /= nn;
      const double cos_n = std::abs(nrm[0] * v.f[0] + nrm[1] * v.f[1] + nrm[2] * v.f[2]);
      const double tf = tan[0] * v.f[0] + tan[1] * v.f[1] + tan[2] * v.f[2];
      const double sin_t = std::sqrt(std::clamp(1.0 - tf * tf, 0.0, 1.0));
      const double cos_c = std::max(cos_n, 1e-3);
      for (int64_t yi = lo_y; yi <= hi_y; ++yi) {
        const double py = double(yi) + 0.5;
        for (int64_t xi = lo_x; xi <= hi_x; ++xi) {
          const double px = double(xi) + 0.5;
          const double dx = px - ax, dy = py - ay;
          const double l1 = (dx * dy2 - dy * dx2) / det;
          const double l2 = (dx1 * dy - dy1 * dx) / det;
          const double l0 = 1 - l1 - l2;
          if (!(l0 >= 0 && l1 >= 0 && l2 >= 0)) continue;
          const double u = l0 * U[0][0] + l1 * U[1][0] + l2 * U[2][0];
          const double w = l0 * U[0][1] + l1 * U[1][1] + l2 * U[2][1];
          const int64_t ui = std::clamp<int64_t>(int64_t(u * tw), 0, tw - 1);
          const int64_t vi = std::clamp<int64_t>(int64_t(w * th), 0, th - 1);
          const float a = tex.alpha[size_t(vi) * tw + ui];
          if (!(a > 0)) continue;
          const double dep = l0 * td[0] + l1 * td[1] + l2 * td[2];
          o.emplace_back();
          fill(o.back(), int32_t(yi * v.w + xi), dep, double(a), tan, double(tex.root[size_t(vi) * tw + ui]),
               l0 * P[0][0] + l1 * P[1][0] + l2 * P[2][0], l0 * P[0][1] + l1 * P[1][1] + l2 * P[2][1],
               l0 * P[0][2] + l1 * P[1][2] + l2 * P[2][2]);
          if (fibre) fo.push_back(std::min(1.0, double(a) * sin_t / cos_c));
        }
      }
    }
  });
  concat(parts, plain.v);
  plain.f32 = true;
  if (fibre) concat(fparts, *fibre);
}

// ---- head proxy (render.ellipsoid_hit) ----
struct HeadHit {
  std::vector<double> depth, n;  // [npix], [npix * 3]
};

inline void pixel_plane(const View& v, int x, int y, double o[3]) {
  const double xs = (double(x) + 0.5 - v.hw) * v.pixel;
  const double ys = -((double(y) + 0.5 - v.hh) * v.pixel);
  for (int k = 0; k < 3; ++k) o[k] = v.c[k] + xs * v.r[k] + ys * v.u[k];
}

void ellipsoid_hit(const View& v, const Head& head, HeadHit& hh) {
  const int np = v.npix();
  hh.depth.assign(np, 0);
  hh.n.assign(size_t(np) * 3, 0);
  const double* centre = head.centre;
  const double* radii = head.radii;
  const double dv[3] = {v.f[0] / radii[0], v.f[1] / radii[1], v.f[2] / radii[2]};
  const double a = dot3(dv, dv);
  for (int y = 0; y < v.h; ++y)
    for (int x = 0; x < v.w; ++x) {
      double o[3];
      pixel_plane(v, x, y, o);
      for (int k = 0; k < 3; ++k) o[k] = o[k] - 2.0 * v.f[k];
      double oc[3];
      for (int k = 0; k < 3; ++k) oc[k] = (o[k] - centre[k]) / radii[k];
      const double b = (2 * oc[0]) * dv[0] + (2 * oc[1]) * dv[1] + (2 * oc[2]) * dv[2];
      const double c = (oc[0] * oc[0] + oc[1] * oc[1] + oc[2] * oc[2]) - 1;
      const double disc = b * b - 4 * a * c;
      const double t = disc >= 0 ? (-b - std::sqrt(std::max(disc, 0.0))) / (2 * a) : std::numeric_limits<double>::infinity();
      const bool fin = std::isfinite(t);
      double p[3];
      for (int k = 0; k < 3; ++k) p[k] = o[k] + (fin ? t : 0.0) * v.f[k];
      double nv[3];
      for (int k = 0; k < 3; ++k) nv[k] = (p[k] - centre[k]) / (radii[k] * radii[k]);
      const double nn = std::max(std::sqrt(nv[0] * nv[0] + nv[1] * nv[1] + nv[2] * nv[2]), 1e-12);
      const size_t i = size_t(y) * v.w + x;
      for (int k = 0; k < 3; ++k) hh.n[i * 3 + k] = nv[k] / nn;
      hh.depth[i] = fin ? (p[0] - v.c[0]) * v.f[0] + (p[1] - v.c[1]) * v.f[1] + (p[2] - v.c[2]) * v.f[2]
                        : std::numeric_limits<double>::infinity();
    }
}

// ---- opacity shadow map ----
template <class T>
inline T tau_of(double alpha) {
  const T a = std::min(T(alpha), T(0.999));
  return -std::log1p(-a);
}

// Stored only over the light pixels any fragment touches (elsewhere the reference's rows are 0: transmittance 1).
struct ShadowMap {
  double lo = 0, span = 1;
  int x0 = 0, y0 = 0, rw = 0, rh = 0, w = 0;
  std::vector<double> cum;  // [rh * rw * (slabs + 1)]
};

void shadow_map(const FragList<LFrag>& lf, const View& lv, ShadowMap& sm) {
  constexpr size_t row = kSlabs + 1;
  sm.lo = 0.0;
  sm.span = 1.0;
  sm.w = lv.w;
  sm.rw = sm.rh = 0;
  double lo = std::numeric_limits<double>::infinity(), hi = -lo;
  int x0 = lv.w, x1 = -1, y0 = lv.h, y1 = -1;
  for (const LFrag& f : lf.v) {
    if (f.pix < 0) continue;
    lo = std::min(lo, f.depth);
    hi = std::max(hi, f.depth);
    const int x = f.pix % lv.w, y = f.pix / lv.w;
    x0 = std::min(x0, x); x1 = std::max(x1, x);
    y0 = std::min(y0, y); y1 = std::max(y1, y);
  }
  if (x1 < 0) {
    sm.cum.clear();
    return;
  }
  sm.lo = lo;
  sm.span = std::max(hi - lo, 1e-6);
  sm.x0 = x0; sm.y0 = y0; sm.rw = x1 - x0 + 1; sm.rh = y1 - y0 + 1;
  sm.cum.assign(size_t(sm.rw) * sm.rh * row, 0.0);
  for (const LFrag& f : lf.v) {
    if (f.pix < 0) continue;
    const int64_t sl = std::clamp<int64_t>(int64_t((f.depth - lo) / sm.span * kSlabs), 0, kSlabs - 1);
    const double tau = lf.f32 ? double(tau_of<float>(f.alpha)) : tau_of<double>(f.alpha);
    const size_t cell = size_t(f.pix / lv.w - y0) * sm.rw + size_t(f.pix % lv.w - x0);
    sm.cum[cell * row + sl + 1] += tau;
  }
  const size_t cells = size_t(sm.rw) * sm.rh;
  parallel_for(int(std::min<size_t>(64, cells)), [&](int ci) {
    const int nc = int(std::min<size_t>(64, cells));
    for (size_t p = cells * ci / nc; p < cells * (ci + 1) / nc; ++p) {
      double* c = &sm.cum[p * row];
      for (size_t k = 1; k < row; ++k) c[k] = c[k - 1] + c[k];
    }
  });
}

inline double shadow_at(const ShadowMap& sm, const View& lv, double px, double py, double pz) {
  double x, y, d;
  lv.project(px, py, pz, x, y, d);
  int32_t pix;
  if (!lv.keep(x, y, pix)) return 1.0;
  const int cx = pix % lv.w - sm.x0, cy = pix / lv.w - sm.y0;
  if (cx < 0 || cy < 0 || cx >= sm.rw || cy >= sm.rh) return 1.0;  // exp(-0)
  const double pos = std::clamp((d - sm.lo) / sm.span * kSlabs, 0.0, double(kSlabs));
  const int64_t i = std::clamp<int64_t>(int64_t(std::floor(pos)), 0, kSlabs - 1);
  const double frac = pos - double(i);
  const double* c = &sm.cum[(size_t(cy) * sm.rw + cx) * (kSlabs + 1)];
  return std::exp(-(c[i] * (1 - frac) + c[i + 1] * frac));
}

// render.shade for one fragment.
inline void shade(const double t[3], const double vd[3], const double ld[3], double fwd8, double shadow, double root,
                  double out[3]) {
  const double tl = t[0] * ld[0] + t[1] * ld[1] + t[2] * ld[2];
  const double tv = t[0] * vd[0] + t[1] * vd[1] + t[2] * vd[2];
  const double sin_l = std::sqrt(std::max(0.0, 1 - tl * tl));
  const double sin_v = std::sqrt(std::max(0.0, 1 - tv * tv));
  const double x = std::max(0.0, -tl * tv + sin_l * sin_v);
  const double x2 = x * x, x4 = x2 * x2, x8 = x4 * x4, x32 = (x8 * x8) * (x8 * x8);
  const double spec = (x32 * x8) * 0.35;  // ** 40 (a few ulp from pow)
  const double tt = fwd8 * sin_l * 1.2;
  const double root_dark = 0.75 + 0.25 * root;
  for (int c = 0; c < 3; ++c) {
    const double diffuse = sin_l * kHair[c] * 0.9;
    const double direct = (diffuse + spec + tt * std::sqrt(kHair[c])) * shadow;
    out[c] = (direct * 2.5 + kHair[c] * 0.12) * root_dark;
  }
}

// One fragment in sorted position: its depth and opacity carried along so the sort and the optical depth pass stream.
struct Item {
  double depth, alpha;
  uint32_t idx;
};

struct Scratch {
  std::vector<uint32_t> hist, start;
  std::vector<Item> items;
  std::vector<double> total, tau64, cum64;
  std::vector<float> tau32, cum32;
  ShadowMap sm;
};

// Fragments in front of the head proxy (render: frags.select(frags.depth < hd[frags.pixel])), in the stable order by
// (pixel, depth) of render._fragment_order (= lexsort((depth, pix)), ties by generation order): a stable counting sort
// by pixel with per-chunk histograms, then each pixel's run sorted by (depth, index).
void front_order(const FragList<Frag>& fr, const HeadHit& hh, int npix, Scratch& s) {
  const size_t n = fr.size();
  constexpr int C = 16;  // fixed: the result never depends on the thread count
  s.hist.assign(size_t(C) * npix, 0);
  auto front = [&](const Frag& f) { return f.pix >= 0 && f.depth < hh.depth[f.pix]; };
  parallel_for(C, [&](int c) {
    uint32_t* h = &s.hist[size_t(c) * npix];
    for (size_t k = n * c / C; k < n * (c + 1) / C; ++k)
      if (front(fr.v[k])) ++h[fr.v[k].pix];
  });
  s.start.assign(size_t(npix) + 1, 0);
  uint32_t run = 0;
  for (int p = 0; p < npix; ++p) {
    s.start[p] = run;
    for (int c = 0; c < C; ++c) {
      uint32_t& h = s.hist[size_t(c) * npix + p];
      const uint32_t k = h;
      h = run;
      run += k;
    }
  }
  s.start[npix] = run;
  s.items.resize(run);
  parallel_for(C, [&](int c) {
    uint32_t* h = &s.hist[size_t(c) * npix];
    for (size_t k = n * c / C; k < n * (c + 1) / C; ++k) {
      const Frag& f = fr.v[k];
      if (front(f)) s.items[h[f.pix]++] = Item{f.depth, f.alpha, uint32_t(k)};
    }
  });
  const int chunks = std::max(1, std::min(256, npix / 256));
  parallel_for(chunks, [&](int ci) {
    const int p0 = int(int64_t(npix) * ci / chunks), p1 = int(int64_t(npix) * (ci + 1) / chunks);
    auto less = [](const Item& x, const Item& y) { return x.depth < y.depth || (!(y.depth < x.depth) && x.idx < y.idx); };
    for (int p = p0; p < p1; ++p) {
      Item* b = s.items.data() + s.start[p];
      const size_t m = s.start[p + 1] - s.start[p];
      if (m < 2) continue;
      if (m <= 32) {
        for (size_t i = 1; i < m; ++i) {  // stable insertion (input is in index order)
          const Item key = b[i];
          size_t j = i;
          while (j > 0 && key.depth < b[j - 1].depth) {
            b[j] = b[j - 1];
            --j;
          }
          b[j] = key;
        }
      } else {
        std::sort(b, b + m, less);
      }
    }
  });
}

// render.composite + render.shade + shadow_transmittance in sorted order. numpy takes the optical depth cumsum over
// the whole sorted array in the opacity's precision (float32 for plain cards), so that prefix sum stays sequential;
// the rest runs per pixel in parallel.
template <class T>
void shade_composite(const FragList<Frag>& fr, const View& v, const View& lv, const double ld[3], Scratch& s,
                     std::vector<T>& tau, std::vector<T>& cum, std::vector<double>& image) {
  const size_t n = s.items.size();
  const int npix = v.npix();
  image.assign(size_t(npix) * 3, 0.0);
  s.total.assign(npix, 0.0);
  tau.resize(n);
  cum.resize(n);
  const int chunks = int(std::max<size_t>(1, std::min<size_t>(256, n / 8192)));
  parallel_for(chunks, [&](int ci) {
    for (size_t k = n * ci / chunks; k < n * (ci + 1) / chunks; ++k) tau[k] = tau_of<T>(s.items[k].alpha);
  });
  T run = 0;
  for (size_t k = 0; k < n; ++k) cum[k] = run = run + tau[k];
  const double vd[3] = {-v.f[0], -v.f[1], -v.f[2]};
  const double fwd = std::max(0.0, -(vd[0] * ld[0] + vd[1] * ld[1] + vd[2] * ld[2]));
  const double fwd8 = std::pow(fwd, 8.0);
  const int pchunks = std::max(1, std::min(256, npix / 128));
  parallel_for(pchunks, [&](int ci) {
    const int p0 = int(int64_t(npix) * ci / pchunks), p1 = int(int64_t(npix) * (ci + 1) / pchunks);
    const Item* items = s.items.data();
    const uint32_t kend = s.start[p1];
    for (int p = p0; p < p1; ++p) {
      const uint32_t k0 = s.start[p], k1 = s.start[p + 1];
      if (k0 == k1) continue;
      const T base = cum[k0] - tau[k0];
      double img[3] = {0, 0, 0}, tot = 0;
      for (uint32_t k = k0; k < k1; ++k) {
        if (k + 16 < kend) _mm_prefetch(reinterpret_cast<const char*>(&fr.v[items[k + 16].idx]), _MM_HINT_T0);
        const Frag& f = fr.v[items[k].idx];
        const double sh = shadow_at(s.sm, lv, double(f.world[0]), double(f.world[1]), double(f.world[2]));
        double col[3];
        shade(f.tan, vd, ld, fwd8, sh, f.root, col);
        const T a = std::min(T(items[k].alpha), T(0.999));
        const T before = std::exp(-((cum[k] - tau[k]) - base));
        const double w = double(T(before * a));
        img[0] += w * col[0];
        img[1] += w * col[1];
        img[2] += w * col[2];
        tot += double(tau[k]);
      }
      double* o = &image[size_t(p) * 3];
      o[0] = img[0];
      o[1] = img[1];
      o[2] = img[2];
      s.total[p] = tot;
    }
  });
}

// render.render's coverage only (1 - transmittance) of the fibre cards: plain fragments with their fibre opacity
// (those at 0 are not fibre fragments; they would add 0).
void render_coverage(const FragList<Frag>& fr, const std::vector<double>& fa, const View& v, const HeadHit& hh,
                     std::vector<double>& cov) {
  std::vector<double> total(v.npix(), 0.0);
  for (size_t i = 0; i < fr.size(); ++i) {
    const Frag& f = fr.v[i];
    if (!(fa[i] > 0) || !(f.depth < hh.depth[f.pix])) continue;
    total[f.pix] += tau_of<double>(fa[i]);
  }
  cov.resize(v.npix());
  for (int p = 0; p < v.npix(); ++p) cov[p] = 1 - std::exp(-total[p]);
}

// render.render: radiance image [npix * 3] and coverage [npix].
void render_full(const FragList<LFrag>& lf, const FragList<Frag>& fr, const View& v, const View& lv, const double ld[3],
                 const HeadHit& hh, Scratch& s, std::vector<double>& image, std::vector<double>& cov) {
  shadow_map(lf, lv, s.sm);
  front_order(fr, hh, v.npix(), s);
  if (fr.f32)
    shade_composite<float>(fr, v, lv, ld, s, s.tau32, s.cum32, image);
  else
    shade_composite<double>(fr, v, lv, ld, s, s.tau64, s.cum64, image);
  cov.resize(v.npix());
  const double grey[3] = {0.05, 0.05, 0.06};
  parallel_for(v.h, [&](int y) {
    for (int x = 0; x < v.w; ++x) {
      const size_t p = size_t(y) * v.w + x;
      const double trans = std::exp(-s.total[p]);
      cov[p] = 1 - trans;
      double bg[3] = {grey[0], grey[1], grey[2]};
      const double hd = hh.depth[p];
      if (std::isfinite(hd)) {
        const double* hn = &hh.n[p * 3];
        double o[3];
        pixel_plane(v, x, y, o);
        double surf[3];
        for (int k = 0; k < 3; ++k) surf[k] = o[k] + hd * v.f[k] + hn[k] * 1e-3;
        const double hs = shadow_at(s.sm, lv, surf[0], surf[1], surf[2]);
        const double lam = std::max(0.0, hn[0] * ld[0] + hn[1] * ld[1] + hn[2] * ld[2]);
        for (int k = 0; k < 3; ++k) bg[k] = kSkin[k] * (lam * hs * 2.0 + 0.12);
      }
      for (int k = 0; k < 3; ++k) image[p * 3 + k] += trans * bg[k];
    }
  });
}

struct ViewResult { double iou = 0, coverage = 0, radiance = 0, ratio = 0; };

ViewResult validate_view(const Mesh& m, const CardTex& tex_view, const CardTex& tex_light, const Groom& g,
                         const Head& head, const View& view, const double light[3]) {
  const double ln = std::sqrt(dot3(light, light));
  const double ld[3] = {light[0] / ln, light[1] / ln, light[2] / ln};
  const View lv = light_view_for(view, ld, view.pixel * 2);
  FragList<Frag> cards_v, strands_v;
  FragList<LFrag> cards_l, strands_l;
  std::vector<double> fibre;
  card_fragments(m, tex_view, view, cards_v, &fibre);
  card_fragments(m, tex_light, lv, cards_l, nullptr);
  strand_fragments(g, view, strands_v);
  strand_fragments(g, lv, strands_l);
  HeadHit hh;
  ellipsoid_hit(view, head, hh);
  Scratch s;
  std::vector<double> cov_c, cov_p, cov_g, img_c, img_g;
  render_coverage(cards_v, fibre, view, hh, cov_c);
  render_full(cards_l, cards_v, view, lv, ld, hh, s, img_c, cov_p);
  render_full(strands_l, strands_v, view, lv, ld, hh, s, img_g, cov_g);
  long long inter = 0, uni = 0, n = 0, nr = 0;
  double sum_g = 0, sum_c = 0, sum_ig = 0, sum_ic = 0;
  for (int p = 0; p < view.npix(); ++p) {
    const double c = cov_c[p], gg = cov_g[p], pl = cov_p[p];
    inter += (c > 0.5) && (gg > 0.5);
    uni += (c > 0.5) || (gg > 0.5);
    if (c > 0.02 || gg > 0.02) {
      sum_g += gg;
      sum_c += c;
      ++n;
    }
    if (pl > 0.02 || gg > 0.02) {
      for (int k = 0; k < 3; ++k) {
        sum_ig += img_g[size_t(p) * 3 + k];
        sum_ic += img_c[size_t(p) * 3 + k];
      }
      ++nr;
    }
  }
  ViewResult r;
  r.iou = double(inter) / double(std::max<long long>(uni, 1));
  r.coverage = n ? sum_g / double(n) - sum_c / double(n) : 0.0;
  r.ratio = sum_c > 0 ? sum_g / sum_c : 0.0;   // strand over card opacity: the coverage bound's own quantity (RTX_HAIR_PRIMARY 5u)
  r.radiance = nr ? (sum_ig / double(3 * nr)) / std::max(sum_ic / double(3 * nr), 1e-9) - 1 : 0.0;
  return r;
}

}  // namespace

Validation validate_full(const Mesh& m, const Mask& mask, const Groom& g, const Head& head, int threads) {
  Validation out;
  const uint32_t V = m.vertices();
  if (!V) return out;
  // batch.bbox_views(positions, 0.002)
  const double pixel = 0.002;
  double lo[3], hi[3];
  for (int k = 0; k < 3; ++k) lo[k] = hi[k] = m.pos[k];
  for (uint32_t i = 0; i < V; ++i)
    for (int k = 0; k < 3; ++k) {
      lo[k] = std::min(lo[k], double(m.pos[i * 3 + k]));
      hi[k] = std::max(hi[k], double(m.pos[i * 3 + k]));
    }
  double c[3], ext[3];
  for (int k = 0; k < 3; ++k) {
    c[k] = (lo[k] + hi[k]) / 2;
    ext[k] = hi[k] - lo[k];
  }
  const int w = int(std::max(ext[0], ext[2]) * 1.3 / pixel) + 8, h = int(ext[1] * 1.15 / pixel) + 8;
  const double up[3] = {0.0, 1.0, 0.0};
  const double fwd[3][3] = {{0, 0, 1.0}, {-1.0, 0, 0}, {0, 0, -1.0}};
  const double light[3][3] = {{0.4, 0.5, -1.0}, {0.8, 0.6, -0.3}, {-0.3, 0.6, 1.0}};
  ViewResult res[3];
  // Views are independent tasks; their stages split further on the same pool.
  Pool pool(std::max(1, threads));
  Pool* const outer = tl_pool_ref();
  tl_pool_ref() = &pool;
  CardTex tex[2];
  parallel_for(2, [&](int k) { tex[k] = card_texture(mask, pixel * (k + 1)); });
  parallel_for(3, [&](int k) {
    const View view = make_view(c, fwd[k], up, pixel, w, h);
    res[k] = validate_view(m, tex[0], tex[1], g, head, view, light[k]);
  });
  tl_pool_ref() = outer;
  for (const ViewResult& r : res) {
    out.iou.push_back(r.iou);
    out.coverage.push_back(r.coverage);
    out.ratio.push_back(r.ratio);
    out.radiance.push_back(r.radiance);
  }
  return out;
}

}  // namespace hairconv
