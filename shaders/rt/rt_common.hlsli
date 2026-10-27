// Track D: ray queries against the host's TLAS, with alpha-tested candidates
// (phase 3, docs/TRACK_D_RT.md). Included by the core's ray-traced passes.
//
// Geometry 0 of the BLAS is opaque. Geometry 1 (rt_alpha=1) holds the draws
// whose depth-prepass fragment shader alpha tests (layer/alphatest.cpp); it is
// not opaque, so its triangles come back as candidates, and alpha_pass()
// reproduces the game's test at the hit: UV interpolated from the captured
// soup, the texture the game sampled, its cut-off. Set 1 is the host's.
// With the BVH split (rt_bvh_split=1, layer/bvhsplit.cpp) the TLAS holds one
// instance per draw instance instead, each BLAS with one geometry; InstInfo
// (below) says where each instance's vertices and alpha record are.
#pragma once

[[vk::binding(18, 0)]] RaytracingAccelerationStructure gScene;
// Counters read back by the core (rt_stats=1, core/gi.cpp lists them).
[[vk::binding(20, 0)]] RWByteAddressBuffer rtStats;

// Adds the number of lanes with `cond` to counter `index` (wave-reduced, and
// spread over 64 bins picked by `bin` so the atomics do not serialise; the core
// sums the bins).
void stat_add(uint index, bool cond, uint bin) {
  uint n = WaveActiveCountBits(cond);
  uint b = WaveReadLaneFirst(bin) & 63u;
  if (WaveIsFirstLane() && n) rtStats.InterlockedAdd((index * 64 + b) * 4, n);
}

// Per-draw records: 16-byte header (uint count), then 208 bytes per draw,
// sorted by first_tri (layer/host.h AlphaRec).
[[vk::binding(0, 1)]] ByteAddressBuffer gAlphaRec;
// Geometry 1's vertices, 24 bytes each: camera-relative position (float4), UV.
[[vk::binding(1, 1)]] ByteAddressBuffer gAlphaSoup;
// Sizes: layer/alphatest.cpp kTextures / kCaches.
[[vk::binding(2, 1)]] Texture2D<float4> gAlphaTex[4096];       // indirection (vt) or the alpha texture (tex)
[[vk::binding(3, 1)]] Texture2DArray<float4> gAlphaCache[16];  // virtual texture tile caches
[[vk::binding(4, 1)]] SamplerState gAlphaWrap;
[[vk::binding(5, 1)]] SamplerState gAlphaClamp;
// Geometry 0's vertices (the opaque soup), same layout; the vertex stride is in
// gAlphaRec's header (uint 1). For the geometric normal of a hit (rt_origin=1).
[[vk::binding(6, 1)]] ByteAddressBuffer gOpaqueSoup;
// Per pixel (written by gi/rt_origin.hlsl): xyz the geometric normal facing the
// camera, w = t_hit / |P| - 1 of the TLAS surface under the pixel, or kOriginNoHit.
[[vk::binding(40, 0)]] [[vk::image_format("rgba16f")]] RWTexture2D<float4> rtOrigin;
static const float kOriginNoHit = 1000.0;

static const uint kRecBytes = 208;

uint rec_addr(uint i) { return 16 + i * kRecBytes; }

// Tile-set constant: matrix m (0/1), column c, row r of the SSBO entry (row-major mat4).
float vt_t(uint a, uint m, uint c, uint r) { return asfloat(gAlphaRec.Load(a + 80 + 4 * (m * 16 + r * 4 + c))); }

// BG3's Granite lookup, as in its prepass fragment shaders (e.g. 84d5df19):
// mip from the uv footprint, page entry from the indirection texture, texel in
// the tile cache; the tile set's albedo layer `col` carries the alpha. Without
// screen derivatives the footprint is the ray cone's, isotropic.
float vt_alpha(uint a, float2 uv, float g, uint tex, uint cache, uint col) {
  if (vt_t(a, 0, 2, 3) != 2202.0) return 1.0;   // the game's "tile set resident" sentinel
  float4 P0 = asfloat(gAlphaRec.Load4(a + 48));
  float4 P1 = asfloat(gAlphaRec.Load4(a + 64));
  float2 u = frac(uv) * P0.zw + P0.xy;
  float2 s = float2(vt_t(a, 0, 2, 0), vt_t(a, 0, 3, 0));
  float2 dx = P0.zw * float2(g, 0.0) * s, dy = P0.zw * float2(0.0, g) * s;
  float d1 = dot(dx, dx), d2 = dot(dy, dy);
  float l = log2(max(max(d1, d2), 1e-30));
  float aniso = min(0.5 * l - 0.5 * log2(max(min(d1, d2), 1e-30)), vt_t(a, 0, 1, 0));
  float mip = floor(min(max(0.5 * l - aniso - 0.5, 0.0), P1.x) + 0.5);
  float2 dims = float2(vt_t(a, 0, 0, 3), vt_t(a, 0, 0, 3) * vt_t(a, 0, 1, 3));
  float2 ip = floor(u * dims * pow(0.5, mip));
  uint e = asuint(gAlphaTex[NonUniformResourceIndex(tex)].Load(int3(int2(ip), int(mip))).x);
  float sc = pow(2.0, float(e & 15u));
  float2 s163 = float2(sc, sc * vt_t(a, 0, 1, 3));
  float2 s168 = float2(vt_t(a, 0, 0, 2), vt_t(a, 0, 1, 2));
  float2 s175 = float2(vt_t(a, 1, 0, 0), vt_t(a, 1, 1, 0));
  float2 c = (frac(u * s163) * s168 + float2(float((e >> 14) & 1023u), float((e >> 4) & 1023u))) * s175 +
             float2(vt_t(a, 1, 2, col), vt_t(a, 1, 3, col));
  return gAlphaCache[NonUniformResourceIndex(cache)].SampleLevel(gAlphaClamp, float3(c, float((e >> 24) & 127u)), 0).w;
}

float tex_alpha(uint a, float2 uv, float g, uint tex, uint ch) {
  float2 sc = asfloat(gAlphaRec.Load2(a + 32));
  uint w, h, levels;
  gAlphaTex[NonUniformResourceIndex(tex)].GetDimensions(0, w, h, levels);
  float lod = log2(max(g * max(abs(sc.x), abs(sc.y)) * float(max(w, h)), 1e-8));
  float4 v = gAlphaTex[NonUniformResourceIndex(tex)].SampleLevel(gAlphaWrap, uv * sc, clamp(lod, 0.0, float(levels) - 1.0));
  return ch == 0 ? v.x : ch == 1 ? v.y : ch == 2 ? v.z : v.w;
}

// ---- per-instance data (Track D BVH split, layer/bvhsplit.cpp) ----
// One 16-byte InstInfo per TLAS instance (set 1 binding 7, indexed by the
// instance index):
//   base  byte offset of the instance's first vertex: in the mesh pool (binding 8,
//         object space) or in the soup (binding 6, camera-relative world);
//   meta  bits 0-7 vertex stride (16, or 24 with the UV at +16); kInfoPool: the
//         vertices are in the pool (apply the instance's object-to-world);
//         kInfoAlpha: `rec` is the instance's alpha record; kInfoLegacy: the
//         single soup BLAS of rt_bvh_split=0 (geometry 0 opaque, 1 alpha-tested,
//         records found by triangle index);
//   rec   byte address of the alpha record in gAlphaRec (0: none);
//   tris  triangles of the instance's BLAS (bounds).
[[vk::binding(7, 1)]] ByteAddressBuffer gInstInfo;
[[vk::binding(8, 1)]] ByteAddressBuffer gMeshPool;
static const uint kInfoPool = 1u << 8, kInfoLegacy = 1u << 9, kInfoAlpha = 1u << 10;
// kInfoEmissive: an instance of the emissive scene (D-EM, rt/em_common.hlsli: its own
// TLAS and InstInfo; `rec` is then the emissive record's index). Never set in this TLAS.
static const uint kInfoEmissive = 1u << 11;
struct InstInfo { uint base, meta, rec, tris; };

InstInfo inst_info(uint inst) {
  InstInfo r;
  uint bytes;
  gInstInfo.GetDimensions(bytes);
  uint4 v = inst * 16u + 16u <= bytes ? gInstInfo.Load4(inst * 16u) : uint4(0, kInfoLegacy, 0, 0);
  r.base = v.x;
  r.meta = v.y;
  r.rec = v.z;
  r.tris = v.w;
  return r;
}

// Vertex k of triangle `prim` of an instance, in the instance's object space
// (pool) or camera-relative world (soup).
float3 inst_pos(InstInfo ii, uint prim, uint k) {
  uint stride = ii.meta & 0xffu;
  uint a = ii.base + (prim * 3u + k) * stride;
  return (ii.meta & kInfoPool) != 0u ? asfloat(gMeshPool.Load3(a)) : asfloat(gOpaqueSoup.Load3(a));
}
float2 inst_uv(InstInfo ii, uint prim, uint k) {
  uint stride = ii.meta & 0xffu;
  uint a = ii.base + (prim * 3u + k) * stride + 16u;
  return (ii.meta & kInfoPool) != 0u ? asfloat(gMeshPool.Load2(a)) : asfloat(gOpaqueSoup.Load2(a));
}

// Byte address of the record of alpha-tested triangle `prim` (legacy: geometry 1's
// triangle index; 0 if none).
uint find_rec(uint prim) {
  uint n = gAlphaRec.Load(0);
  if (n == 0) return 0;
  uint lo = 0, hi = n;   // last record with first_tri <= prim
  while (hi - lo > 1) {
    uint mid = (lo + hi) >> 1;
    if (gAlphaRec.Load(rec_addr(mid)) <= prim) lo = mid;
    else hi = mid;
  }
  return rec_addr(lo);
}

// The game's alpha test of record `a` at barycentrics `bary` of the triangle
// (p: world-space corners, u: their UVs). `footprint`: world-space width of the
// ray's footprint at the hit.
bool alpha_eval(uint a, float3 p0, float3 p1, float3 p2, float2 u0, float2 u1, float2 u2, float2 bary, float footprint) {
  uint4 h0 = gAlphaRec.Load4(a);        // first_tri, tris, kind, tex
  uint4 h1 = gAlphaRec.Load4(a + 16);   // cache, channel, cut
  float2 uv = u0 + (u1 - u0) * bary.x + (u2 - u0) * bary.y;
  float2 e1 = u1 - u0, e2 = u2 - u0;
  float area_uv = abs(e1.x * e2.y - e1.y * e2.x);
  float area_w = length(cross(p1 - p0, p2 - p0));
  float g = footprint * sqrt(area_uv / max(area_w, 1e-12));   // uv change across the footprint
  if (!all(isfinite(uv)) || !isfinite(g)) return true;
  uint tex = min(h0.w, 4095u), cache = min(h1.x, 15u);
  float alpha = h0.z == 1 ? vt_alpha(a, uv, g, tex, cache, min(h1.y, 3u)) : tex_alpha(a, uv, g, tex, h1.y);
  return alpha >= asfloat(h1.z);
}

// Legacy: true if the alpha-tested triangle `prim` of geometry 1 is solid at `bary`.
bool alpha_pass(uint prim, float2 bary, float footprint) {
  uint a = find_rec(prim);
  if (a == 0) return true;
  uint4 h0 = gAlphaRec.Load4(a);
  if (prim < h0.x || prim - h0.x >= h0.y) return true;
  uint vb = prim * 72;
  float3 p0 = asfloat(gAlphaSoup.Load3(vb)), p1 = asfloat(gAlphaSoup.Load3(vb + 24)), p2 = asfloat(gAlphaSoup.Load3(vb + 48));
  float2 u0 = asfloat(gAlphaSoup.Load2(vb + 16)), u1 = asfloat(gAlphaSoup.Load2(vb + 40)), u2 = asfloat(gAlphaSoup.Load2(vb + 64));
  return alpha_eval(a, p0, p1, p2, u0, u1, u2, bary, footprint);
}

// A non-opaque candidate of any instance: solid at `bary`?
bool alpha_pass_hit(uint inst, uint geom, uint prim, float2 bary, float footprint, float3x4 o2w) {
  InstInfo ii = inst_info(inst);
  if ((ii.meta & kInfoLegacy) != 0u) return geom == 1u ? alpha_pass(prim, bary, footprint) : true;
  if ((ii.meta & kInfoAlpha) == 0u || ii.rec == 0u || prim >= ii.tris || (ii.meta & 0xffu) < 24u) return true;
  float3 p0 = inst_pos(ii, prim, 0), p1 = inst_pos(ii, prim, 1), p2 = inst_pos(ii, prim, 2);
  if ((ii.meta & kInfoPool) != 0u) {
    p0 = mul(o2w, float4(p0, 1.0));
    p1 = mul(o2w, float4(p1, 1.0));
    p2 = mul(o2w, float4(p2, 1.0));
  }
  return alpha_eval(ii.rec, p0, p1, p2, inst_uv(ii, prim, 0), inst_uv(ii, prim, 1), inst_uv(ii, prim, 2), bary, footprint);
}

// Debug: the family of the draw behind a hit (1 vt, 2 tex, 0 not alpha-tested).
uint alpha_kind(uint prim) {
  uint a = find_rec(prim);
  return a ? gAlphaRec.Load(a + 8) : 0;
}
uint alpha_kind_hit(uint inst, uint geom, uint prim) {
  InstInfo ii = inst_info(inst);
  if ((ii.meta & kInfoLegacy) != 0u) return geom == 1u ? alpha_kind(prim) : 0u;
  return (ii.meta & kInfoAlpha) != 0u && ii.rec != 0u ? gAlphaRec.Load(ii.rec + 8) : 0u;
}
bool alpha_instance(uint inst, uint geom) {
  InstInfo ii = inst_info(inst);
  return (ii.meta & kInfoLegacy) != 0u ? geom == 1u : (ii.meta & kInfoAlpha) != 0u;
}

// The candidate test every ray query loop runs.
#define RT_ALPHA_CANDIDATE(q, cone)                                                                             \
  alpha_pass_hit(q.CandidateInstanceIndex(), q.CandidateGeometryIndex(), q.CandidatePrimitiveIndex(),        \
                 q.CandidateTriangleBarycentrics(), cone.f0 + cone.spread * q.CandidateTriangleRayT(),       \
                 q.CandidateObjectToWorld3x4())

// Ray footprint: width f0 at the origin, growing by `spread` per unit length.
struct RtCone { float f0, spread; };

// Any alpha-tested hit within [0, tmax] (shadow and occlusion rays).
bool rt_occluded(float3 o, float3 d, float tmax, RtCone cone) {
  RayQuery<RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH | RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES> q;
  RayDesc r;
  r.Origin = o;
  r.Direction = d;
  r.TMin = 0.0;
  r.TMax = tmax;
  q.TraceRayInline(gScene, 0, 0xFF, r);
  while (q.Proceed()) {
    if (q.CandidateType() == CANDIDATE_NON_OPAQUE_TRIANGLE && RT_ALPHA_CANDIDATE(q, cone)) q.CommitNonOpaqueTriangleHit();
  }
  return q.CommittedStatus() == COMMITTED_TRIANGLE_HIT;
}

// Debug (rt_debug=3/5): the closest hit, whether it is alpha-tested geometry,
// and the family of the nearest alpha-tested candidate along the ray, whether
// it was let through or not.
struct RtDebugHit { float t; bool alpha; uint near_kind; uint hit_kind; };
RtDebugHit rt_closest_debug(float3 o, float3 d, float tmax, RtCone cone) {
  RayQuery<RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES> q;
  RayDesc r;
  r.Origin = o;
  r.Direction = d;
  r.TMin = 0.0;
  r.TMax = tmax;
  q.TraceRayInline(gScene, 0, 0xFF, r);
  RtDebugHit h;
  h.near_kind = 0;
  float near_t = 1e30;
  while (q.Proceed()) {
    if (q.CandidateType() != CANDIDATE_NON_OPAQUE_TRIANGLE) continue;
    float ct = q.CandidateTriangleRayT();
    if (ct < near_t) {
      near_t = ct;
      h.near_kind = alpha_kind_hit(q.CandidateInstanceIndex(), q.CandidateGeometryIndex(), q.CandidatePrimitiveIndex());
    }
    if (RT_ALPHA_CANDIDATE(q, cone)) q.CommitNonOpaqueTriangleHit();
  }
  bool hit = q.CommittedStatus() == COMMITTED_TRIANGLE_HIT;
  h.t = hit ? q.CommittedRayT() : -1.0;
  h.alpha = hit && alpha_instance(q.CommittedInstanceIndex(), q.CommittedGeometryIndex());
  h.hit_kind = h.alpha ? alpha_kind_hit(q.CommittedInstanceIndex(), q.CommittedGeometryIndex(), q.CommittedPrimitiveIndex())
                       : 0u;
  return h;
}

// Closest hit (alpha-tested): distance (-1 on a miss), the triangle and its instance.
struct RtHit { float t; uint geom; uint prim; uint inst; float3x4 o2w; };
RtHit rt_closest_hit(float3 o, float3 d, float tmax, RtCone cone) {
  RayQuery<RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES> q;
  RayDesc r;
  r.Origin = o;
  r.Direction = d;
  r.TMin = 0.0;
  r.TMax = tmax;
  q.TraceRayInline(gScene, 0, 0xFF, r);
  while (q.Proceed()) {
    if (q.CandidateType() == CANDIDATE_NON_OPAQUE_TRIANGLE && RT_ALPHA_CANDIDATE(q, cone)) q.CommitNonOpaqueTriangleHit();
  }
  RtHit h;
  bool hit = q.CommittedStatus() == COMMITTED_TRIANGLE_HIT;
  h.t = hit ? q.CommittedRayT() : -1.0;
  h.geom = hit ? q.CommittedGeometryIndex() : 0u;
  h.prim = hit ? q.CommittedPrimitiveIndex() : 0u;
  h.inst = hit ? q.CommittedInstanceIndex() : 0u;
  h.o2w = hit ? q.CommittedObjectToWorld3x4() : float3x4(1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0);
  return h;
}

// Legacy: unnormalised geometric normal of a triangle of the soup BLAS (the soups
// are camera-relative, the lighting space).
float3 rt_tri_normal(uint geom, uint prim) {
  float3 p0, p1, p2;
  // Never read past the soups' descriptors (a hit is always inside them; this
  // only guards against a scene and a set from different builds).
  uint bytes;
  if (geom == 0u) {
    uint stride = gAlphaRec.Load(4);
    gOpaqueSoup.GetDimensions(bytes);
    if (stride == 0u || stride > 64u || prim >= bytes / (3u * stride)) return 0.0.xxx;
    uint vb = prim * 3u * stride;
    p0 = asfloat(gOpaqueSoup.Load3(vb));
    p1 = asfloat(gOpaqueSoup.Load3(vb + stride));
    p2 = asfloat(gOpaqueSoup.Load3(vb + 2u * stride));
  } else {
    gAlphaSoup.GetDimensions(bytes);
    if (prim >= bytes / 72u) return 0.0.xxx;
    uint vb = prim * 72u;
    p0 = asfloat(gAlphaSoup.Load3(vb));
    p1 = asfloat(gAlphaSoup.Load3(vb + 24u));
    p2 = asfloat(gAlphaSoup.Load3(vb + 48u));
  }
  float3 n = cross(p1 - p0, p2 - p0);
  return all(isfinite(n)) ? n : 0.0.xxx;
}

// Unnormalised geometric normal of the hit triangle in the lighting space
// (camera-relative world), for any instance.
float3 rt_hit_normal(RtHit h) {
  InstInfo ii = inst_info(h.inst);
  if ((ii.meta & kInfoLegacy) != 0u) return rt_tri_normal(h.geom, h.prim);
  uint stride = ii.meta & 0xffu, bytes;
  if ((ii.meta & kInfoPool) != 0u) gMeshPool.GetDimensions(bytes);
  else gOpaqueSoup.GetDimensions(bytes);
  if (stride < 16u || h.prim >= ii.tris || ii.base + (h.prim * 3u + 3u) * stride > bytes) return 0.0.xxx;
  float3 p0 = inst_pos(ii, h.prim, 0), p1 = inst_pos(ii, h.prim, 1), p2 = inst_pos(ii, h.prim, 2);
  if ((ii.meta & kInfoPool) != 0u) {
    p0 = mul(h.o2w, float4(p0, 1.0));
    p1 = mul(h.o2w, float4(p1, 1.0));
    p2 = mul(h.o2w, float4(p2, 1.0));
  }
  float3 n = cross(p1 - p0, p2 - p0);
  return all(isfinite(n)) ? n : 0.0.xxx;
}

// Wachter & Binder, "A Fast and Robust Method for Avoiding Self-Intersection"
// (Ray Tracing Gems, ch. 6): moves p off its surface along n (facing the side the
// ray leaves on) by a fixed number of float ULPs, or a fixed distance near 0.
float3 rt_offset_ray(float3 p, float3 n) {
  const float origin = 1.0 / 32.0, float_scale = 1.0 / 65536.0, int_scale = 256.0;
  int3 of_i = int3(int_scale * n);
  float3 p_i = asfloat(asint(p) + int3(p.x < 0.0 ? -of_i.x : of_i.x, p.y < 0.0 ? -of_i.y : of_i.y, p.z < 0.0 ? -of_i.z : of_i.z));
  return float3(abs(p.x) < origin ? p.x + float_scale * n.x : p_i.x,
                abs(p.y) < origin ? p.y + float_scale * n.y : p_i.y,
                abs(p.z) < origin ? p.z + float_scale * n.z : p_i.z);
}

// Origin of a secondary ray in direction `dir` from the surface at pixel fp (P its
// depth-buffer point, Ns its shading normal, `foot` the pixel's footprint there).
//   rt_origin=0: P + Ns x rt_bias x |P| (towards the ray's side of Ns), the old way;
//   rt_origin=1: the exact TLAS surface point under the pixel, pushed off along the
//     geometric normal towards the ray's side by Wachter & Binder's ULP offset.
//     Where the shading and the geometric normal disagree about the side (the
//     shadow terminator of coarse meshes) or no triangle matched the depth buffer,
//     the offset is 2 footprints, along the shading normal resp. the depth normal.
// `o`: rt_origin_at(fp), read once per pixel by passes that trace several rays.
float4 rt_origin_at(uint2 fp) { return pc.rt_origin != 0u ? rtOrigin[fp] : float4(0, 0, 0, kOriginNoHit); }
float3 rt_ray_origin_o(float4 o, float3 P, float3 Ns, float foot, float3 dir) {
  float ss = dot(Ns, dir) < 0.0 ? -1.0 : 1.0;
  if (pc.rt_origin == 0u) return P + Ns * (length(P) * pc.rt_bias * ss);
  float3 r;
  float sg = dot(o.xyz, dir) < 0.0 ? -1.0 : 1.0;
  if (sg != ss) r = P + Ns * (2.0 * foot * ss);
  else if (!(o.w < kOriginNoHit)) r = P + o.xyz * (2.0 * foot * sg);
  else r = rt_offset_ray(P * (1.0 + o.w), o.xyz * sg);
  // A non-finite ray origin is undefined behaviour for ray queries.
  return all(isfinite(r)) ? r : P + Ns * (2.0 * foot * ss);
}
float3 rt_ray_origin(uint2 fp, float3 P, float3 Ns, float foot, float3 dir) {
  return rt_ray_origin_o(rt_origin_at(fp), P, Ns, foot, dir);
}

// Closest alpha-tested hit distance, or -1.
float rt_closest(float3 o, float3 d, float tmax, RtCone cone) {
  RayQuery<RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES> q;
  RayDesc r;
  r.Origin = o;
  r.Direction = d;
  r.TMin = 0.0;
  r.TMax = tmax;
  q.TraceRayInline(gScene, 0, 0xFF, r);
  while (q.Proceed()) {
    if (q.CandidateType() == CANDIDATE_NON_OPAQUE_TRIANGLE && RT_ALPHA_CANDIDATE(q, cone)) q.CommitNonOpaqueTriangleHit();
  }
  return q.CommittedStatus() == COMMITTED_TRIANGLE_HIT ? q.CommittedRayT() : -1.0;
}
