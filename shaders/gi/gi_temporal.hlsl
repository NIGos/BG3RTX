// Temporal accumulation. With the game's DLSS motion vectors the history is
// reprojected; without them it is reused in place. Either way a depth
// mismatch rejects it, so disocclusions fall back to the fresh sample.
//
// The depth test (rt/depth_tol.hlsli; was |dz| < 3% of the view z):
//   - camera distances, not view z: a camera rotation changes the view z of an off-axis
//     point (by up to ~1.07 z x the rotation at the screen edge) but not its distance;
//   - the same-surface tolerance depth_tol at the actual screen offset between this
//     sample, carried back by its motion vector, and the sample the history texel holds
//     (both are rotating full-res samples of their 2x2 blocks), plus two frames' jitter,
//     two depth roundings, and the depth buffer's own slope at this pixel;
//   - plus kCamStepRel x distance for the camera's own translation along the ray between
//     the frames. That one is NOT derived: it needs the previous camera position, which
//     no binding of this pass has (set 0 b2 _m6 is bound in the ray-traced layouts only).
//     Kept at the old 3% until a per-frame camera step is passed in (then: |dcam| + the
//     MV's rounding).
#include "gi_common.hlsli"

static const float kCamStepRel = 0.03;   // NOT DERIVED (see above): the old value

// Last frame's full-resolution sample of half-res texel hp (gi_fp of frame - 1), pixel centre.
float2 prev_sample_px(int2 hp) {
  if (pc.res_shift == 0) return float2(hp) + 0.5;
  const uint f = pc.frame - 1u;
  return float2(min(uint2(hp) * 2u + uint2(f & 1u, (f >> 1) & 1u), pc.full - 1u)) + 0.5;
}

[numthreads(8, 8, 1)]
void main(uint3 tid : SV_DispatchThreadID) {
  uint2 id = tid.xy;
  if (any(id >= pc.half)) return;
  float4 cur = giRaw[id];
  float cd = giRawDepth[id];
  int2 hp = int2(id);
  const float2 cur_px = gi_sample_px(int2(id));   // the sample cd belongs to
  float2 prev_px = cur_px;                        // where its surface was last frame
  if (pc.has_mv != 0) {
    float scale = pc.res_shift ? 2.0 : 1.0;
    float2 fpix = float2(min(id * (pc.res_shift ? 2u : 1u) + pc.res_shift, pc.full - 1)) + 0.5;
    float2 mv = gVelocity.Load(int3(int2(fpix), 0)) * pc.mv_scale;
    float2 prev = (fpix + mv) / scale;
    // A non-finite motion vector is no position: float-to-int of NaN/Inf is undefined.
    hp = all(isfinite(prev)) && all(prev >= 0.0) && all(prev < float2(pc.half))
       ? int2(floor(prev)) : int2(-1, -1);
    prev_px = cur_px + mv;
  }
  bool inside = all(hp >= 0) && all(hp < int2(pc.half));
  float4 hist = inside ? giHistIn[hp] : float4(0, 0, 0, 1);
  float hd = inside ? giHistDepthIn[hp] : 0.0;
  bool valid = inside && hd > 0.0 && cd > 0.0 && all(isfinite(hist));
  if (valid) {
    const float2 inv_full = 1.0 / float2(pc.full);
    const float2 hs = prev_sample_px(hp);
    const float3 P = ray_at(cur_px * inv_full) * cd;
    const float rc = length(P);
    // |ray_at| depends on the screen position only (the frustum rays rotate rigidly).
    const float rh = hd * length(ray_at(hs * inv_full));
    const float ndv = rc > 0.0 ? dot(depth_geo_normal(int2(cur_px), P), P) / rc : 0.0;
    const float lat = length(prev_px - hs) + 2.0 * kJitterPx;
    const float tol = depth_tol(rc, pixel_angle(cur_px * inv_full), ndv, lat, 2.0) + kCamStepRel * rc;
    valid = abs(rh - rc) < tol;
  }
  // Rejected history falls back to the fresh sample. lerp(hist, cur, 1) is hist * 0 + cur, which
  // is NaN for a NaN/Inf history texel, and that texel would then never recover: select instead
  // (the same value for finite history).
  giHistOut[id] = valid ? lerp(hist, cur, pc.history) : cur;
  giHistDepthOut[id] = cd;
}
