// EN2: material data at secondary ray hits (docs/TRACK_D_RT.md 13, layer/hitmat.cpp).
//
//   HitMaterial rt_hit_material(RtHitEx h, float3 dir, RtCone cone)
//     -> albedo (base colour), N, roughness, metalness, reflectance, `known` bits, family
//
// The data is the game's own, per draw of its G-buffer pass:
//   - which texture/tile-set layer/component feeds RT1.rgb (base colour), RT2.x
//     (perceptual roughness), RT2.y (reflectance) and RT2.z (metalness): read offline
//     from each G-buffer fragment shader (scripts/material_table.py ->
//     shaders/rt/material_fs.txt);
//   - per draw: the views the game bound (Granite indirection + tile caches, or a plain
//     texture), the UBO constants (uv transform, tile-set index, scalar factors) and the
//     tile set's SSBO entry, read by the layer at the draw from the game's mapped memory;
//   - the UV at the hit: the draw's own UV output (XFB of the G-buffer pass, written
//     into the scene's vertex data, layer/hitmat.cpp), interpolated at the barycentrics.
// A field whose source the shader does not reduce to one of those is "unknown": its
// `known` bit stays 0 and the value is 0, never a guess (docs/QUALITY.md R0). Consumers
// fall back explicitly (the game's probe, the screen, a radiance cache).
//
// N is the geometric normal of the hit triangle, facing the ray's origin. The normal map
// at hits is not read: the capture holds no tangent frame (kHmNormalMap stays 0).
//
// Set 1 (the host's, layer/alphatest.cpp) bindings 10-12, per scene slot:
//   10 gMatRec   header (16 B: records, instances, frame, flags), then 256 B per record;
//   11 gMatInst  8 B per TLAS instance: record index (0xffffffff: none), family | flags << 16;
//   12 gMatStats coverage per family and the on-screen self-check sums (rt_stats / hm stats).
#pragma once
#include "rt_common.hlsli"

[[vk::binding(10, 1)]] ByteAddressBuffer gMatRec;
[[vk::binding(11, 1)]] ByteAddressBuffer gMatInst;
[[vk::binding(12, 1)]] RWByteAddressBuffer gMatStats;

static const uint kMatRecBytes = 256u;
static const uint kHmAlbedo = 1u, kHmRough = 2u, kHmMetal = 4u, kHmRefl = 8u, kHmNormalMap = 16u;
// Source kinds of a field (record uint4 .x bits 0-3): layer/hitmat.cpp kSrc*.
static const uint kSrcNone = 0u, kSrcVt = 1u, kSrcTex = 2u, kSrcConst = 3u;
// Instance flags (gMatInst .y >> 16): the UV at the instance's vertices is the draw's own.
static const uint kHmInstUv = 1u;
// Families (gMatInst .y & 0xffff): 0 no G-buffer draw joined to the instance, 1 the legacy
// single-BLAS scene (no per-instance data), 2.. one per G-buffer fragment shader.
static const uint kFamUnjoined = 0u, kFamLegacy = 1u, kFamCount = 1024u;
// gMatStats layout (uints): [0, 4 x kFamCount) per family {hits, albedo, rough, metal};
// then kHmQa.. the self-check sums (rt_reflections.hlsl).
static const uint kHmQa = 4u * kFamCount;

struct HitMaterial {
  float3 albedo;   // base colour (RT1.rgb), linear
  float3 N;        // unit geometric normal, facing -dir
  float rough;     // perceptual roughness (RT2.x)
  float metal;     // metalness (RT2.z)
  float refl;      // specular reflectance (RT2.y: F0 = 0.16 refl^2 for dielectrics)
  uint known;      // kHm* bits
  uint family;     // coverage bucket
  float lvl;       // blur level of the base colour read (log2 mip-0 texels per texel)
};

// A closest hit with its barycentrics (rt_closest_hit plus what a material read needs).
struct RtHitEx {
  RtHit h;
  float2 bary;
};

RtHitEx rt_closest_hit_ex(float3 o, float3 d, float tmax, RtCone cone) {
  g_rt_class = 0u;
  RtHitEx x = (RtHitEx)0;
  x.h.t = -1.0;
  x.h.o2w = float3x4(1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0);
  if (!rt_scene_ok() || !rt_ray_valid(o, d, tmax)) return x;
  RayQuery<RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES> q;
  RayDesc r = (RayDesc)0;
  r.Origin = o;
  r.Direction = d;
  r.TMin = 0.0;
  r.TMax = tmax;
  q.TraceRayInline(gScene, 0, 0xFF, r);
  uint cand_n = 0;
  while (q.Proceed()) {
    if (++cand_n > kMaxCandidates) {
      guard_count(kGuardLoopCap);
      q.Abort();
      break;
    }
    if (q.CandidateType() == CANDIDATE_NON_OPAQUE_TRIANGLE && RT_ALPHA_CANDIDATE(q, cone)) q.CommitNonOpaqueTriangleHit();
  }
  if (q.CommittedStatus() != COMMITTED_TRIANGLE_HIT) return x;
  x.h.t = q.CommittedRayT();
  x.h.geom = q.CommittedGeometryIndex();
  x.h.prim = q.CommittedPrimitiveIndex();
  x.h.inst = q.CommittedInstanceIndex();
  x.h.o2w = q.CommittedObjectToWorld3x4();
  x.bary = q.CommittedTriangleBarycentrics();
  return x;
}

// Record field `i` (0 albedo, 1 roughness, 2 metalness, 3 reflectance): uint4
//   x: kind | swizzle << 4 (2 bits per component, 3 components) | ind channel << 12 | column << 16
//   y: view slot (tile cache in gAlphaCache, or texture in gAlphaTex)
//   z: constant (asfloat), w: 0
uint4 hm_field(uint a, uint i) { return gMatRec.Load4(a + 16u + 16u * i); }
// Tile-set constant of record `a`: matrix m (0/1), column c, row r (as rt_common.hlsli vt_t).
float hm_t(uint a, uint m, uint c, uint r) { return asfloat(gMatRec.Load(a + 128u + 4u * (m * 16u + r * 4u + c))); }

// Granite lookup of record `a`, as the game's G-buffer shaders do it (e.g. 14678345) and
// vt_alpha() does for the alpha: the indirection texture `tex`'s channel `ich` holds the
// page entry of the tile set's layer `col` in tile cache `cache`. Without screen
// derivatives the footprint `g` (uv units) is the ray cone's. Returns the texel and the
// blur level; ok = false where the tile set is not resident.
float4 hm_vt(uint a, float2 uv, float g, uint tex, uint cache, uint ich, uint col, out float lvl, out bool ok) {
  lvl = 0.0;
  ok = hm_t(a, 0, 2, 3) == 2202.0;   // the game's "tile set resident" sentinel
  if (!ok) return 0.0.xxxx;
  float4 P0 = asfloat(gMatRec.Load4(a + 96u));
  float4 P1 = asfloat(gMatRec.Load4(a + 112u));
  float2 u = frac(uv) * P0.zw + P0.xy;
  float2 s = float2(hm_t(a, 0, 2, 0), hm_t(a, 0, 3, 0));
  float2 dx = P0.zw * float2(g, 0.0) * s, dy = P0.zw * float2(0.0, g) * s;
  float d1 = dot(dx, dx), d2 = dot(dy, dy);
  float l = log2(max(max(d1, d2), 1e-30));
  float aniso = min(0.5 * l - 0.5 * log2(max(min(d1, d2), 1e-30)), hm_t(a, 0, 1, 0));
  float mip = floor(min(max(0.5 * l - aniso - 0.5, 0.0), P1.x) + 0.5);
  float2 dims = float2(hm_t(a, 0, 0, 3), hm_t(a, 0, 0, 3) * hm_t(a, 0, 1, 3));
  float2 ip = floor(u * dims * pow(0.5, mip));
  uint iw, ih, ilevels;
  gAlphaTex[NonUniformResourceIndex(tex)].GetDimensions(0, iw, ih, ilevels);
  mip = min(mip, float(max(ilevels, 1u) - 1u));
  int2 ipi = clamp(int2(ip), int2(0, 0), int2(max(iw >> uint(mip), 1u) - 1u, max(ih >> uint(mip), 1u) - 1u));
  float4 ent = gAlphaTex[NonUniformResourceIndex(tex)].Load(int3(ipi, int(mip)));
  uint e = asuint(ich == 0u ? ent.x : ich == 1u ? ent.y : ich == 2u ? ent.z : ent.w);
  float sc = pow(2.0, float(e & 15u));
  float used = max(log2(max(dims.x, 1.0)) - float(e & 15u), 0.0);
  lvl = max(used, mip);
  float2 s163 = float2(sc, sc * hm_t(a, 0, 1, 3));
  float2 s168 = float2(hm_t(a, 0, 0, 2), hm_t(a, 0, 1, 2));
  float2 s175 = float2(hm_t(a, 1, 0, 0), hm_t(a, 1, 1, 0));
  float2 c = (frac(u * s163) * s168 + float2(float((e >> 14) & 1023u), float((e >> 4) & 1023u))) * s175 +
             float2(hm_t(a, 1, 2, col), hm_t(a, 1, 3, col));
  return gAlphaCache[NonUniformResourceIndex(cache)].SampleLevel(gAlphaClamp, float3(c, float((e >> 24) & 127u)), 0);
}

// A plain texture at uv x the record's uv scale, at the ray cone's mip (as tex_alpha()).
// The game's sampler is not readable: ours repeats, like the alpha path.
float4 hm_tex(uint a, float2 uv, float g, uint tex, out float lvl) {
  float2 sc = asfloat(gMatRec.Load2(a + 96u));
  uint w, h, levels;
  gAlphaTex[NonUniformResourceIndex(tex)].GetDimensions(0, w, h, levels);
  lvl = clamp(log2(max(g * max(abs(sc.x), abs(sc.y)) * float(max(w, h)), 1e-8)), 0.0, float(levels) - 1.0);
  return gAlphaTex[NonUniformResourceIndex(tex)].SampleLevel(gAlphaWrap, uv * sc, lvl);
}

float hm_comp(float4 v, uint k) { return k == 0u ? v.x : k == 1u ? v.y : k == 2u ? v.z : v.w; }

// One field: up to 3 components (albedo) from its source; false if it cannot be read.
bool hm_eval(uint a, uint i, float2 uv, float g, uint ind, out float3 v, out float lvl) {
  uint4 f = hm_field(a, i);
  const uint kind = f.x & 15u;
  v = 0.0.xxx;
  lvl = 0.0;
  if (kind == kSrcConst) {
    v = asfloat(f.z).xxx;
    return true;
  }
  float4 s;
  bool ok = true;
  if (kind == kSrcVt) s = hm_vt(a, uv, g, min(ind, 4095u), min(f.y, 15u), (f.x >> 12) & 3u, (f.x >> 16) & 3u, lvl, ok);
  else if (kind == kSrcTex) s = hm_tex(a, uv, g, min(f.y, 4095u), lvl);
  else return false;
  if (!ok || !all(isfinite(s))) return false;
  const uint sw = f.x >> 4;
  v = float3(hm_comp(s, sw & 3u), hm_comp(s, (sw >> 2) & 3u), hm_comp(s, (sw >> 4) & 3u));
  return true;
}

// The instance's material entry: x record index (0xffffffff none), y family | flags << 16.
uint2 hm_inst(uint inst) {
  uint n = gMatRec.Load(4), bytes;
  gMatInst.GetDimensions(bytes);
  if (inst >= n || inst >= bytes / 8u) return uint2(0xffffffffu, kFamUnjoined);
  return gMatInst.Load2(inst * 8u);
}

HitMaterial rt_hit_material(RtHitEx x, float3 dir, RtCone cone) {
  HitMaterial m = (HitMaterial)0;
  m.family = kFamUnjoined;
  RtHit h = x.h;
  float3 n = rt_hit_normal(h);
  m.N = dot(n, n) > 0.0 ? normalize(n) : -dir;
  if (dot(m.N, dir) > 0.0) m.N = -m.N;
  if (h.t < 0.0) return m;
  InstInfo ii = inst_info(h.inst);
  if ((ii.meta & kInfoLegacy) != 0u) {
    m.family = kFamLegacy;
    return m;
  }
  uint2 e = hm_inst(h.inst);
  m.family = min(e.y & 0xffffu, kFamCount - 1u);
  const uint nrec = gMatRec.Load(0);
  uint rbytes;
  gMatRec.GetDimensions(rbytes);
  if (e.x >= nrec || ((e.y >> 16) & kHmInstUv) == 0u || (ii.meta & 0xffu) < 24u || h.prim >= ii.tris) return m;
  const uint a = 16u + e.x * kMatRecBytes;
  if (a + kMatRecBytes > rbytes) return m;
  uint vbytes;
  if ((ii.meta & kInfoPool) != 0u) gMeshPool.GetDimensions(vbytes);
  else gOpaqueSoup.GetDimensions(vbytes);
  if (!tri_in_range(ii.base, h.prim, ii.meta & 0xffu, vbytes)) {
    guard_count(kGuardSet1);
    return m;
  }
  float2 u0 = inst_uv(ii, h.prim, 0), u1 = inst_uv(ii, h.prim, 1), u2 = inst_uv(ii, h.prim, 2);
  float3 p0 = inst_pos(ii, h.prim, 0), p1 = inst_pos(ii, h.prim, 1), p2 = inst_pos(ii, h.prim, 2);
  if ((ii.meta & kInfoPool) != 0u) {
    p0 = mul(h.o2w, float4(p0, 1.0));
    p1 = mul(h.o2w, float4(p1, 1.0));
    p2 = mul(h.o2w, float4(p2, 1.0));
  }
  float2 uv = u0 + (u1 - u0) * x.bary.x + (u2 - u0) * x.bary.y;
  float2 e1 = u1 - u0, e2 = u2 - u0;
  float area_uv = abs(e1.x * e2.y - e1.y * e2.x);
  float area_w = length(cross(p1 - p0, p2 - p0));
  // The cone's footprint across the surface, widened by the incidence (1 / |cos|).
  float cosi = max(abs(dot(m.N, dir)), 0.05);
  float g = (cone.f0 + cone.spread * h.t) / cosi * sqrt(area_uv / max(area_w, 1e-12));
  if (!all(isfinite(uv)) || !isfinite(g)) return m;
  const uint ind = gMatRec.Load(a + 8u);
  float3 v;
  float lvl;
  if (hm_eval(a, 0u, uv, g, ind, v, lvl)) {
    float3 k = asfloat(gMatRec.Load3(a + 80u));
    m.albedo = saturate(v * k);
    m.lvl = lvl;
    m.known |= kHmAlbedo;
  }
  if (hm_eval(a, 1u, uv, g, ind, v, lvl)) m.rough = saturate(v.x), m.known |= kHmRough;
  if (hm_eval(a, 2u, uv, g, ind, v, lvl)) m.metal = saturate(v.x), m.known |= kHmMetal;
  if (hm_eval(a, 3u, uv, g, ind, v, lvl)) m.refl = saturate(v.x), m.known |= kHmRefl;
  return m;
}

// Coverage (gMatStats): one hit of `m`'s family and which of its fields were known.
void hm_count(HitMaterial m) {
  const uint b = 16u * min(m.family, kFamCount - 1u);
  gMatStats.InterlockedAdd(b, 1u);
  if ((m.known & kHmAlbedo) != 0u) gMatStats.InterlockedAdd(b + 4u, 1u);
  if ((m.known & kHmRough) != 0u) gMatStats.InterlockedAdd(b + 8u, 1u);
  if ((m.known & kHmMetal) != 0u) gMatStats.InterlockedAdd(b + 12u, 1u);
}
void hm_qa_add(uint k, uint v) {
  if (v != 0u) gMatStats.InterlockedAdd(4u * (kHmQa + k), v);
}
