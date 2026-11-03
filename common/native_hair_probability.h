#pragma once
// Shared empirical hard-coverage estimator and 24-bit Bernoulli decision.
// An invalid estimate keeps the shader on full native coverage. This conserves
// the stored scalar probability, not spatial/directional scene energy.
#ifdef __cplusplus
#include <cstdint>
namespace bg3::native_hair_probability {
#define NHP_INLINE inline
#define NHP_UINT uint32_t
#else
#define NHP_INLINE
#define NHP_UINT uint
#endif
// One 64-lane workgroup's candidate budget per draw. The quadrature already
// samples 64 UV sites; strand discovery must never grow with mesh complexity.
NHP_INLINE NHP_UINT nh_scan_count(NHP_UINT tris) { return tris < 64u ? tris : 64u; }
// Two trailing words: probability and a per-draw capped-thread counter (0/1).
// Subtract before multiplying: malicious offsets/counts cannot wrap to a neighbour.
NHP_INLINE bool nh_mean_range(NHP_UINT base, NHP_UINT allocation, NHP_UINT tris,
                              NHP_UINT owned_tris, NHP_UINT pool_bytes) {
  return tris != 0u && tris == owned_tris && (base & 3u) == 0u &&
         base <= pool_bytes && allocation <= pool_bytes - base &&
         allocation >= 8u && tris <= (allocation - 8u) / 24u;
}
NHP_INLINE float nh_probability(NHP_UINT hits, NHP_UINT trials) {
  if (trials == 0u || hits > trials) return -1.0f;
  return float(hits) / float(trials);
}
NHP_INLINE bool nh_bernoulli(float probability, NHP_UINT random24) {
  return probability >= 0.0f && probability <= 1.0f && random24 <= 0xffffffu &&
         float(random24) * (1.0f / 16777216.0f) < probability;
}
// Single candidate decision: 2 means full native fallback, never retry the
// probability until accepted. Comparisons reject NaN and infinities as invalid.
NHP_INLINE NHP_UINT nh_consume(float probability, NHP_UINT random24) {
  if (!(probability >= 0.0f && probability <= 1.0f) || random24 > 0xffffffu) return 2u;
  return nh_bernoulli(probability, random24) ? 1u : 0u;
}
#ifdef __cplusplus
}
#endif
#undef NHP_INLINE
#undef NHP_UINT
