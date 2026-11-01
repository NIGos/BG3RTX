// M5.3 water composition (rt_water_compose=1, docs/PLAN_M5_WATER.md M5.3, core/water.cpp).
// Replaces the forward water FS's probe reflection term with the ray-traced reflection where
// the ray resolves, exactly, with the FS's own factors (common/water_compose.h: the patched FS
// stored them per pixel, common/water_fs_patch.h):
//     C' = max(C + A L - B, 0),  A = G E,  B = G k Ks,  G = a T X
// a the FS's opacity, T its fog transmittance, X its atmosphere transmittance, k its probe-to-
// lighting scale (set 2 b0 _m0[0] * 100), E = F0 DFG.x + DFG.y its own split-sum LUT value at
// (N.V, roughness), Ks its probe term (global + local probes, times E), L the traced radiance
// in the lighting's units (a probe of radiance S lights as S k: D-EN4). Energy: the traced
// radiance takes the same env-BRDF weight E the probe had, so a ray that sees what the probe
// holds (L = S k) changes nothing, and the probe term is subtracted exactly once (no double
// count). Unresolved rays keep C.
//
// water_copy (at the lighting, after water_reflect, run_post): this frame's lit radiance
//   (gDiffuse + gSpecular, the M5.2 tier (a) source), its depth and material class, and the
//   lighting's camera constants, into images / a buffer of ours, so the composition at the end
//   of the forward water pass does not depend on the game's lighting targets still being
//   intact then. Thread (0,0) copies the camera (CameraCB, ViewportCB) into cCam: one block
//   (not per frame slot) that every frame's copy overwrites, so the compose reads the camera of
//   the lighting the GPU ran last, the same lighting as wRad / wDepOut, whatever the order the
//   CPU recorded the lighting and the forward pass in (core/water.cpp, common/water_compose.h).
// water_compose (the end of each forward water pass, core/water.cpp water_on_pass_end): per
//   pixel with a record (and the record's colour IS the target's: overlapping water fragments
//   store in any order, the attachment keeps the last), one mirror ray along reflect(-V, N)
//   with the FS's own N, from P = t * dir (t the FS's camera distance), resolved from the screen
//   as M5.2 (tier (a): the hit projects, the depth there is that surface at its geometric
//   normal, it faces the ray, not skin). Rough pixels (roughness above rf_mirror_rough: the
//   probe is a pre-filtered lobe, a mirror ray is not) and rays below the surface keep C.
//   rt_debug 25: the delta view instead of the image (below).
// Statistics (cStats words; core/water.cpp "[water] compose"): 0 compose dispatches, 1 records,
//   2 owned, 3 not owned (colour mismatch), 4 invalid, 5 rough (kept), 6 below the surface
//   (kept), 7 rays, 8..13 tiers 1..6 (as water_reflect), 14 skin rejections, 15 clamped at 0,
//   16/17 sum lum(C) over owned, 18/19 sum lum(B) over composed, 20/21 sum lum(A L) over composed,
//   22/23 sum positive lum(C' - C), 24/25 sum negative |lum(C' - C)|, 26/27 sum lum(B) over owned,
//   28 composed, 29/30/31 composed with A L / B below 0.5 / 0.5..2 / above 2; sums x 4096 (64-bit).
// Key off: nothing of this runs (no ordinary shader includes this file).
#include "../gi/gi_common.hlsli"
#include "rt_common.hlsli"

struct ComposeParams {
  uint4 a;    // x pitch (records per row), y capacity (records), z rt_debug, w flags (bit 0 apply)
  float4 b;   // x roughness above which the probe stays (rf_mirror_rough)
};
[[vk::binding(60, 0)]] ConstantBuffer<ComposeParams> cp;
[[vk::binding(61, 0)]] RWByteAddressBuffer cStats;
[[vk::binding(62, 0)]] [[vk::image_format("rgba16f")]] RWTexture2D<float4> wRad;   // rgb L, a material class
[[vk::binding(63, 0)]] [[vk::image_format("r32f")]] RWTexture2D<float> wDepOut;     // water_copy's target (gDepth copy)
[[vk::binding(64, 0)]] RWByteAddressBuffer cCam;                                     // water_copy: CameraCB + ViewportCB
[[vk::binding(66, 0)]] RWByteAddressBuffer cRec;                                     // the FS's records
[[vk::binding(67, 0)]] [[vk::image_format("r11g11b10f")]] RWTexture2D<float3> target;   // the forward water target

static const uint kHeaderBytes = 64u, kRecordBytes = 32u;   // common/water_compose.h
static const uint kVpOffset = 512u;                         // cCam: CameraCB (25 float4) at 0, ViewportCB here

void cstat(uint word, bool cond) {
  const uint n = WaveActiveCountBits(cond);
  if (WaveIsFirstLane() && n != 0u) cStats.InterlockedAdd(word * 4u, n);
}
void csum64(uint word, float v, bool cond) {
  const uint q = cond ? uint(clamp(v, 0.0, 1e4) * 4096.0 + 0.5) : 0u;
  const uint s = WaveActiveSum(q);
  if (WaveIsFirstLane() && s != 0u) {
    uint old;
    cStats.InterlockedAdd(word * 4u, s, old);
    if (old + s < old) cStats.InterlockedAdd(word * 4u + 4u, 1u);
  }
}
float lum(float3 c) { return dot(c, float3(0.2126, 0.7152, 0.0722)); }

[numthreads(8, 8, 1)]
void water_copy(uint3 tid : SV_DispatchThreadID) {
  const uint2 fp = tid.xy;
  if (all(fp == 0)) {
    const float4 c[25] = {cam.v0, cam.v1, cam.v2, cam.m6_r0, cam.m6_r1, cam.m6_r2, cam.m6_r3, cam.ray0, cam.ray1,
                          cam.ray2, cam.x[0], cam.x[1], cam.x[2], cam.x[3], cam.x[4], cam.x[5], cam.x[6], cam.x[7],
                          cam.x[8], cam.x[9], cam.x[10], cam.x[11], cam.x[12], cam.x[13], cam.x[14]};
    [unroll] for (uint i = 0; i < 25u; ++i) cCam.Store4(i * 16u, asuint(c[i]));
    cCam.Store4(kVpOffset, asuint(float4(vp.scale, vp.bias)));
  }
  if (any(fp >= pc.full)) return;
  const float3 L = gDiffuse[fp] + gSpecular[fp];
  const uint cls = uint(gMaterial.Load(int3(fp, 0)).a * 255.0 + 0.5) & 7u;
  wRad[fp] = float4(all(isfinite(L)) ? max(L, 0.0) : 0.0.xxx, float(cls));
  wDepOut[fp] = gDepth.Load(int3(fp, 0));
}

// B10G11R11 ownership (common/water_compose.h same_encoded): t within one ulp of c's channel format.
bool same_encoded(float c, float t, int mbits) {
  if (!(c >= 0.0) || !(t >= 0.0) || !isfinite(c) || !isfinite(t)) return false;
  const float m = max(c, 6.103515625e-05);
  float e;
  frexp(m, e);   // m = f 2^e, f in [0.5, 1): ilogb(m) = e - 1
  return abs(t - c) <= ldexp(1.0, e - 1.0 - float(mbits));
}

[numthreads(8, 8, 1)]
void water_compose(uint3 tid : SV_DispatchThreadID) {
  g_rt_pass = kRtPassRefl;
  const uint2 fp = tid.xy;
  if (all(fp == 0)) cStats.InterlockedAdd(0, 1u);
  const bool inside = all(fp < pc.full);
  const uint idx = fp.y * cp.a.x + fp.x;
  bool has = inside && fp.x < cp.a.x && idx < cp.a.y;
  const uint at = kHeaderBytes + idx * kRecordBytes;
  uint4 r0 = 0u, r1 = 0u;
  if (has) {
    r0 = cRec.Load4(at);
    r1 = cRec.Load4(at + 16u);
  }
  has = has && (r1.w & 0xffffu) != 0u;
  if (has) cRec.Store(at + 28u, 0u);   // consumed
  const float2 a01 = f16tof32(uint2(r0.x, r0.x >> 16)), ab = f16tof32(uint2(r0.y, r0.y >> 16));
  const float2 b12 = f16tof32(uint2(r0.z, r0.z >> 16)), n01 = f16tof32(uint2(r0.w, r0.w >> 16));
  const float2 nc = f16tof32(uint2(r1.x, r1.x >> 16)), c12 = f16tof32(uint2(r1.y, r1.y >> 16));
  const float3 A = float3(a01, ab.x), B = float3(ab.y, b12), Crec = float3(nc.y, c12);
  float3 N = float3(n01, nc.x);
  const float t = asfloat(r1.z);
  const float rough = float(r1.w >> 16) / 65535.0;
  const float3 Ct = has ? target[fp] : 0.0.xxx;
  const bool own = has && same_encoded(Crec.r, Ct.r, 6) && same_encoded(Crec.g, Ct.g, 6) && same_encoded(Crec.b, Ct.b, 5);
  const float ln = length(N);
  const bool valid = own && all(isfinite(A)) && all(isfinite(B)) && all(A >= 0.0) && all(B >= 0.0) && t > 0.0 &&
                     isfinite(t) && ln > 0.5 && ln < 1.5;
  N = valid ? N / ln : float3(0, 1, 0);
  const float2 uv = (float2(min(fp, pc.full - 1)) + 0.5) / float2(pc.full);
  const float3 dir = normalize(ray_at(uv));
  const float3 P = dir * t;
  const float3 V = -dir;
  const bool smooth = rough <= cp.b.x;
  const bool above = dot(N, V) > 0.0;
  const bool trace = valid && smooth && above;
  uint tier = 0u;
  bool skin = false;
  float3 L = 0.0.xxx;
  if (trace) {
    const float3 R = reflect(-V, N);
    const float pa = pixel_angle(uv);
    // The scene TLAS has no water (its own TLAS): one footprint off the surface clears it.
    const float3 o = P + N * (t * pa);
    RtCone cone = {t * pa, pa};
    bool aborted;
    if (!all(isfinite(o)) || !all(isfinite(R))) {
      tier = 6u;
    } else {
      const float th = rt_closest(o, R, pc.rf_tmax, cone, aborted);
      if (aborted) tier = 5u;
      else if (th < 0.0) tier = 4u;
      else {
        const float3 H = o + R * th;
        float2 quv;
        float qd;
        const bool onscreen = project(frustum_inverse(), H, quv, qd) && all(quv >= 0.0) && all(quv <= 1.0);
        tier = 3u;
        if (onscreen) {
          tier = 2u;
          const int2 qp = min(int2(quv * float2(pc.full)), int2(pc.full) - 1);
          const float sd = gDepth.Load(int3(qp, 0));
          if (sd > 0.0) {
            const float3 Pq = ray_at((float2(qp) + 0.5) / float2(pc.full)) * sd;
            float3 ngq = depth_geo_normal(qp, Pq);
            if (dot(ngq, Pq) > 0.0) ngq = -ngq;   // towards the camera
            const float4 rq = wRad[qp];
            skin = (uint(rq.a + 0.5) & 7u) == kMatSkin;
            if (depth_same_surface(qp, qd, sd, ngq, false, 0.0, 1.0) && dot(ngq, R) < 0.0 && !skin) {
              L = rq.rgb;
              tier = 1u;
            }
          }
        }
      }
    }
  }
  const bool composed = tier == 1u;
  const float3 Cu = Ct + A * L - B;
  const float3 Cn = composed ? max(Cu, 0.0) : Ct;
  const float d = lum(Cn) - lum(Ct);
  cstat(1, has);
  cstat(2, own);
  cstat(3, has && !own);
  cstat(4, own && !valid);
  cstat(5, valid && !smooth);
  cstat(6, valid && smooth && !above);
  cstat(7, trace && tier != 6u);
  // Tiers 1..6 at words 8..13 (common/water_compose.h kCsTier1). Was 6 + k: tier 1 went into the
  // ray count (7) and every logged tier was the next one (m5w3: "off screen 66%" were sky misses).
  [unroll] for (uint k = 1u; k <= 6u; ++k) cstat(7u + k, trace && tier == k);
  cstat(14, trace && tier == 2u && skin);
  cstat(15, composed && any(Cu < 0.0));
  csum64(16, lum(Ct), own);
  csum64(18, lum(B), composed);
  csum64(20, lum(A * L), composed);
  csum64(22, max(d, 0.0), composed);
  csum64(24, max(-d, 0.0), composed);
  csum64(26, lum(B), valid);
  cstat(28, composed);
  // A L / B per composed pixel (common/water_compose.h ratio_bin): L / (k Sp), the traced radiance
  // over the probe's in that direction. A unit mismatch is one factor for every pixel (one bin);
  // occlusion of the probe's sky by the hit geometry spreads it.
  const float rb = lum(B), ra = lum(A * L), rr = rb > 0.0 ? ra / rb : 0.0;
  cstat(29, composed && rb > 0.0 && rr < 0.5);
  cstat(30, composed && rb > 0.0 && rr >= 0.5 && rr <= 2.0);
  cstat(31, composed && rb > 0.0 && rr > 2.0);
  if (!has) return;
  // rt_debug 25, over every record of this pass (exact codes, no tonemap of ours):
  //   composed: green = the relative brightening d / lum(C), red = the relative darkening;
  //   kept (unresolved ray): dark blue; kept (rough or below the surface): dark cyan;
  //   not owned (another fragment's colour) or invalid: yellow.
  if (cp.a.z == 25u) {
    const float rel = d / max(lum(Ct), 1e-4);
    const float3 c = !own || !valid ? float3(1, 1, 0)
                   : !trace ? float3(0, 0.25, 0.25)
                   : composed ? float3(max(-rel, 0.0), max(rel, 0.0), 0.0)
                   : float3(0, 0, 0.3);
    target[fp] = c;
  } else if (composed && (cp.a.w & 1u) != 0u) {
    target[fp] = Cn;
  }
}
