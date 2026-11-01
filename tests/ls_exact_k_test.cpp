// common/ls_exact_k.h (D-LS exact-k + RIS remainder, ls_k; docs/TRACK_D_LOCAL_LIGHTS.md 9.11): the
// selection the LS_K shader variant runs, compiled as C++.
//   - the exact set is the K largest weights, in list order;
//   - each remainder sample picks light j with probability w_j / sum_R w (midpoint grid over the
//     uniform; the grid error bound is derived below), so the estimator's mean is the exact sum;
//   - a sample's scaled weight is at most sum_R w (no fireflies);
//   - the push word and the variant rule core/gi.cpp uses.
#include "../common/ls_exact_k.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <vector>
using namespace bg3::ls_exact_k;
static int checks = 0, failures = 0;
#define CHECK(x) do { ++checks; if (!(x)) { ++failures; std::printf("FAIL line %d: %s\n", __LINE__, #x); } } while (0)

static LkSel select(const std::vector<float>& w, LkUint K, LkUint NR, float u0, float u1) {
  LkSel s = lk_begin(u0, u1);
  for (LkUint i = 0; i < w.size(); ++i)
    if (w[i] > 0.0f) s = lk_add(s, w[i], 100u + i, K);   // entries: list positions 100.. (not 0-based)
  return lk_end(s, NR);
}

int main() {
  // Push word and variant rule.
  CHECK(kmode(0, 1) == 0x10u); CHECK(kmode(1, 1) == 0x11u); CHECK(kmode(9, 7) == 0x24u); CHECK(kmode(-3, -1) == 0u);
  CHECK(lk_k(kmode(2, 1)) == 2u && lk_rest(kmode(2, 1)) == 1u);
  CHECK(lk_k(0xFFu) == 4u && lk_rest(0xFFu) == 2u);
  CHECK(!use_variant(0, 0, 0, 0, 1, false));    // ls_k=0: the every-light pass, no LS_K pipeline
  CHECK(use_variant(1, 0, 0, 0, 1, false));
  CHECK(!use_variant(1, 1, 0, 0, 1, false));    // RIS sampling (ls_samples)
  CHECK(!use_variant(1, 0, 4, 0, 1, false));    // debug views
  CHECK(!use_variant(1, 0, 0, 1, 1, false));    // ls_check against the frozen reference
  CHECK(!use_variant(1, 0, 0, 0, 0, false));    // ls_impl=0 (the frozen reference)
  CHECK(!use_variant(1, 0, 0, 0, 1, true));     // native-direct validation pipelines

  // Light sets: distinct weights (no ties at the top-K boundary), zeros (no candidate), list order not by weight.
  const std::vector<std::vector<float>> sets = {
      {0.5f, 3.0f, 0.25f, 1.5f, 0.0f, 2.0f, 0.125f},
      {7.0f, 0.01f, 0.02f, 0.03f, 5.0f},
      {1.0f},
      {0.2f, 0.4f},
      {4.0f, 1.0f, 9.0f, 0.5f, 0.75f, 3.0f, 2.5f, 6.0f, 0.3f}};
  // Midpoint grid over u (N cells). Each reservoir decision rescales u; the k-th decision's boundary is
  // resolved to within one cell of the (k-1)-times rescaled uniform, so a probability over |R| decisions is
  // off by at most |R| / N, plus FP32 rounding (|R| x 2^-23, far below). Bound: (|R| + 1) / N.
  const int N = 1 << 16;
  for (const auto& w : sets) {
    for (LkUint K = 1; K <= 4; ++K) {
      std::vector<std::pair<float, LkUint>> sorted;
      for (LkUint i = 0; i < w.size(); ++i) if (w[i] > 0) sorted.push_back({w[i], 100u + i});
      std::sort(sorted.begin(), sorted.end(), [](auto a, auto b) { return a.first > b.first; });
      std::vector<LkUint> exact;
      double rsum = 0, total = 0;
      for (size_t i = 0; i < sorted.size(); ++i) {
        total += sorted[i].first;
        if (i < K) exact.push_back(sorted[i].second); else rsum += sorted[i].first;
      }
      std::sort(exact.begin(), exact.end());
      const size_t nR = sorted.size() > K ? sorted.size() - K : 0;
      // Deterministic part: the exact set (list order, empty slots last), the remainder sum.
      const LkSel s0 = select(w, K, 1, 0.5f, 0.5f);
      bool ok = s0.n == sorted.size() && std::abs(s0.rsum - rsum) <= 1e-6 * (rsum + 1);
      for (LkUint q = 0; q < 4; ++q) ok &= lk_entry(s0, q) == (q < exact.size() ? exact[q] : kLkNone);
      CHECK(ok);
      for (LkUint NR = 0; NR <= 2; ++NR) {
        // Estimator over the grid (per sample slot): E[sum_E w + (1/NR) sum_samples w_j * factor_j] == total
        // for NR > 0; NR = 0 drops R (biased by design: the exact part only).
        std::vector<double> freq(w.size(), 0.0);
        double mean = 0, worst_scaled = 0;
        for (int g = 0; g < N; ++g) {
          const float u = (float(g) + 0.5f) / float(N);
          const float v = float((g * 40503u) % N + 0.5) / float(N);   // second reservoir: an independent permutation of the grid
          const LkSel s = select(w, K, NR, u, v);
          double est = 0;
          for (LkUint q = 0; q < 6; ++q) {
            const LkUint e = lk_entry(s, q);
            if (e == kLkNone) continue;
            const double we = w[e - 100u], f = lk_factor(s, q, NR);
            est += we * f;
            if (q >= 4) {
              worst_scaled = std::max(worst_scaled, we * f * NR);
              if (q == 4) freq[e - 100u] += 1.0 / N;
            }
          }
          mean += est / N;
        }
        const double want = NR == 0 ? total - rsum : total;
        const double bound = (double(nR) + 1) / N * rsum + 1e-6 * total;
        if (std::abs(mean - want) > bound)
          std::printf("  set of %zu, K %u, NR %u: mean %.9f want %.9f (bound %.2e)\n", w.size(), K, NR, mean, want, bound);
        CHECK(std::abs(mean - want) <= bound);
        CHECK(worst_scaled <= rsum * (1 + 1e-6) + 1e-12);   // a sample never carries more than sum_R w
        if (NR > 0) {
          for (LkUint i = 0; i < w.size(); ++i) {
            const bool inR = w[i] > 0 && std::find(exact.begin(), exact.end(), 100u + i) == exact.end();
            const double p = inR ? w[i] / rsum : 0.0;
            if (std::abs(freq[i] - p) > (double(nR) + 1) / N) std::printf("  light %u: freq %.6f p %.6f\n", i, freq[i], p);
            CHECK(std::abs(freq[i] - p) <= (double(nR) + 1) / N);
          }
        }
        if (NR < 2) { const LkSel s = select(w, K, NR, 0.3f, 0.7f); CHECK(lk_entry(s, 5) == kLkNone); }
        if (nR == 0) { const LkSel s = select(w, K, NR, 0.3f, 0.7f); CHECK(lk_entry(s, 4) == kLkNone); }
      }
    }
  }
  std::printf("ls_exact_k_test: %d checks, %d failed\n", checks, failures);
  return failures == 0 ? 0 : 1;
}
