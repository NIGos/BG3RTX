#pragma once

// Native CHAR_Scalp scalar contract. The opt-in runtime hard branch uses its
// bounded capture adapter; camera raster dither remains CPU-reference only.
// Inputs are the native sampler's already-filtered mask B/A and native draw
// state. This does not replace texture sampling, derivatives or rasterization.
// Source: verified Vulkan modules listed below, Materials.pak (2026-10-26 DB).
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace bg3::native_scalp {
enum class Pass { CameraDepth, Shadow, ShadowTetrahedron };
struct Recipe {
  uint64_t hash;
  Pass pass;
  uint8_t material_binding, mask_binding;
  // All six: mask/material set 1, sampler set 0 binding 7, UV location 0.
  // Material scalar offsets 0 (smoothstep edge), 4 (root multiplier).
  // CameraDepth additionally reads Frame set 0 binding 0 offsets 8/12.
};
inline constexpr Recipe recipes[] = {
    {0x9e77575a9f377ec5ull, Pass::CameraDepth, 1, 2},
    {0x36eeb96ba83ea027ull, Pass::CameraDepth, 0, 1},
    {0x03a2d9f645625c44ull, Pass::Shadow, 1, 2},
    {0x4096aa06e414de92ull, Pass::Shadow, 0, 1},
    {0xb1b923f0662a55b5ull, Pass::ShadowTetrahedron, 1, 2},
    {0x3535fd5f461f7d23ull, Pass::ShadowTetrahedron, 0, 1},
};
inline const Recipe* lookup(uint64_t hash) {
  for (const auto& r : recipes) if (r.hash == hash) return &r;
  return nullptr;
}
enum class Status { CompleteScalar, MissingInputs, Nonfinite, UndefinedSmoothstep, UnsupportedShader };
struct Material { float edge{}, root{}; bool available{}; };
struct Mask { float blue{}, alpha{}; bool available{}; };
struct Frame { float phase{}, dither_mode{}, frag_x{}, frag_y{}; bool available{}, coordinates_available{}; };
struct Captured { Material material; Frame frame; };
// Portable adapter for layer read_bound-style readers. The read must address
// the SAME draw's binding and dynamic offset. No unbounded/full UBO copying.
// read(set,binding,byte_offset,dst,byte_count) returns false if not resident.
// Coordinates deliberately remain unavailable until original raster projection
// and viewport/sample location have been established by the caller.
template<class Read>
inline bool capture(uint64_t hash, Read&& read, Captured& out) {
  out = {};
  const auto* recipe = lookup(hash);
  if (!recipe) return false;
  float material[2]{};
  if (!read(1u, recipe->material_binding, 0u, material, sizeof(material))) return false;
  if (!std::isfinite(material[0]) || !std::isfinite(material[1])) return false;
  float frame[2]{};
  if (recipe->pass == Pass::CameraDepth) {
    if (!read(0u, 0u, 8u, frame, sizeof(frame)) || !std::isfinite(frame[0]) || !std::isfinite(frame[1])) return false;
  }
  out.material = {material[0], material[1], true};
  if (recipe->pass == Pass::CameraDepth) out.frame = {frame[0], frame[1], 0, 0, true, false};
  return true;
}
struct Result {
  Status status = Status::MissingInputs;
  float coverage{};
  bool survives{};
  // CompleteScalar only certifies evaluation of supplied scalar inputs. No
  // GPU numeric precision, sample footprint or raster coverage certificate.
};
inline float fract(float x) { return x - std::floor(x); }
inline Result evaluate(uint64_t hash, Material m, Mask mask, Frame frame = {}) {
  const auto* recipe = lookup(hash);
  if (!recipe) return {Status::UnsupportedShader};
  if (!m.available || !mask.available ||
      (recipe->pass == Pass::CameraDepth && !frame.available)) return {};
  if (!std::isfinite(m.edge) || !std::isfinite(m.root) ||
      !std::isfinite(mask.blue) || !std::isfinite(mask.alpha)) return {Status::Nonfinite};
  // Native smoothstep is undefined if edge0 >= edge1; never manufacture a
  // classification for this material. Preserve native float constants/order.
  const float hi = std::clamp(m.edge + 0.699999988079071044921875f, 0.0f, 1.0f);
  if (!(m.edge < hi)) return {Status::UndefinedSmoothstep};
  const float t = std::clamp((mask.alpha - m.edge) / (hi - m.edge), 0.0f, 1.0f);
  const float s = t * t * (3.0f - 2.0f * t);
  const float root = std::fma(mask.blue - 1.0f, m.root, 1.0f);
  const float coverage = s * root;
  if (!std::isfinite(coverage)) return {Status::Nonfinite};
  constexpr float cut = 0.33329999446868896484375f;
  if (recipe->pass != Pass::CameraDepth) return {Status::CompleteScalar, coverage, coverage >= cut};
  if (!std::isfinite(frame.dither_mode)) return {Status::Nonfinite};
  if (frame.dither_mode == 0.0f) return {Status::CompleteScalar, coverage, coverage >= cut};
  if (!frame.coordinates_available) return {};
  if (!std::isfinite(frame.phase) || !std::isfinite(frame.frag_x) || !std::isfinite(frame.frag_y))
    return {Status::Nonfinite};
  // Exact native phase expression uses truncation remainder, not positive mod,
  // and gl_FragCoord is the ORIGINAL raster sample, not an RT output pixel.
  const float phase = frame.phase - 4.0f * std::trunc(frame.phase / 4.0f);
  const float x = fract(((std::fma(frame.frag_y, 2.0f, frame.frag_x) - 1.5f) + phase) * 0.20000000298023223876953125f);
  const float y = fract(std::fma(frame.frag_x, 2.408400058746337890625f, frame.frag_y * 3.2534999847412109375f));
  const float noise = fract(0.833000004291534423828125f * x + 0.16660000383853912353515625f * y);
  const float gate = std::fma(s, root, noise - 0.66670000553131103515625f);
  if (!std::isfinite(gate)) return {Status::Nonfinite};
  return {Status::CompleteScalar, coverage, !((gate - cut) < 0.0f)};
}
} // namespace bg3::native_scalp
