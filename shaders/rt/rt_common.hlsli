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

// The scene stamp of this set's build (layer/alphatest.cpp): uint 0 the generation
// the host expects, uint 1 the one the build's command buffer wrote when it ran.
// Different: that build never ran before this pass (its command buffer dropped
// without a submit, or submitted later): its TLAS is not traced (every ray misses).
[[vk::binding(9, 1)]] ByteAddressBuffer gSceneStamp;
bool rt_scene_ok() {
  uint2 s = gSceneStamp.Load2(0);
  return s.x != 0u && s.x == s.y;
}

static const uint kRecBytes = 208;

uint rec_addr(uint i) { return 16 + i * kRecBytes; }

// Bounds (defence in depth; the device need not have robust buffer access, and
// an out-of-range descriptor read can fault). A TLAS is traced with its own
// build's set 1 (rtscene.cpp scene slots), so these should never trigger; they
// turn a mismatch into a miss or an "unknown" alpha instead of a wild read.
// Record at byte address `a` lies inside gAlphaRec (overflow-safe).
bool rec_in_range(uint a) {
  uint rbytes;
  gAlphaRec.GetDimensions(rbytes);
  return a >= 16u && rbytes >= kRecBytes && a <= rbytes - kRecBytes;
}
// Triangle `prim` (3 vertices of `stride` bytes from byte `base`) lies inside a
// buffer of `bytes` bytes (overflow-safe).
bool tri_in_range(uint base, uint prim, uint stride, uint bytes) {
  return stride != 0u && base <= bytes && prim < (bytes - base) / (3u * stride);
}

// Tile-set constant: matrix m (0/1), column c, row r of the SSBO entry (row-major mat4).
float vt_t(uint a, uint m, uint c, uint r) { return asfloat(gAlphaRec.Load(a + 80 + 4 * (m * 16 + r * 4 + c))); }

// ---- alpha-test policy (docs/TRACK_D_RT.md 7.1, "Alpha test at secondary hits") ----
// gAlphaRec header uint 2, written per frame by layer/alphatest.cpp from bg3rtx.ini
// (live A/B switches; every bit set is the default):
//   kAlphaCutEff      rt_alpha_cut=1: the record's effective cut-off (the strictest
//                     constant of the shader's discard tests on the alpha), not the
//                     first test's (an early-out at 0.001 / 0.01 on 76 vt shaders);
//   kAlphaStochastic  rt_alpha_stochastic=1: where the texel footprint has blurred the
//                     mask (level >= 1 above mip 0), compare against a hashed threshold
//                     instead of the hard cut-off: P(solid) -> the filtered alpha, i.e.
//                     the coverage, so partial coverage stays unbiased under RR;
//   kAlphaUnknownClear rt_alpha_unknown=1: a candidate whose alpha cannot be evaluated
//                     (no record or UV, a non-resident tile set) lets visibility rays
//                     (GI, AO, reflections) through; shadow rays still treat it as solid.
static const uint kAlphaCutEff = 1u, kAlphaStochastic = 2u, kAlphaUnknownClear = 4u;
uint alpha_flags() { return gAlphaRec.Load(8); }

// Class of the running ray query, for the counters and the unknown-case policy:
// 0 visibility (closest hit: GI, AO, reflections, origins, debug), 1 shadow (any hit).
static uint g_rt_class = 0u;

// Alpha-test counters (rt_stats=1, core/gi.cpp): 80 + 16 x class + k, k:
//   0 candidates; solid without evaluation: 1 no record, 2 no UV (stride < 24),
//   3 prim >= tris, 4 non-finite uv/footprint, 5 vt tile set not resident;
//   6/7 vt solid/clear, 8/9 tex solid/clear; 10 records whose first cut-off is < 0.1,
//   11 of those solid; 12 level >= 2, 13 level >= 4 (blur level of the alpha read,
//   log2 mip-0 texels); 14 stochastic (level > 1); 15 vt page from a coarser level
//   than requested (not resident at the cone's mip).
void alpha_stat(uint k, bool cond, uint bin) {
  if (pc.rt_stats != 0u) stat_add(80u + 16u * g_rt_class + k, cond, bin);
}

// A candidate whose alpha is unknown: solid (true) or let through.
bool alpha_unknown(uint flags) { return (flags & kAlphaUnknownClear) == 0u || g_rt_class == 1u; }

// Uniform [0, 1) per ray and hit: the hit's uv bits, its triangle and the frame.
float alpha_hash(float2 uv, uint prim) {
  uint3 h = pcg3d(uint3(asuint(uv.x), asuint(uv.y), prim ^ (pc.frame * 0x9E3779B9u) ^ (g_rt_class << 31)));
  return float(h.x >> 8) * (1.0 / 16777216.0);
}

// An alpha read: the value, the blur level of the data it came from (log2 of mip-0
// texels per texel: the mip, or the level of the coarser page that stood in), and
// whether it could be read at all.
struct AlphaRead { float a; float lvl; bool ok; bool coarser; };

// BG3's Granite lookup, as in its prepass fragment shaders (e.g. 84d5df19):
// mip from the uv footprint, page entry from the indirection texture, texel in
// the tile cache; the tile set's albedo layer `col` carries the alpha. Without
// screen derivatives the footprint is the ray cone's, isotropic. The indirection
// entry of a page that is not resident at that mip points at the finest resident
// ancestor (its tile scale, e & 15, is then smaller): the read clamps itself to
// the finest resident level, and `lvl` reports the level actually used.
AlphaRead vt_alpha(uint a, float2 uv, float g, uint tex, uint cache, uint col) {
  AlphaRead r;
  r.a = 1.0;
  r.lvl = 0.0;
  r.coarser = false;
  r.ok = vt_t(a, 0, 2, 3) == 2202.0;   // the game's "tile set resident" sentinel
  if (!r.ok) return r;
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
  // Stay inside the indirection texture (constants from another tile set, or a
  // mip it does not have, would fetch out of bounds).
  uint iw, ih, ilevels;
  gAlphaTex[NonUniformResourceIndex(tex)].GetDimensions(0, iw, ih, ilevels);
  mip = min(mip, float(max(ilevels, 1u) - 1u));
  int2 ipi = clamp(int2(ip), int2(0, 0), int2(max(iw >> uint(mip), 1u) - 1u, max(ih >> uint(mip), 1u) - 1u));
  uint e = asuint(gAlphaTex[NonUniformResourceIndex(tex)].Load(int3(ipi, int(mip))).x);
  float sc = pow(2.0, float(e & 15u));
  // Pages across the tile set at the level the entry points to vs at mip 0.
  float used = max(log2(max(dims.x, 1.0)) - float(e & 15u), 0.0);
  r.lvl = max(used, mip);
  r.coarser = used > mip + 0.5;
  float2 s163 = float2(sc, sc * vt_t(a, 0, 1, 3));
  float2 s168 = float2(vt_t(a, 0, 0, 2), vt_t(a, 0, 1, 2));
  float2 s175 = float2(vt_t(a, 1, 0, 0), vt_t(a, 1, 1, 0));
  float2 c = (frac(u * s163) * s168 + float2(float((e >> 14) & 1023u), float((e >> 4) & 1023u))) * s175 +
             float2(vt_t(a, 1, 2, col), vt_t(a, 1, 3, col));
  r.a = gAlphaCache[NonUniformResourceIndex(cache)].SampleLevel(gAlphaClamp, float3(c, float((e >> 24) & 127u)), 0).w;
  return r;
}

AlphaRead tex_alpha(uint a, float2 uv, float g, uint tex, uint ch) {
  float2 sc = asfloat(gAlphaRec.Load2(a + 32));
  uint w, h, levels;
  gAlphaTex[NonUniformResourceIndex(tex)].GetDimensions(0, w, h, levels);
  float lod = clamp(log2(max(g * max(abs(sc.x), abs(sc.y)) * float(max(w, h)), 1e-8)), 0.0, float(levels) - 1.0);
  float4 v = gAlphaTex[NonUniformResourceIndex(tex)].SampleLevel(gAlphaWrap, uv * sc, lod);
  AlphaRead r;
  r.a = ch == 0 ? v.x : ch == 1 ? v.y : ch == 2 ? v.z : v.w;
  r.lvl = lod;
  r.ok = true;
  r.coarser = false;
  return r;
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
struct InstInfo { uint base, meta, rec, tris; };

InstInfo inst_info(uint inst) {
  InstInfo r;
  uint bytes;
  gInstInfo.GetDimensions(bytes);
  // Past the InstInfo of this TLAS: no vertices, no record (never the legacy lookup).
  uint4 v = inst < bytes / 16u ? gInstInfo.Load4(inst * 16u) : uint4(0, 0, 0, 0);
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
  uint rbytes;
  gAlphaRec.GetDimensions(rbytes);
  uint n = min(gAlphaRec.Load(0), rbytes >= 16u ? (rbytes - 16u) / kRecBytes : 0u);
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
bool alpha_eval(uint a, float3 p0, float3 p1, float3 p2, float2 u0, float2 u1, float2 u2, float2 bary, float footprint,
                uint prim) {
  const uint flags = alpha_flags();
  uint4 h0 = gAlphaRec.Load4(a);        // first_tri, tris, kind, tex
  uint4 h1 = gAlphaRec.Load4(a + 16);   // cache, channel, cut (effective), cut of the first test
  float2 uv = u0 + (u1 - u0) * bary.x + (u2 - u0) * bary.y;
  float2 e1 = u1 - u0, e2 = u2 - u0;
  float area_uv = abs(e1.x * e2.y - e1.y * e2.x);
  float area_w = length(cross(p1 - p0, p2 - p0));
  float g = footprint * sqrt(area_uv / max(area_w, 1e-12));   // uv change across the footprint
  if (!all(isfinite(uv)) || !isfinite(g)) {
    alpha_stat(4, true, prim);
    return alpha_unknown(flags);
  }
  uint tex = min(h0.w, 4095u), cache = min(h1.x, 15u);
  const bool vt = h0.z == 1;
  AlphaRead r;
  if (vt) r = vt_alpha(a, uv, g, tex, cache, min(h1.y, 3u));
  else r = tex_alpha(a, uv, g, tex, h1.y);
  if (!r.ok) {
    alpha_stat(5, true, prim);
    return alpha_unknown(flags);
  }
  const float cut_first = asfloat(h1.w);
  const float cut = (flags & kAlphaCutEff) != 0u ? asfloat(h1.z) : cut_first;
  // Hard cut-off where the read is as sharp as the game's (levels 0-1); towards
  // a hashed threshold over the next level, where it is a blurred coverage.
  const float k = (flags & kAlphaStochastic) != 0u ? saturate(r.lvl - 1.0) : 0.0;
  const float thr = k > 0.0 ? lerp(cut, alpha_hash(uv, prim), k) : cut;
  const bool solid = r.a >= thr;
  if (pc.rt_stats != 0u) {
    alpha_stat(6, vt && solid, prim);
    alpha_stat(7, vt && !solid, prim);
    alpha_stat(8, !vt && solid, prim);
    alpha_stat(9, !vt && !solid, prim);
    alpha_stat(10, cut_first < 0.1, prim);
    alpha_stat(11, cut_first < 0.1 && solid, prim);
    alpha_stat(12, r.lvl >= 2.0, prim);
    alpha_stat(13, r.lvl >= 4.0, prim);
    alpha_stat(14, k > 0.0, prim);
    alpha_stat(15, r.coarser, prim);
  }
  return solid;
}

// Legacy: true if the alpha-tested triangle `prim` of geometry 1 is solid at `bary`.
bool alpha_pass(uint prim, float2 bary, float footprint) {
  alpha_stat(0, true, prim);
  uint a = find_rec(prim);
  if (a == 0) {
    alpha_stat(1, true, prim);
    return alpha_unknown(alpha_flags());
  }
  uint4 h0 = gAlphaRec.Load4(a);
  if (prim < h0.x || prim - h0.x >= h0.y) {
    alpha_stat(1, true, prim);
    return alpha_unknown(alpha_flags());
  }
  uint vb = prim * 72, sbytes;
  gAlphaSoup.GetDimensions(sbytes);
  if (vb + 72u > sbytes) {
    alpha_stat(3, true, prim);
    return alpha_unknown(alpha_flags());
  }
  float3 p0 = asfloat(gAlphaSoup.Load3(vb)), p1 = asfloat(gAlphaSoup.Load3(vb + 24)), p2 = asfloat(gAlphaSoup.Load3(vb + 48));
  float2 u0 = asfloat(gAlphaSoup.Load2(vb + 16)), u1 = asfloat(gAlphaSoup.Load2(vb + 40)), u2 = asfloat(gAlphaSoup.Load2(vb + 64));
  return alpha_eval(a, p0, p1, p2, u0, u1, u2, bary, footprint, prim);
}

// A non-opaque candidate of any instance: solid at `bary`?
bool alpha_pass_hit(uint inst, uint geom, uint prim, float2 bary, float footprint, float3x4 o2w) {
  InstInfo ii = inst_info(inst);
  if ((ii.meta & kInfoLegacy) != 0u) return geom == 1u ? alpha_pass(prim, bary, footprint) : true;
  alpha_stat(0, true, prim);
  if ((ii.meta & kInfoAlpha) == 0u || ii.rec == 0u || prim >= ii.tris || (ii.meta & 0xffu) < 24u) {
    if (pc.rt_stats != 0u) {
      const bool norec = (ii.meta & kInfoAlpha) == 0u || ii.rec == 0u;
      alpha_stat(1, norec, prim);
      alpha_stat(2, !norec && (ii.meta & 0xffu) < 24u, prim);
      alpha_stat(3, !norec && (ii.meta & 0xffu) >= 24u, prim);
    }
    return alpha_unknown(alpha_flags());
  }
  // Never read past the vertex buffer or the records: no robust buffer access is guaranteed.
  uint vbytes;
  if ((ii.meta & kInfoPool) != 0u) gMeshPool.GetDimensions(vbytes);
  else gOpaqueSoup.GetDimensions(vbytes);
  if (!tri_in_range(ii.base, prim, ii.meta & 0xffu, vbytes) || !rec_in_range(ii.rec)) {
    alpha_stat(3, true, prim);
    return alpha_unknown(alpha_flags());
  }
  float3 p0 = inst_pos(ii, prim, 0), p1 = inst_pos(ii, prim, 1), p2 = inst_pos(ii, prim, 2);
  if ((ii.meta & kInfoPool) != 0u) {
    p0 = mul(o2w, float4(p0, 1.0));
    p1 = mul(o2w, float4(p1, 1.0));
    p2 = mul(o2w, float4(p2, 1.0));
  }
  return alpha_eval(ii.rec, p0, p1, p2, inst_uv(ii, prim, 0), inst_uv(ii, prim, 1), inst_uv(ii, prim, 2), bary, footprint,
                    prim);
}

// Debug: the family of the draw behind a hit (1 vt, 2 tex, 0 not alpha-tested).
uint alpha_kind(uint prim) {
  uint a = find_rec(prim);
  return a ? gAlphaRec.Load(a + 8) : 0;
}
uint alpha_kind_hit(uint inst, uint geom, uint prim) {
  InstInfo ii = inst_info(inst);
  if ((ii.meta & kInfoLegacy) != 0u) return geom == 1u ? alpha_kind(prim) : 0u;
  return (ii.meta & kInfoAlpha) != 0u && rec_in_range(ii.rec) ? gAlphaRec.Load(ii.rec + 8) : 0u;
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
  g_rt_class = 1u;
  if (!rt_scene_ok()) return false;
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
  g_rt_class = 0u;
  RtDebugHit h;
  h.near_kind = 0;
  if (!rt_scene_ok()) {
    h.t = -1.0;
    h.alpha = false;
    h.hit_kind = 0u;
    return h;
  }
  RayQuery<RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES> q;
  RayDesc r;
  r.Origin = o;
  r.Direction = d;
  r.TMin = 0.0;
  r.TMax = tmax;
  q.TraceRayInline(gScene, 0, 0xFF, r);
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
  g_rt_class = 0u;
  if (!rt_scene_ok()) {
    RtHit m;
    m.t = -1.0;
    m.geom = m.prim = m.inst = 0u;
    m.o2w = float3x4(1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0);
    return m;
  }
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
  if (stride < 16u || h.prim >= ii.tris || !tri_in_range(ii.base, h.prim, stride, bytes)) return 0.0.xxx;
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
  g_rt_class = 0u;
  if (!rt_scene_ok()) return -1.0;
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
