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
NHP_INLINE float nh_probability(NHP_UINT hits, NHP_UINT trials) {
  if (trials == 0u || hits > trials) return -1.0f;
  return float(hits) / float(trials);
}
NHP_INLINE bool nh_bernoulli(float probability, NHP_UINT random24) {
  return probability >= 0.0f && probability <= 1.0f && random24 <= 0xffffffu &&
         float(random24) * (1.0f / 16777216.0f) < probability;
}
#ifdef __cplusplus
}
#endif
#undef NHP_INLINE
#undef NHP_UINT
