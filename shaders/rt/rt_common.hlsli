// Track D: ray queries against the host's TLAS, with alpha-tested candidates
// (phase 3, docs/TRACK_D_RT.md). Included by the core's ray-traced passes.
//
// Geometry 0 of the BLAS is opaque. Geometry 1 (rt_alpha=1) holds the draws
// whose depth-prepass fragment shader alpha tests (layer/alphatest.cpp); it is
// not opaque, so its triangles come back as candidates, and alpha_pass()
// reproduces the game's test at the hit: UV interpolated from the captured
// soup, the texture the game sampled, its cut-off. Set 1 is the host's.
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
[[vk::binding(2, 1)]] Texture2D<float4> gAlphaTex[256];        // indirection (vt) or the alpha texture (tex)
[[vk::binding(3, 1)]] Texture2DArray<float4> gAlphaCache[4];   // virtual texture tile caches
[[vk::binding(4, 1)]] SamplerState gAlphaWrap;
[[vk::binding(5, 1)]] SamplerState gAlphaClamp;

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

// True if the alpha-tested triangle `prim` of geometry 1 is solid at `bary`.
// `footprint`: world-space width of the ray's footprint at the hit.
// Byte address of the record of alpha-tested triangle `prim` (0 if none).
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

// Debug: the family of the draw (1 vt, 2 tex, 0 unknown).
uint alpha_kind(uint prim) {
  uint a = find_rec(prim);
  return a ? gAlphaRec.Load(a + 8) : 0;
}

bool alpha_pass(uint prim, float2 bary, float footprint) {
  uint a = find_rec(prim);
  if (a == 0) return true;
  uint4 h0 = gAlphaRec.Load4(a);        // first_tri, tris, kind, tex
  uint4 h1 = gAlphaRec.Load4(a + 16);   // cache, channel, cut
  if (prim < h0.x || prim - h0.x >= h0.y) return true;
  uint vb = prim * 72;
  float3 p0 = asfloat(gAlphaSoup.Load3(vb)), p1 = asfloat(gAlphaSoup.Load3(vb + 24)), p2 = asfloat(gAlphaSoup.Load3(vb + 48));
  float2 u0 = asfloat(gAlphaSoup.Load2(vb + 16)), u1 = asfloat(gAlphaSoup.Load2(vb + 40)), u2 = asfloat(gAlphaSoup.Load2(vb + 64));
  float2 uv = u0 + (u1 - u0) * bary.x + (u2 - u0) * bary.y;
  float2 e1 = u1 - u0, e2 = u2 - u0;
  float area_uv = abs(e1.x * e2.y - e1.y * e2.x);
  float area_w = length(cross(p1 - p0, p2 - p0));
  float g = footprint * sqrt(area_uv / max(area_w, 1e-12));   // uv change across the footprint
  if (!all(isfinite(uv)) || !isfinite(g)) return true;
  uint tex = min(h0.w, 255u), cache = min(h1.x, 3u);
  float alpha = h0.z == 1 ? vt_alpha(a, uv, g, tex, cache, min(h1.y, 3u)) : tex_alpha(a, uv, g, tex, h1.y);
  return alpha >= asfloat(h1.z);
}

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
    if (q.CandidateType() == CANDIDATE_NON_OPAQUE_TRIANGLE &&
        alpha_pass(q.CandidatePrimitiveIndex(), q.CandidateTriangleBarycentrics(),
                   cone.f0 + cone.spread * q.CandidateTriangleRayT()))
      q.CommitNonOpaqueTriangleHit();
  }
  return q.CommittedStatus() == COMMITTED_TRIANGLE_HIT;
}

// Debug (rt_debug=3/5): the closest hit, its geometry (0 opaque, 1 alpha-tested),
// and the family of the nearest alpha-tested candidate along the ray, whether
// it was let through or not.
struct RtDebugHit { float t; uint geom; uint near_kind; uint hit_kind; };
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
    uint prim = q.CandidatePrimitiveIndex();
    if (ct < near_t) {
      near_t = ct;
      h.near_kind = alpha_kind(prim);
    }
    if (alpha_pass(prim, q.CandidateTriangleBarycentrics(), cone.f0 + cone.spread * ct)) q.CommitNonOpaqueTriangleHit();
  }
  bool hit = q.CommittedStatus() == COMMITTED_TRIANGLE_HIT;
  h.t = hit ? q.CommittedRayT() : -1.0;
  h.geom = hit ? q.CommittedGeometryIndex() : 0;
  h.hit_kind = hit && h.geom == 1 ? alpha_kind(q.CommittedPrimitiveIndex()) : 0;
  return h;
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
    if (q.CandidateType() == CANDIDATE_NON_OPAQUE_TRIANGLE &&
        alpha_pass(q.CandidatePrimitiveIndex(), q.CandidateTriangleBarycentrics(),
                   cone.f0 + cone.spread * q.CandidateTriangleRayT()))
      q.CommitNonOpaqueTriangleHit();
  }
  return q.CommittedStatus() == COMMITTED_TRIANGLE_HIT ? q.CommittedRayT() : -1.0;
}
