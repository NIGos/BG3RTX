#ifndef BG3_LS_EXACT_K_H
#define BG3_LS_EXACT_K_H
// D-LS exact-k + RIS remainder (ls_k, ls_rest_samples; docs/TRACK_D_LOCAL_LIGHTS.md 9.11).
// Shared by the shader variant (gi/rt_local_shadows_k*.hlsl, LS_K=1), core/gi.cpp and the tests
// (tests/ls_exact_k_test.cpp, tools/native_hit_gpu_fixture case 8), so the selection the CTest
// checks is the code the GPU runs.
//
// The traced lights of a pixel are split deterministically by their weight w_i (the exact
// unshadowed luminance lum(c_d + c_s)): the K largest (E) get their own ray; from the rest (R),
// NR lights are drawn with p_j = w_j / sum_R w (with replacement, one streaming reservoir per
// sample, one uniform each, rescaled after every decision). Each sample adds its term x
// sum_R w / (NR w_j). The estimator sum_E c_i + (1/NR) sum_samples c_j / p_j is unbiased
// (p_j > 0 wherever w_j > 0), and a sample's scaled luminance is at most sum_R w: no fireflies.
#ifdef __cplusplus
#include <cstdint>
namespace bg3::ls_exact_k {
using LkUint = uint32_t;
struct LkF4 {float v[4]; float& operator[](LkUint i){return v[i];} float operator[](LkUint i) const {return v[i];}};
struct LkU4 {LkUint v[4]; LkUint& operator[](LkUint i){return v[i];} LkUint operator[](LkUint i) const {return v[i];}};
struct LkF2 {float v[2]; float& operator[](LkUint i){return v[i];} float operator[](LkUint i) const {return v[i];}};
struct LkU2 {LkUint v[2]; LkUint& operator[](LkUint i){return v[i];} LkUint operator[](LkUint i) const {return v[i];}};
inline float lk_min(float a, float b) { return a < b ? a : b; }
inline float lk_max(float a, float b) { return a > b ? a : b; }
inline LkUint lk_umin(LkUint a, LkUint b) { return a < b ? a : b; }
inline LkUint lk_umax(LkUint a, LkUint b) { return a > b ? a : b; }
#define LK_UNROLL
#else
#define LkUint uint
#define LkF4 float4
#define LkU4 uint4
#define LkF2 float2
#define LkU2 uint2
#define lk_min min
#define lk_max max
#define lk_umin min
#define lk_umax max
#define LK_UNROLL [unroll]
#endif

static const LkUint kLkNone = 0xFFFFFFFFu;
static const LkUint kLkMaxK = 4u, kLkMaxRest = 2u;
// rtStats counters (core/gi.cpp kLsKFirst; "ls exact-k stats", ls_stats=1): candidates (what
// ls_k=0 traces), px over the cap, remainder lights, remainder rays, their occluded ones, the
// remainder's share of the weight on capped px (x 256).
static const LkUint kLkStatFirst = 350u, kLkStatCount = 6u;

// Push::ls_kmode of the D-LS dispatch: bits 0-3 K (0 off, at most 4), bits 4-7 NR (at most 2).
inline LkUint lk_k(LkUint kmode) { return lk_umin(kmode & 15u, kLkMaxK); }
inline LkUint lk_rest(LkUint kmode) { return lk_umin((kmode >> 4) & 15u, kLkMaxRest); }

struct LkSel {
  LkF4 ew;   // the exact set's weights, descending (0: empty)
  LkU4 ex;   // ... their entries (list positions; kLkNone: empty); after lk_end in list order
  LkF2 rw;   // the remainder samples' weights
  LkU2 rs;   // ... and entries
  LkF2 u;    // the two reservoirs' uniforms
  float rsum;   // sum of w over R
  LkUint n;     // candidates (w > 0)
};

inline LkSel lk_begin(float u0, float u1) {
  LkSel s;
  for (LkUint i = 0; i < 4u; ++i) { s.ew[i] = 0.0f; s.ex[i] = kLkNone; }
  for (LkUint j = 0; j < 2u; ++j) { s.rw[j] = 0.0f; s.rs[j] = kLkNone; }
  s.u[0] = u0; s.u[1] = u1; s.rsum = 0.0f; s.n = 0u;
  return s;
}

// One reservoir decision: take with probability p; the uniform is rescaled to stay uniform.
inline float lk_rescale(float u, float p, bool take) {
  return lk_min(take ? u / p : (u - p) / lk_max(1.0f - p, 1e-30f), 0.99999994f);
}

// Light `e` with weight w (callers pass only finite w > 0). K: the exact-set size.
inline LkSel lk_add(LkSel s, float w, LkUint e, LkUint K) {
  s.n += 1u;
  float cw = w;
  LkUint ce = e;
  LK_UNROLL for (LkUint k = 0; k < 4u; ++k) {   // running top-K by weight (insertion)
    if (k < K && cw > s.ew[k]) {
      const float tw = s.ew[k];
      const LkUint te = s.ex[k];
      s.ew[k] = cw; s.ex[k] = ce;
      cw = tw; ce = te;
    }
  }
  if (cw > 0.0f) {   // ce is (now) in R: both reservoirs see it
    s.rsum += cw;
    const float p = cw / s.rsum;
    LK_UNROLL for (LkUint j = 0; j < 2u; ++j) {
      const bool take = s.u[j] < p;
      s.u[j] = lk_rescale(s.u[j], p, take);
      if (take) { s.rw[j] = cw; s.rs[j] = ce; }
    }
  }
  return s;
}

// After the last light: keep NR samples (0 drops R: biased, timing and debug only) and put the
// exact set in list order (empty slots last), so that neighbours with the same set but another
// ranking trace the same light in the same iteration (coherent rays, as the ls_k=0 loop).
inline LkSel lk_end(LkSel s, LkUint NR) {
  if (NR == 0u) { s.rs[0] = kLkNone; s.rs[1] = kLkNone; }
  else if (NR == 1u) s.rs[1] = kLkNone;
  const LkUint a = s.ex[0], b = s.ex[1], c = s.ex[2], d = s.ex[3];
  const LkUint a1 = lk_umin(a, b), b1 = lk_umax(a, b), c1 = lk_umin(c, d), d1 = lk_umax(c, d);
  const LkUint a2 = lk_umin(a1, c1), c2 = lk_umax(a1, c1), b2 = lk_umin(b1, d1), d2 = lk_umax(b1, d1);
  s.ex[0] = a2; s.ex[1] = lk_umin(b2, c2); s.ex[2] = lk_umax(b2, c2); s.ex[3] = d2;
  return s;
}

// Selection slot q: 0-3 the exact lights, 4-5 the remainder samples (kLkNone: empty).
inline LkUint lk_entry(LkSel s, LkUint q) { return q < 4u ? s.ex[q] : s.rs[q - 4u]; }
// The slot's weight in the estimator: 1 for an exact light, sum_R w / (NR w_j) for a sample.
inline float lk_factor(LkSel s, LkUint q, LkUint NR) {
  return q < 4u ? 1.0f : s.rsum / (s.rw[q - 4u] * float(NR));
}

#ifdef __cplusplus
// core/gi.cpp: the Push::ls_kmode word from the settings (ls_k clamped to 0-4, ls_rest_samples to 0-2).
inline LkUint kmode(int k, int rest) {
  const LkUint kk = LkUint(k < 0 ? 0 : k > 4 ? 4 : k), rr = LkUint(rest < 0 ? 0 : rest > 2 ? 2 : rest);
  return kk | (rr << 4);
}
// Whether the D-LS dispatch runs the exact-k variant: ls_k > 0 on the optimised pass (ls_impl=1), not
// under RIS sampling (ls_samples), a debug view, the frozen-reference check (ls_check) or the
// native-direct validation pipelines. ls_stats keeps it (the FULL variant has its counters).
inline bool use_variant(int k, int samples, int debug, int check, int impl, bool native_validation) {
  return k > 0 && samples == 0 && debug == 0 && check == 0 && impl != 0 && !native_validation;
}
}  // namespace bg3::ls_exact_k
#else
#undef LkUint
#undef LkF4
#undef LkU4
#undef LkF2
#undef LkU2
#undef lk_min
#undef lk_max
#undef lk_umin
#undef lk_umax
#undef LK_UNROLL
#endif
#endif
