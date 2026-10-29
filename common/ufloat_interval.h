#ifndef BG3_UFLOAT_INTERVAL_H
#define BG3_UFLOAT_INTERVAL_H
// Diagnostic preimage of an unsigned 5-exponent-bit float storage conversion.
// Vulkan Floating-Point Format Conversions permits either adjacent finite value:
// https://registry.khronos.org/vulkan/specs/latest/html/vkspec.html#fundamentals-fp-conversion
// Consequently the open interval between neighbours is conservative; no assumed
// nearest rounding, empirical percentage, or allowance for BRDF error is added.
#ifdef __cplusplus
#include <bit>
#include <cmath>
#include <cstdint>
namespace bg3 {
using uint = uint32_t;
inline uint uf_bits(float x) { return std::bit_cast<uint>(x); }
inline float uf_float(uint x) { return std::bit_cast<float>(x); }
inline bool uf_finite(float x) { return std::isfinite(x); }
#else
uint uf_bits(float x) { return asuint(x); }
float uf_float(uint x) { return asfloat(x); }
bool uf_finite(float x) { return isfinite(x); }
#endif
struct UfInterval {
  uint status; // 0 invalid/nonrepresentable native value, 1 bounded, 2 finite maximum (unbounded)
  float lower;
  float upper;
};
float uf_decode(uint code, uint mantissa) {
  uint exponent = code >> mantissa;
  uint fraction = code & ((1u << mantissa) - 1u);
  if (exponent == 0u) return float(fraction) * uf_float((113u - mantissa) << 23u);
  return uf_float(((exponent + 112u) << 23u) | (fraction << (23u - mantissa)));
}
UfInterval uf_interval(float native_value, uint mantissa) {
  UfInterval r;
  r.status = 0u; r.lower = 0.0; r.upper = 0.0;
  if ((mantissa != 5u && mantissa != 6u) || !uf_finite(native_value) || native_value < 0.0) return r;
  uint max_code = (31u << mantissa) - 1u;
  if (native_value > uf_decode(max_code, mantissa)) return r;
  uint code;
  if (native_value < uf_float(113u << 23u)) {
    float units = native_value / uf_float((113u - mantissa) << 23u);
    code = uint(units);
    if (float(code) != units) return r;
  } else {
    uint bits = uf_bits(native_value);
    if ((bits & ((1u << (23u - mantissa)) - 1u)) != 0u) return r;
    code = (((bits >> 23u) - 112u) << mantissa) | ((bits & 0x7fffffu) >> (23u - mantissa));
  }
  r.lower = code == 0u ? 0.0f : uf_decode(code - 1u, mantissa);
  if (code == max_code) { r.status = 2u; return r; }
  r.upper = uf_decode(code + 1u, mantissa);
  r.status = 1u;
  return r;
}
bool uf_contains(UfInterval r, float native_value, float candidate) {
  // Negative estimates are audited as invalid even though unsigned storage can
  // clamp them to zero. Finite-maximum saturation is inconclusive, never a pass.
  return r.status == 1u && uf_finite(candidate) && candidate >= 0.0 &&
         (candidate > r.lower || (native_value == 0.0 && candidate == 0.0)) && candidate < r.upper;
}
#ifdef __cplusplus
} // namespace bg3
#endif
#endif
