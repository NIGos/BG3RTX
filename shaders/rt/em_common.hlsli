// D-EM: the emissive scene (layer/emissive.cpp, docs/TRACK_D_RT.md 12).
//
// BG3 redraws its emissive meshes, depth-tested, into the effects target before the
// tiled lighting. The layer captures those draws (transform feedback, the BVH split's
// emissive scene) with one record per draw, and at the frame's first tiled-lighting
// dispatch builds a separate emissive TLAS and a bounded light list over its triangles.
//
// Space: camera-relative world, the lighting's space (the same as the main TLAS).
// Units: radiance is what the game's emissive shader writes into its effects target,
// i.e. the HDR buffer's pre-exposed units. Divide by the header's pre-exposure (lighting
// s1b13[0], when known) for the lighting's own units.
//
// Why a separate TLAS: the emissive draws redraw meshes that are already in the main
// TLAS (coplanar duplicates), so mixing them in would z-fight and would make every
// existing ray (shadows, GI, reflections) pay for and mask out the duplicates. A ray that
// wants emission traces the main TLAS, then asks this small TLAS (tens of instances)
// whether its hit is emissive: em_lookup() over [t - eps, t + eps]. Light sampling uses
// the light list, which needs a structure of its own anyway.
//
// Set EM_SET (3 by default) of a consumer pass; layer/emissive.cpp writes it per build.
#pragma once

#ifndef EM_SET
#define EM_SET 3
#endif

static const uint kEmInfoPool = 1u << 8;       // vertices in the mesh pool (object space), else in the soup (world)
static const uint kEmInfoEmissive = 1u << 11;  // (always set here) word 2 is the record index
static const uint kEmMaxTextures = 256;

// One per emissive draw (64 bytes).
static const uint kEmModeConst = 0;   // radiance as is
static const uint kEmModeMaskR = 1;   // radiance * mask.r (the game's glow maps)
static const uint kEmModeMaskRGB = 2; // radiance * mask.rgb (coloured glow maps)
static const uint kEmFlagLuminance = 1u << 0;   // the material's emission is in luminance units (x the global scale)
static const uint kEmFlagApprox = 1u << 1;      // the family's formula is not fully decoded (vertex colour, extra maps)
static const uint kEmFlagPulse = 1u << 2;       // time-animated (sampled at record time)
struct EmRec {
  float3 radiance;   // colour x intensity [x pulse x global scale], effects-target units
  uint mode;         // kEmMode*
  uint tex;          // mask texture slot in emTex (0: none)
  uint flags;        // kEmFlag*
  uint fs_lo, fs_hi; // the draw's fragment shader (asset DB key)
  float4 uv;         // mask uv = uv.xy * captured uv + uv.zw
  uint draw;         // index of the draw in its build
  uint cls;          // asset DB material class
  float2 pad;
};

// One per emissive triangle (80 bytes), camera-relative world.
struct EmLight {
  float3 p0; uint rec;
  float3 e1; float area;     // p1 - p0
  float3 e2; float power;    // p2 - p0; power = pi * area * luminance(radiance), one-sided Lambertian
  float3 radiance; uint inst; // mean radiance over the triangle (mask averaged at the triangle's mip)
  uint prim; uint flags; float2 pad;
};

[[vk::binding(0, EM_SET)]] RaytracingAccelerationStructure emTlas;
[[vk::binding(1, EM_SET)]] ByteAddressBuffer emInfo;    // 16 B per TLAS instance: base, stride | flags, record, triangles
[[vk::binding(2, EM_SET)]] ByteAddressBuffer emPool;    // cached meshes: object space, stride B per vertex, uv at +16
[[vk::binding(3, EM_SET)]] ByteAddressBuffer emSoup;    // per-draw geometry: camera-relative world, uv at +16
[[vk::binding(4, EM_SET)]] StructuredBuffer<EmRec> emRecs;
[[vk::binding(5, EM_SET)]] RWStructuredBuffer<EmLight> emLights;
[[vk::binding(6, EM_SET)]] Texture2D<float4> emTex[kEmMaxTextures];
[[vk::binding(7, EM_SET)]] SamplerState emSampler;      // linear, wrap
[[vk::binding(8, EM_SET)]] RWByteAddressBuffer emStats; // debug views' statistics (host-read)
// Header, 16 words: 0 lights, 1 triangles left out (cap), 2 instances, 3 records,
// 4 pre-exposure (float), 5 build frame (low 32 bits); words 16+: per-group power sums.
[[vk::binding(9, EM_SET)]] RWByteAddressBuffer emHeader;

float em_lum(float3 c) { return dot(c, float3(0.2126, 0.7152, 0.0722)); }

struct EmTri { float3 p[3]; float2 uv[3]; uint rec; bool ok; };

// A triangle of TLAS instance `inst`. Pool vertices are transformed by `o2w` (the
// instance's object-to-world 3x4); soup vertices are already world.
EmTri em_triangle(uint inst, uint prim, float3x4 o2w) {
  EmTri t;
  uint4 info = emInfo.Load4(inst * 16);
  uint stride = info.y & 0xffu;
  bool pool = (info.y & kEmInfoPool) != 0;
  t.rec = info.z;
  uint nrec = emHeader.Load(3 * 4);   // records of this build: a record index is never trusted blindly
  t.ok = prim < info.w && stride >= 16 && t.rec < nrec;
  if (!t.ok) t.rec = 0;
  uint base = info.x + prim * 3 * stride;
  [unroll] for (uint k = 0; k < 3; ++k) {
    uint a = base + k * stride;
    float3 p = asfloat(pool ? emPool.Load3(a) : emSoup.Load3(a));
    float2 uv = stride >= 24 ? asfloat(pool ? emPool.Load2(a + 16) : emSoup.Load2(a + 16)) : float2(0, 0);
    t.p[k] = pool ? mul(o2w, float4(p, 1.0)) : p;
    t.uv[k] = uv;
  }
  return t;
}

// Radiance of record `r` at mask uv `uv`, at mip `lod`.
float3 em_radiance(EmRec r, float2 uv, float lod) {
  float3 L = r.radiance;
  uint mode = r.mode & 0xffu;
  if (mode != kEmModeConst && r.tex != 0 && r.tex < kEmMaxTextures) {
    float4 m = emTex[NonUniformResourceIndex(r.tex)].SampleLevel(emSampler, uv * r.uv.xy + r.uv.zw, lod);
    L *= mode == kEmModeMaskRGB ? m.rgb : m.rrr;
  }
  return all(L >= 0.0) && all(L < 1e6) ? L : float3(0, 0, 0);
}

// The mip at which one texel of the record's mask covers about `px` of the triangle's
// world size: lod = log2(sqrt(uv area * texels) * px / sqrt(world area)).
float em_lod(EmRec r, float uv_area, float world_area, float px) {
  if ((r.mode & 0xffu) == kEmModeConst || r.tex == 0 || r.tex >= kEmMaxTextures) return 0.0;
  uint w, h, levels;
  emTex[NonUniformResourceIndex(r.tex)].GetDimensions(0, w, h, levels);
  float texels = uv_area * abs(r.uv.x * r.uv.y) * float(w) * float(h);
  float ratio = texels * px * px / max(world_area, 1e-12);
  return clamp(0.5 * log2(max(ratio, 1e-12)), 0.0, float(levels) - 1.0);
}

// Emission at a hit of the main TLAS at distance t along (o, d): the emissive scene's
// closest triangle within [t - eps, t + eps]. `px` is the world size of one pixel (or
// ray-cone width) at the hit, for the mask's mip. Returns false when the hit is not emissive.
bool em_lookup(float3 o, float3 d, float t, float eps, float px, out float3 L) {
  L = float3(0, 0, 0);
  RayDesc ray;
  ray.Origin = o;
  ray.Direction = d;
  ray.TMin = max(t - eps, 0.0);
  ray.TMax = t + eps;
  RayQuery<RAY_FLAG_FORCE_OPAQUE> q;
  q.TraceRayInline(emTlas, RAY_FLAG_NONE, 0xFF, ray);
  q.Proceed();
  if (q.CommittedStatus() != COMMITTED_TRIANGLE_HIT) return false;
  EmTri tri = em_triangle(q.CommittedInstanceIndex(), q.CommittedPrimitiveIndex(), q.CommittedObjectToWorld3x4());
  if (!tri.ok) return false;
  float2 b = q.CommittedTriangleBarycentrics();
  float2 uv = tri.uv[0] * (1.0 - b.x - b.y) + tri.uv[1] * b.x + tri.uv[2] * b.y;
  EmRec r = emRecs[tri.rec];
  float wa = 0.5 * length(cross(tri.p[1] - tri.p[0], tri.p[2] - tri.p[0]));
  float2 du = tri.uv[1] - tri.uv[0], dv = tri.uv[2] - tri.uv[0];
  float ua = 0.5 * abs(du.x * dv.y - du.y * dv.x);
  L = em_radiance(r, uv, em_lod(r, ua, wa, px));
  return true;
}
