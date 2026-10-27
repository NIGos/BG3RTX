// Unit tests for common/light_tree.h (the CPU light BVH and its reference sampler,
// docs/DESIGN_SOTA_NEXT.md item 1):
//   - the GPU node layout;
//   - the probabilities of all lights sum to 1 at every receiver some light reaches;
//   - pmf() agrees with sample() (the probability returned) and with sampled frequencies;
//   - conservativeness: every light that contributes at a receiver has pmf > 0 (else the
//     estimator would be biased);
//   - the estimator sum_i f_i / pmf_i over samples is unbiased against brute force;
//   - degenerate inputs (one light, coincident lights, zero power).
//   bin/light_tree_test.exe   (exit code = failures)
#include "../common/light_tree.h"

#include <cstdio>
#include <random>

using namespace bg3::lt;

static int g_failed = 0, g_checks = 0;
#define CHECK(cond)                                                \
  do {                                                             \
    ++g_checks;                                                    \
    if (!(cond)) {                                                 \
      ++g_failed;                                                  \
      std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);  \
    }                                                              \
  } while (0)

// The exact contribution of a light at (p, n), unshadowed: BG3-like point/spot with a range,
// pow(1 - (d/R)^2, 2) falloff, 1/d^2 omitted (falloff dominates), N.L clamp. Positive exactly
// where the light can contribute.
static float contribution(const Light& l, V3 p, V3 n) {
  V3 d = l.pos - p;
  float dist = length(d);
  if (dist >= l.range || dist <= 0) return 0;
  V3 w = d * (1.0f / dist);
  float nl = dot(n, w);
  if (nl <= 0) return 0;
  if (l.cos_o > 0.5f && dot(normalize(l.axis), w * -1.0f) <= l.cos_e) return 0;   // spot cone
  float x = dist / l.range;
  float f = (1 - x * x) * (1 - x * x);
  return l.power * f * nl / (dist * dist + 0.01f);
}

static std::vector<Light> random_lights(std::mt19937& rng, int count, float world, float spot_share) {
  std::uniform_real_distribution<float> U(0, 1);
  std::vector<Light> L;
  for (int i = 0; i < count; ++i) {
    Light l;
    l.pos = v3(U(rng) * world, U(rng) * world * 0.2f, U(rng) * world);
    l.range = 2 + U(rng) * 14;
    l.power = 0.05f + U(rng) * U(rng) * 50;
    l.id = 1000 + i;
    if (U(rng) < spot_share) {
      l.axis = normalize(v3(U(rng) - 0.5f, -1.0f, U(rng) - 0.5f));
      l.cos_o = 1;
      l.cos_e = std::cos(0.3f + U(rng) * 0.9f);
    }
    L.push_back(l);
  }
  return L;
}

static void test_layout() {
  CHECK(sizeof(LightTreeNodeGpu) == 64);
  LightTree t;
  t.build({});
  CHECK(t.empty());
  uint32_t li;
  float pmf;
  CHECK(!t.sample(v3(0, 0, 0), v3(0, 1, 0), 0.5f, &li, &pmf));
}

static void test_single_and_degenerate() {
  LightTree t;
  Light a;
  a.pos = v3(0, 2, 0);
  a.range = 5;
  a.power = 3;
  a.id = 7;
  t.build({a});
  uint32_t li = 99;
  float pmf = 0;
  CHECK(t.sample(v3(0, 0, 0), v3(0, 1, 0), 0.3f, &li, &pmf) && li == 0 && pmf == 1.0f);
  CHECK(t.pmf(0, v3(0, 0, 0), v3(0, 1, 0)) == 1.0f);
  CHECK(t.pmf(0, v3(0, 0, 0), v3(0, -1, 0)) == 0.0f);   // below the hemisphere
  CHECK(t.pmf(0, v3(100, 0, 0), v3(0, 1, 0)) == 0.0f);  // out of range
  CHECK(t.index_of_id(7) == 0 && t.index_of_id(8) == UINT32_MAX);

  // Coincident lights and a zero-power light.
  std::vector<Light> L(9, a);
  for (uint32_t i = 0; i < L.size(); ++i) L[i].id = i;
  L[4].power = 0;
  t.build(L);
  CHECK(t.lights().size() == 8);
  CHECK(t.index_of_id(4) == UINT32_MAX);
  float sum = 0;
  for (uint32_t i = 0; i < t.lights().size(); ++i) sum += t.pmf(i, v3(0, 0, 0), v3(0, 1, 0));
  CHECK(std::abs(sum - 1) < 1e-5f);
}

static void test_random_scene(uint32_t seed, int count, float spot_share) {
  std::mt19937 rng(seed);
  std::uniform_real_distribution<float> U(0, 1);
  const float world = 60;
  std::vector<Light> L = random_lights(rng, count, world, spot_share);
  LightTree t;
  t.build(L);
  CHECK(!t.empty());
  CHECK(t.lights().size() == L.size());
  CHECK(t.depth() < 40);
  const auto& TL = t.lights();
  int receivers = 0, sum_bad = 0, support_bad = 0, sample_bad = 0;
  double worst_bias = 0, psum_min = 1, rel_err_sum = 0;
  for (int r = 0; r < 300; ++r) {
    V3 p = v3(U(rng) * world, U(rng) * world * 0.2f, U(rng) * world);
    V3 n = normalize(v3(U(rng) - 0.5f, U(rng) * 0.5f + 0.2f, U(rng) - 0.5f));
    double truth = 0;
    double psum = 0;
    for (uint32_t i = 0; i < TL.size(); ++i) {
      float f = contribution(TL[i], p, n);
      float pm = t.pmf(i, p, n);
      truth += f;
      psum += pm;
      if (f > 0 && !(pm > 0)) ++support_bad;
    }
    if (truth <= 0) continue;
    ++receivers;
    // The descent can stop where both children are 0 (no light below can contribute): the
    // probabilities then sum to less than 1, never more, and never to 0 while a light contributes.
    if (psum > 1 + 1e-4 || !(psum > 0)) ++sum_bad;
    psum_min = std::min(psum_min, psum);
    // sample() returns the same probability as pmf(); the one-sample estimator f/pmf is unbiased.
    const int S = 4000;
    double est = 0, est2 = 0;
    for (int s = 0; s < S; ++s) {
      uint32_t li;
      float pm;
      if (!t.sample(p, n, U(rng), &li, &pm)) continue;
      if (std::abs(pm - t.pmf(li, p, n)) > 1e-5f * std::max(1.0f, pm)) ++sample_bad;
      double x = contribution(TL[li], p, n) / pm;
      est += x;
      est2 += x * x;
    }
    est /= S;
    // |mean - truth| in standard errors of the mean (a biased sampler drifts by many).
    double se = std::sqrt(std::max(est2 / S - est * est, 0.0) / S);
    worst_bias = std::max(worst_bias, std::abs(est - truth) / std::max(se, 1e-12 * truth));
    rel_err_sum += std::abs(est - truth) / truth;
  }
  std::printf("  scene seed %u: %d lights (%.0f%% spots), depth %u, %d receivers lit; worst |est-truth| %.2f standard errors, "
              "mean relative error %.3f (4000 samples), smallest probability sum %.3f\n",
              seed, count, spot_share * 100, t.depth(), receivers, worst_bias, rel_err_sum / std::max(receivers, 1), psum_min);
  CHECK(receivers > 50);
  CHECK(sum_bad == 0);
  CHECK(support_bad == 0);
  CHECK(sample_bad == 0);
  CHECK(worst_bias < 5.5);   // 300 receivers: 5.5 sigma has a negligible false-alarm rate
}

// Sampled frequencies follow pmf().
static void test_frequencies() {
  std::mt19937 rng(42);
  std::uniform_real_distribution<float> U(0, 1);
  std::vector<Light> L = random_lights(rng, 24, 12, 0.3f);
  LightTree t;
  t.build(L);
  V3 p = v3(6, 1, 6), n = v3(0, 1, 0);
  std::vector<int> hist(t.lights().size(), 0);
  const int S = 400000;
  for (int s = 0; s < S; ++s) {
    uint32_t li;
    float pm;
    if (t.sample(p, n, U(rng), &li, &pm)) ++hist[li];
  }
  double worst = 0;
  for (uint32_t i = 0; i < hist.size(); ++i) {
    double expect = double(t.pmf(i, p, n)) * S;
    double sigma = std::sqrt(std::max(expect, 1.0));
    worst = std::max(worst, std::abs(hist[i] - expect) / sigma);
    if (expect == 0) CHECK(hist[i] == 0);
  }
  std::printf("  frequencies: worst deviation %.2f sigma over %zu lights\n", worst, hist.size());
  CHECK(worst < 5.0);
}

// Importance is never larger for a farther copy of the same light (sanity of the bound).
static void test_importance_monotonic() {
  Light a;
  a.pos = v3(0, 3, 0);
  a.range = 50;
  a.power = 1;
  Bounds b = bounds_of(a);
  float prev = 1e30f;
  bool ok = true;
  for (float x = 0; x < 40; x += 1) {
    float i = importance(b, v3(x, 0, 0), v3(0, 0, 0));
    ok = ok && i <= prev + 1e-6f;
    prev = i;
  }
  CHECK(ok);
  // A spot pointing away from the receiver has importance 0.
  a.axis = v3(0, 1, 0);
  a.cos_o = 1;
  a.cos_e = std::cos(0.5f);
  CHECK(importance(bounds_of(a), v3(0, 0, 0), v3(0, 1, 0)) == 0);
  a.axis = v3(0, -1, 0);
  CHECK(importance(bounds_of(a), v3(0, 0, 0), v3(0, 1, 0)) > 0);
}

static void test_cone_union() {
  Cone a{v3(0, 0, 1), std::cos(0.1f), false}, b{v3(1, 0, 0), std::cos(0.1f), false};
  Cone u = cone_union(a, b);
  // Both axes inside the union.
  CHECK(dot(u.w, a.w) >= u.cos_t - 1e-5f && dot(u.w, b.w) >= u.cos_t - 1e-5f);
  Cone c{v3(0, 0, -1), std::cos(0.1f), false};
  CHECK(cone_union(a, c).cos_t == -1);   // opposite: the whole sphere
}

int main() {
  std::printf("light_tree_test\n");
  test_layout();
  test_single_and_degenerate();
  test_cone_union();
  test_importance_monotonic();
  test_frequencies();
  test_random_scene(1, 64, 0.0f);
  test_random_scene(2, 400, 0.25f);
  test_random_scene(3, 1024, 0.1f);
  std::printf("%d checks, %d failed\n", g_checks, g_failed);
  return g_failed;
}
