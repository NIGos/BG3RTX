// rt_water_rr_mv (core/water.cpp water_mv, ABI 26 set_rr_motion_vectors): Ray Reconstruction's motion
// vectors with the water surface's motion on the water pixels.
//
// The water writes no depth and no motion (M5.1): the game's velocity pass gives every water pixel the
// motion of the BED below it (or of the sky over an empty G-buffer pixel), so RR reprojects the water's
// colour with the bed's parallax. Here, after water_primary (wT: the water hit t along the pixel's unit
// ray, class 1 water in front of the bed, 2 water over an empty G-buffer pixel), each water pixel gets
//     mv_water = mv_game + prev_px(P_water) - prev_px(P_bed)
// in render pixels (previous = current + mv * mv_scale, gi_common.hlsli). The current pixel is the same
// for both points (one ray), so this frame's jitter cancels, and prev_px uses ONE previous camera for both,
// so the previous jitter cancels too: only the parallax between the bed and the surface is added. P_bed is
// the G-buffer depth's point (class 1) or the ray's direction far away (class 2: the sky's rotation-only
// motion). The water is taken as world-static (its waves move by less than the wave height per frame).
// The previous camera: this pass stores the lighting's frustum rays, viewport mapping and absolute camera
// each frame (camHist, two slots by frame parity) and uses the other slot when it was written exactly one
// frame earlier; otherwise (first frame, a skipped frame) every pixel keeps the game's vector.
// Every other pixel copies the game's vector unchanged. Key off: nothing of this runs (its own file).
#include "../gi/gi_common.hlsli"

struct Camera2 { float4 v[14]; };   // the lighting's set 0 b2: v[13].xyz = absolute camera
[[vk::binding(63, 0)]] [[vk::image_format("rg32f")]] RWTexture2D<float2> wT;
[[vk::binding(65, 0)]] ConstantBuffer<Camera2> cam2;
[[vk::binding(66, 0)]] RWByteAddressBuffer camHist;   // 2 x kHistBytes
[[vk::binding(67, 0)]] [[vk::image_format("rg16f")]] RWTexture2D<float2> mvOut;

static const uint kHistBytes = 96u;   // frame, pad x3 | ray0 | ray1 | ray2 | vp scale, bias | camera abs (float4 each)

struct PrevCam {
  float3 r0, r1, r2;
  float2 scale, bias;
  float3 abs_cam;
};

PrevCam load_cam(uint at) {
  PrevCam c;
  c.r0 = asfloat(camHist.Load3(at + 16u));
  c.r1 = asfloat(camHist.Load3(at + 32u));
  c.r2 = asfloat(camHist.Load3(at + 48u));
  const float4 v = asfloat(camHist.Load4(at + 64u));
  c.scale = v.xy;
  c.bias = v.zw;
  c.abs_cam = asfloat(camHist.Load3(at + 80u));
  return c;
}

// Render-pixel position of the previous frame's camera for the point P (this frame's camera-relative
// space; `d` = this camera - the previous camera, absolute). False behind the previous camera.
bool prev_px(PrevCam c, float3 P, float3 d, out float2 px) {
  const float3 c0 = c.r0, c1 = c.r1 - c.r0, c2 = c.r2 - c.r0;
  const float3 i0 = cross(c1, c2), i1 = cross(c2, c0), i2 = cross(c0, c1);
  const float det = dot(c0, i0);
  const float3 x = mul(float3x3(i0, i1, i2), P + d) / det;   // (depth, depth a, depth b)
  const float2 ab = x.yz / max(x.x, 1e-20);
  px = ((ab * 2.0) * c.scale + c.bias) * float2(pc.full);
  return x.x > 0.0 && abs(det) > 0.0 && all(isfinite(px));
}

[numthreads(16, 16, 1)]
void water_mv(uint3 tid : SV_DispatchThreadID) {
  const uint2 p = tid.xy;
  const uint cur = (pc.frame & 1u) * kHistBytes, prv = ((pc.frame & 1u) ^ 1u) * kHistBytes;
  // The previous slot was written by an earlier dispatch (an earlier frame); this one writes the other.
  const bool have_prev = camHist.Load(prv) + 1u == pc.frame;
  if (all(p == 0)) {
    camHist.Store4(cur, uint4(pc.frame, 0u, 0u, 0u));
    camHist.Store3(cur + 16u, asuint(cam.ray0.xyz));
    camHist.Store3(cur + 32u, asuint(cam.ray1.xyz));
    camHist.Store3(cur + 48u, asuint(cam.ray2.xyz));
    camHist.Store4(cur + 64u, asuint(float4(vp.scale, vp.bias)));
    camHist.Store3(cur + 80u, asuint(cam2.v[13].xyz));
  }
  if (any(p >= pc.full)) return;
  const float2 game = gVelocity.Load(int3(p, 0));
  float2 mv = game;
  const float2 v = wT[p];
  const uint cls = uint(v.y + 0.5);
  if (have_prev && pc.has_mv != 0u && (cls == 1u || cls == 2u) && v.x > 0.0 && all(pc.mv_scale != 0.0)) {
    const PrevCam c = load_cam(prv);
    const float2 uv = (float2(p) + 0.5) / float2(pc.full);
    const float3 ray = ray_at(uv);
    const float3 dir = normalize(ray);
    const float d = gDepth.Load(int3(p, 0));
    const float3 Pw = dir * v.x;
    const float3 Pb = cls == 1u && d > 0.0 ? ray * d : dir * 1e7;
    const float3 dcam = cam2.v[13].xyz - c.abs_cam;
    float2 qw, qb;
    if (all(isfinite(dcam)) && prev_px(c, Pw, dcam, qw) && prev_px(c, Pb, dcam, qb)) {
      const float2 m = game * pc.mv_scale + (qw - qb);
      if (all(isfinite(m))) mv = m / pc.mv_scale;
    }
  }
  mvOut[p] = mv;
}
