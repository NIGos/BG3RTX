// Level-0 four-state EXT micromap producer. One microtriangle per primitive.
// No ordinary shader includes this file. The host must certify the full
// sampling/LOD/content domain before enabling known states or attaching output.
struct BakePush {
  uint vertex_offset, vertex_stride, triangles, channel;
  float scale_u, scale_v, threshold_lo, threshold_hi;
  uint max_lod, max_texels, sampling_certificate, source_bytes;
  float alpha_error, uv_error_u, uv_error_v; uint source_epoch_offset;
};
[[vk::push_constant]] ConstantBuffer<BakePush> pc;
[[vk::binding(0, 0)]] ByteAddressBuffer vertices;
[[vk::binding(1, 0)]] Texture2D<float4> maskTexture;
[[vk::binding(2, 0)]] RWByteAddressBuffer opacityData;
[[vk::binding(3, 0)]] RWByteAddressBuffer audit;
[[vk::binding(4, 0)]] RWByteAddressBuffer contentEpochs;
static const uint kOpacityDataOffset = 256u;
uint atomic_epoch(uint offset) { uint value; contentEpochs.InterlockedOr(offset, 0u, value); return value; }
bool content_matches(uint4 stamp) {
  return stamp.z == 0u && stamp.w == 0x4f4d4d31u &&
         atomic_epoch(4u) == 0u && atomic_epoch(0u) == stamp.y &&
         atomic_epoch(pc.source_epoch_offset) == stamp.x;
}
[numthreads(1, 1, 1)]
void init_stamp(uint3 id : SV_DispatchThreadID) {
  uint bytes; contentEpochs.GetDimensions(bytes);
  uint image = 0u, global = 0u, disabled = 1u;
  if (pc.source_epoch_offset >= 8u && pc.source_epoch_offset % 4u == 0u &&
      pc.source_epoch_offset <= bytes && bytes - pc.source_epoch_offset >= 4u) {
    disabled = atomic_epoch(4u);
    global = atomic_epoch(0u);
    image = atomic_epoch(pc.source_epoch_offset);
  }
  const bool valid = disabled == 0u && global != 0xffffffffu && image != 0u && image != 0xffffffffu;
  opacityData.Store4(0u, uint4(image, global, valid ? 0u : 1u, 0x4f4d4d31u));
}

// The one-texel halo covers coordinate quantization crossing a texel boundary;
// it is not permission to assume exact filtered alpha or a complete UV domain.
bool axis(float lo, float hi, uint extent, out int start, out uint count) {
  float first = floor(lo * float(extent) - 0.5) - 1.0;
  float last = floor(hi * float(extent) - 0.5) + 2.0;
  start = 0; count = extent;
  if (!isfinite(first) || !isfinite(last)) return false;
  if (last - first + 1.0 >= float(extent)) return true;
  // At this range integral float32 coordinates remain exact. Larger domains
  // either use the complete mip above or remain unsupported.
  if (abs(first) >= 8388608.0 || abs(last) >= 8388608.0 || first > last) return false;
  start = int(first); count = uint(last - first + 1.0);
  return true;
}
int wrap(int x, uint extent) {
  int r = x % int(extent);
  return r < 0 ? r + int(extent) : r;
}
[numthreads(64, 1, 1)]
void main(uint3 tid : SV_DispatchThreadID) {
  const uint primitive = tid.x;
  if (primitive >= pc.triangles) return;
  uint state = 3u; // UnknownOpaque, NEVER a two-state opaque substitute.
  float lower = 0.0, upper = 1.0;
  uint visited = 0, reason = 1u;
  // Each primitive has a whole uint allocation; no overlapping packed writes.
  opacityData.Store(kOpacityDataOffset + primitive * 4u, state);
  audit.Store4(primitive * 16u, uint4(asuint(lower), asuint(upper), visited, reason));
  uint invalid; opacityData.InterlockedOr(8u, 0u, invalid);
  const uint4 stamp = uint4(opacityData.Load(0u), opacityData.Load(4u), invalid, opacityData.Load(12u));
  if (!content_matches(stamp)) { uint old; opacityData.InterlockedOr(8u, 1u, old); return; }
  if (pc.vertex_stride < 24u || pc.vertex_stride % 4u != 0u || pc.vertex_offset % 4u != 0u ||
      pc.channel > 3u || pc.sampling_certificate == 0u || pc.sampling_certificate > 2u ||
      !all(isfinite(float4(pc.scale_u, pc.scale_v, pc.threshold_lo, pc.threshold_hi))) ||
      pc.threshold_lo > pc.threshold_hi || pc.threshold_lo < 0 || pc.threshold_hi > 1 ||
      !all(isfinite(float3(pc.alpha_error, pc.uv_error_u, pc.uv_error_v))) ||
      any(float3(pc.alpha_error, pc.uv_error_u, pc.uv_error_v) < 0)) return;
  // Overflow-safe source range, including UVs of all three vertices.
  if (pc.vertex_offset > pc.source_bytes ||
      primitive >= (pc.source_bytes - pc.vertex_offset) / pc.vertex_stride / 3u) return;
  const uint base = pc.vertex_offset + primitive * 3u * pc.vertex_stride;
  const float2 scale = float2(pc.scale_u, pc.scale_v);
  const float2 u0 = asfloat(vertices.Load2(base + 16u)) * scale;
  const float2 u1 = asfloat(vertices.Load2(base + pc.vertex_stride + 16u)) * scale;
  const float2 u2 = asfloat(vertices.Load2(base + 2u * pc.vertex_stride + 16u)) * scale;
  if (!all(isfinite(u0)) || !all(isfinite(u1)) || !all(isfinite(u2))) return;
  // Bound the native float32 affine interpolation followed by scale versus
  // scaling the vertices first. Full epsilon (rather than half epsilon) and
  // sixteen operations also cover representable barycentric boundary error.
  const float gamma16 = (16.0 * 0.00000011920928955078125) / (1.0 - 16.0 * 0.00000011920928955078125);
  const float2 arithmetic = gamma16 * (abs(u0) + abs(u1) + abs(u2) + abs(u1-u0) + abs(u2-u0));
  const float2 uvError = float2(pc.uv_error_u, pc.uv_error_v) + arithmetic;
  const float2 uvLo = min(u0, min(u1, u2)) - uvError;
  const float2 uvHi = max(u0, max(u1, u2)) + uvError;
  uint width, height, levels;
  maskTexture.GetDimensions(0, width, height, levels);
  if (width == 0 || height == 0 || levels == 0 || width > 8388608u || height > 8388608u) return;
  const uint last = min(min(pc.max_lod, levels - 1u), 16u);
  // First pass validates the complete budget before reading any texel.
  for (uint level = 0; level <= last; ++level) {
    maskTexture.GetDimensions(level, width, height, levels);
    int x, y; uint nx, ny;
    if (!axis(uvLo.x, uvHi.x, width, x, nx) || !axis(uvLo.y, uvHi.y, height, y, ny) ||
        ny == 0 || nx > pc.max_texels / ny) return;
    const uint n = nx * ny;
    if (visited > pc.max_texels - n) return;
    visited += n;
  }
  lower = 1.0; upper = 0.0;
  for (uint level = 0; level <= last; ++level) {
    maskTexture.GetDimensions(level, width, height, levels);
    int x, y; uint nx, ny;
    axis(uvLo.x, uvHi.x, width, x, nx); axis(uvLo.y, uvHi.y, height, y, ny);
    for (uint j = 0; j < ny; ++j) for (uint i = 0; i < nx; ++i) {
      const float4 texel = maskTexture.Load(int3(wrap(x + int(i), width), wrap(y + int(j), height), int(level)));
      const float a = pc.channel == 0u ? texel.x : pc.channel == 1u ? texel.y : pc.channel == 2u ? texel.z : texel.w;
      if (!isfinite(a) || a < 0 || a > 1) return;
      lower = min(lower, a); upper = max(upper, a);
    }
  }
  if (pc.sampling_certificate == 2u) {
    // Uniform decoded endpoints only: no mixed-value arithmetic certificate.
    // With stochastic thresholds [0,1], zero remains UNKNOWN (native >= accepts
    // hash zero); one is opaque. Never shift a cutoff/hash for acceleration.
    state = lower == 1.0 && upper == 1.0 ? 1u :
            lower == 0.0 && upper == 0.0 && pc.threshold_lo > 0.0 ? 0u : 3u;
    if (!content_matches(stamp)) { uint old; opacityData.InterlockedOr(8u, 1u, old); state = 3u; }
    opacityData.Store(kOpacityDataOffset + primitive * 4u, state);
    audit.Store4(primitive * 16u, uint4(asuint(lower), asuint(upper), visited, 0u));
    return;
  }
  // Error is a derived provider bound, never a tuned alpha/cutoff epsilon.
  lower = max(0.0, lower - pc.alpha_error);
  upper = min(1.0, upper + pc.alpha_error);
  // Outward round the error arithmetic. With error zero min/max are exact
  // selections of fetched float32 values; no subtraction error is introduced.
  if (pc.alpha_error > 0.0) {
    if (lower > 0.0) lower = asfloat(asuint(lower) - 1u);
    if (upper < 1.0) upper = asfloat(asuint(upper) + 1u);
  }
  // Native >=: transparent requires STRICT upper<threshold_lo.
  state = lower >= pc.threshold_hi ? 1u : upper < pc.threshold_lo ? 0u : 3u;
  if (!content_matches(stamp)) { uint old; opacityData.InterlockedOr(8u, 1u, old); state = 3u; }
  opacityData.Store(kOpacityDataOffset + primitive * 4u, state);
  audit.Store4(primitive * 16u, uint4(asuint(lower), asuint(upper), visited, 0u));
}
