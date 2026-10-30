#pragma once
#include "omm_backend_plan.h"
#include <cmath>

namespace bg3::omm {
enum class SamplingCertificate : uint32_t { None, CompleteNumericDomain, UniformDecodedEndpoints };
// Exact 64-byte push layout of rt/omm_bake_plain.hlsl.
struct PlainBakePush {
  uint32_t vertex_offset{}, vertex_stride{}, triangles{}, channel{};
  float scale_u{1}, scale_v{1}, threshold_lo{}, threshold_hi{1};
  uint32_t max_lod{16}, max_texels{4096}, sampling_certificate{}, source_bytes{};
  float alpha_error{}, uv_error_u{}, uv_error_v{};
  uint32_t source_epoch_offset{};
};
static_assert(sizeof(PlainBakePush)==64);
inline bool valid_plain_push(const PlainBakePush& p) {
  if (!p.triangles || p.vertex_stride<24 || p.vertex_stride%4 || p.vertex_offset%4 ||
      p.vertex_offset>p.source_bytes || p.channel>3 ||
      p.triangles>(p.source_bytes-p.vertex_offset)/p.vertex_stride/3 ||
      p.triangles>65535u*64u || !p.max_texels ||
      uint64_t(p.triangles)*p.max_texels>(1u<<24) || p.max_lod>16 || p.sampling_certificate>2 || p.source_epoch_offset%4)
    return false;
  for (float x:{p.scale_u,p.scale_v,p.threshold_lo,p.threshold_hi,p.alpha_error,p.uv_error_u,p.uv_error_v})
    if (!std::isfinite(x)) return false;
  return p.threshold_lo>=0 && p.threshold_hi<=1 && p.threshold_lo<=p.threshold_hi &&
         p.alpha_error>=0 && p.uv_error_u>=0 && p.uv_error_v>=0;
}
inline bool level_zero_descriptors(uint32_t count,std::vector<Triangle>& out) {
  out.clear();
  if (!count || count>UINT32_MAX/16) return false;
  out.reserve(count);
  // Four bytes per primitive permit race-free uint writes. EXT reads only the
  // low two bits for subdivision zero. The padding is intentionally unused.
  for (uint32_t i=0;i<count;++i) out.push_back({i*4u,0,2});
  return true;
}
}
