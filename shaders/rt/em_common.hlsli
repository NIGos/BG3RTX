// D-EM: the emissive scene (layer/emissive.cpp, docs/TRACK_D_RT.md 13).
//
// BG3 redraws its emissive meshes, depth-tested, into the effects target before the
// tiled lighting. The layer captures those draws (transform feedback) with one record per
// draw. When the game submits the command buffers of a frame's effects passes, the layer
// builds, in a command buffer of its own submitted right after, a separate emissive TLAS
// (one BLAS per draw) and a bounded light list over its triangles. Consumers therefore
// see the previous submitted frame's emission (one frame of latency).
//
// Space: camera-relative world of the frame the scene was built from. Its camera's
// absolute position is in the header; em_shift(camera now) moves a point of the current
// frame's lighting space into the scene's (and minus it back). With a static camera the
// shift is 0.
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
static const uint kEmMaxCaches = 16;

// One per emissive draw (256 bytes). Radiance at uv:
//   radiance x [mask(uv).r | mask(uv).rgb] x [base map(uv2).rgb] x [VT albedo(uv).rgb] x [global]
static const uint kEmModeConst = 0;   // no mask
static const uint kEmModeMaskR = 1;   // x mask.r (the game's glow maps)
static const uint kEmModeMaskRGB = 2; // x mask.rgb (coloured glow maps)
static const uint kEmModeBlackbody = 3; // x the blackbody LUT (tex) at u = uv.x + uv.y * uv.z (lava: uv.z the heat)
static const uint kEmFlagLuminance = 1u << 0;   // x the global scale (emGlobal[0]; luminance-emissive materials)
static const uint kEmFlagApprox = 1u << 1;      // the family's formula is not fully reproduced (e.g. vertex colour)
static const uint kEmFlagPulse = 1u << 2;       // time-animated (sampled at record time)
static const uint kEmFlagBaseMap = 1u << 3;     // x a base-colour texture (tex2, uv2) ("BGM" families)
static const uint kEmFlagVtAlbedo = 1u << 4;    // x the virtual-texture albedo ("BGM_..._VT" families)
static const uint kEmFlagForward = 1u << 5;     // drawn in the forward pass (not in the effects target)
static const uint kEmFlagCalibrated = 1u << 6;  // radiance scaled by the on-screen calibration (approximate families)
struct EmRec {
  float3 radiance; uint mode;      // colour x intensity [x pulse]
  uint tex; uint flags; uint fs_lo; uint fs_hi;
  float4 uv;                       // mask uv = uv.xy * uv + uv.zw
  uint tex2; uint vt_tex; uint vt_cache; uint draw;
  float4 uv2;                      // base map uv
  float4 vt_p0; float4 vt_p1;      // VT uv transform (engine CB s1b0 +0, +16)
  float vt_t[32];                  // VT tile-set constants: two row-major mat4 (SSBO s0b12 entry)
  uint cls; uint glob; uint pad1, pad2;   // glob: slot of the luminance scale in emGlobal (0xFFFFFFFF: none)
};

// One per emissive triangle (80 bytes), in the scene's camera-relative world.
struct EmLight {
  float3 p0; uint rec;
  float3 e1; float area;     // p1 - p0
  float3 e2; float power;    // p2 - p0; power = pi * area * luminance(radiance), one-sided Lambertian
  float3 radiance; uint inst; // mean radiance over the triangle (maps averaged at the triangle's mip)
  uint prim; uint flags; float2 pad;
};

[[vk::binding(0, EM_SET)]] RaytracingAccelerationStructure emTlas;
[[vk::binding(1, EM_SET)]] ByteAddressBuffer emInfo;    // 16 B per TLAS instance: base, stride | flags, record, triangles
[[vk::binding(2, EM_SET)]] ByteAddressBuffer emPool;    // (unused: no cached meshes in the emissive scene)
[[vk::binding(3, EM_SET)]] ByteAddressBuffer emSoup;    // per-draw geometry: camera-relative world, uv at +16
[[vk::binding(4, EM_SET)]] StructuredBuffer<EmRec> emRecs;
[[vk::binding(5, EM_SET)]] RWStructuredBuffer<EmLight> emLights;
[[vk::binding(6, EM_SET)]] Texture2D<float4> emTex[kEmMaxTextures];   // masks, base maps, VT indirection
[[vk::binding(7, EM_SET)]] SamplerState emSampler;      // linear, wrap
[[vk::binding(8, EM_SET)]] RWByteAddressBuffer emStats; // debug views' statistics (host-read)
// Header, 16 words: 0 lights, 1 triangles left out, 2 instances, 3 records, 4 pre-exposure
// (float), 5 build frame (low 32 bits), 6 global scale bound (1), 7 triangles per light (k),
// 8-10 the build camera's absolute position (float3), 12 soup triangles collapsed by the
// sanitize pass;
// words 16+: per-group power sums.
[[vk::binding(9, EM_SET)]] RWByteAddressBuffer emHeader;
[[vk::binding(10, EM_SET)]] ByteAddressBuffer emGlobal;         // our copies of the game's luminance scale (set 2 b0 [0]), per slot
[[vk::binding(11, EM_SET)]] Texture2DArray<float4> emCache[kEmMaxCaches];   // VT tile caches

float em_lum(float3 c) { return dot(c, float3(0.2126, 0.7152, 0.0722)); }

// The scene's camera-relative position of a point given in the current frame's lighting
// space: p + em_shift(cam_now); `cam_now` is the absolute camera position (set 0 b2 _m6).
float3 em_shift(float3 cam_now) {
  float3 c = asfloat(emHeader.Load3(8 * 4));
  float3 s = cam_now - c;
  return all(abs(s) < 1e6) ? s : float3(0, 0, 0);
}

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

// Mip of a texture of `tex` for a footprint of `g` uv units.
float em_tex_lod(uint tex, float g, float2 scale) {
  uint w, h, levels;
  emTex[NonUniformResourceIndex(tex)].GetDimensions(0, w, h, levels);
  float s = g * max(abs(scale.x), abs(scale.y)) * float(max(w, h));
  return clamp(log2(max(s, 1e-8)), 0.0, float(max(levels, 1u)) - 1.0);
}

float vt_c(EmRec r, uint m, uint c, uint row) { return r.vt_t[m * 16 + row * 4 + c]; }

// BG3's Granite albedo lookup (as its BGM fragment shaders do, e.g. 7085eee7; the same
// arithmetic as rt_common.hlsli's vt_alpha, on this record's constants): page from the
// indirection texture at the footprint's mip, texel in the tile cache, layer 0 (albedo).
// White when the tile set is not resident (the game shows red there).
float3 em_vt_albedo(EmRec r, float2 uv, float g) {
  if (r.vt_tex == 0 || r.vt_tex >= kEmMaxTextures || r.vt_cache == 0 || r.vt_cache >= kEmMaxCaches) return 1.0;
  if (vt_c(r, 0, 2, 3) != 2202.0) return 1.0;   // the game's "tile set resident" sentinel
  float2 u = frac(uv) * r.vt_p0.zw + r.vt_p0.xy;
  float2 s = float2(vt_c(r, 0, 2, 0), vt_c(r, 0, 3, 0));
  float2 dx = r.vt_p0.zw * float2(g, 0.0) * s, dy = r.vt_p0.zw * float2(0.0, g) * s;
  float d1 = dot(dx, dx), d2 = dot(dy, dy);
  float l = log2(max(max(d1, d2), 1e-30));
  float aniso = min(0.5 * l - 0.5 * log2(max(min(d1, d2), 1e-30)), vt_c(r, 0, 1, 0));
  float mip = floor(min(max(0.5 * l - aniso - 0.5, 0.0), r.vt_p1.x) + 0.5);
  float2 dims = float2(vt_c(r, 0, 0, 3), vt_c(r, 0, 0, 3) * vt_c(r, 0, 1, 3));
  uint iw, ih, ilevels;
  emTex[NonUniformResourceIndex(r.vt_tex)].GetDimensions(0, iw, ih, ilevels);
  mip = min(mip, float(max(ilevels, 1u) - 1u));
  float2 ip = floor(u * dims * pow(0.5, mip));
  int2 ipi = clamp(int2(ip), int2(0, 0), int2(max(iw >> uint(mip), 1u) - 1u, max(ih >> uint(mip), 1u) - 1u));
  uint e = asuint(emTex[NonUniformResourceIndex(r.vt_tex)].Load(int3(ipi, int(mip))).x);
  float sc = pow(2.0, float(e & 15u));
  float2 s163 = float2(sc, sc * vt_c(r, 0, 1, 3));
  float2 s168 = float2(vt_c(r, 0, 0, 2), vt_c(r, 0, 1, 2));
  float2 s175 = float2(vt_c(r, 1, 0, 0), vt_c(r, 1, 1, 0));
  float2 c = (frac(u * s163) * s168 + float2(float((e >> 14) & 1023u), float((e >> 4) & 1023u))) * s175 +
             float2(vt_c(r, 1, 2, 0), vt_c(r, 1, 3, 0));
  float3 a = emCache[NonUniformResourceIndex(r.vt_cache)].SampleLevel(emSampler, float3(c, float((e >> 24) & 127u)), 0).rgb;
  return all(a >= 0.0) && all(a < 1e4) ? a : 1.0;
}

// Radiance of record `r` at uv `uv`, for a footprint of `g` uv units (a pixel, or the
// whole triangle for its mean).
float3 em_radiance(EmRec r, float2 uv, float g) {
  float3 L = r.radiance;
  uint mode = r.mode & 0xffu;
  if (mode == kEmModeBlackbody && r.tex != 0 && r.tex < kEmMaxTextures) {
    L *= emTex[NonUniformResourceIndex(r.tex)].SampleLevel(emSampler, float2(saturate(r.uv.x + r.uv.y * r.uv.z), 0.5), 0).rgb;
  } else if (mode != kEmModeConst && r.tex != 0 && r.tex < kEmMaxTextures) {
    float4 m = emTex[NonUniformResourceIndex(r.tex)].SampleLevel(emSampler, uv * r.uv.xy + r.uv.zw,
                                                                 em_tex_lod(r.tex, g, r.uv.xy));
    L *= mode == kEmModeMaskRGB ? m.rgb : m.rrr;
  }
  if ((r.flags & kEmFlagBaseMap) != 0 && r.tex2 != 0 && r.tex2 < kEmMaxTextures)
    L *= emTex[NonUniformResourceIndex(r.tex2)].SampleLevel(emSampler, uv * r.uv2.xy + r.uv2.zw,
                                                            em_tex_lod(r.tex2, g, r.uv2.xy)).rgb;
  if ((r.flags & kEmFlagVtAlbedo) != 0) L *= em_vt_albedo(r, uv, g);
  if ((r.flags & kEmFlagLuminance) != 0 && r.glob != 0xFFFFFFFFu) {
    // Copied right after the draw's pass (em_copy.hlsl): the game rewrites its buffer later
    // in the same batch, so a later read is another value.
    float k = asfloat(emGlobal.Load(r.glob * 4));
    L *= k >= 0.0 && k < 1e6 ? k : 1.0;
  }
  return all(L >= 0.0) && all(L < 1e6) ? L : float3(0, 0, 0);
}

// ---- Texture filtering as the game's fragment shader does it (rt_em_filter, TRACK_D 13.16) ----
// The game samples its glow maps with implicit derivatives: the UV's change per screen pixel
// (dx, dy). filt 1: the hardware's isotropic LOD from them, log2(max |dx|, |dy| in texels).
// filt 2: anisotropic filtering as the Vulkan spec's reference (and the hardware's) does it:
// up to `aniso` trilinear probes along the footprint's major axis, at the LOD of
// major / probes. filt 0 (default): the ray-cone LOD of em_tex_lod (isotropic, from the
// pixel's world footprint), what em_radiance does.
float4 em_sample_grad(uint tex, float2 uv, float2 dx, float2 dy, uint filt, float aniso) {
  uint w, h, levels;
  emTex[NonUniformResourceIndex(tex)].GetDimensions(0, w, h, levels);
  const float2 sz = float2(w, h);
  const float lx = length(dx * sz), ly = length(dy * sz);
  const float maxl = float(max(levels, 1u)) - 1.0;
  if (filt == 1u) {
    const float lod = clamp(log2(max(max(lx, ly), 1e-8)), 0.0, maxl);
    return emTex[NonUniformResourceIndex(tex)].SampleLevel(emSampler, uv, lod);
  }
  const float major = max(lx, ly), minor = max(min(lx, ly), 1e-8);
  const float n = clamp(ceil(major / minor), 1.0, max(aniso, 1.0));
  const float lod = clamp(log2(max(major / n, 1e-8)), 0.0, maxl);
  const float2 axis = lx >= ly ? dx : dy;
  float4 sum = float4(0, 0, 0, 0);
  const uint cnt = uint(n);
  for (uint i = 0; i < cnt; ++i)
    sum += emTex[NonUniformResourceIndex(tex)].SampleLevel(emSampler, uv + axis * ((float(i) + 0.5) / n - 0.5), lod);
  return sum / n;
}

// em_radiance with the maps sampled from screen-space UV derivatives (filt 1/2); the VT
// albedo keeps its own LOD.
float3 em_radiance_g(EmRec r, float2 uv, float g, float2 dx, float2 dy, uint filt, float aniso) {
  if (filt == 0u) return em_radiance(r, uv, g);
  float3 L = r.radiance;
  uint mode = r.mode & 0xffu;
  if (mode == kEmModeBlackbody && r.tex != 0 && r.tex < kEmMaxTextures) {
    L *= emTex[NonUniformResourceIndex(r.tex)].SampleLevel(emSampler, float2(saturate(r.uv.x + r.uv.y * r.uv.z), 0.5), 0).rgb;
  } else if (mode != kEmModeConst && r.tex != 0 && r.tex < kEmMaxTextures) {
    float4 m = em_sample_grad(r.tex, uv * r.uv.xy + r.uv.zw, dx * r.uv.xy, dy * r.uv.xy, filt, aniso);
    L *= mode == kEmModeMaskRGB ? m.rgb : m.rrr;
  }
  if ((r.flags & kEmFlagBaseMap) != 0 && r.tex2 != 0 && r.tex2 < kEmMaxTextures)
    L *= em_sample_grad(r.tex2, uv * r.uv2.xy + r.uv2.zw, dx * r.uv2.xy, dy * r.uv2.xy, filt, aniso).rgb;
  if ((r.flags & kEmFlagVtAlbedo) != 0) L *= em_vt_albedo(r, uv, g);
  if ((r.flags & kEmFlagLuminance) != 0 && r.glob != 0xFFFFFFFFu) {
    float k = asfloat(emGlobal.Load(r.glob * 4));
    L *= k >= 0.0 && k < 1e6 ? k : 1.0;
  }
  return all(L >= 0.0) && all(L < 1e6) ? L : float3(0, 0, 0);
}

// The UV where a ray (o, dd) meets the plane of triangle `t` (ray differentials).
float2 em_uv_on_plane(EmTri t, float3 o, float3 dd) {
  const float3 e1 = t.p[1] - t.p[0], e2 = t.p[2] - t.p[0];
  const float3 n = cross(e1, e2);
  const float dn = dot(dd, n);
  const float s = abs(dn) > 1e-20 ? dot(t.p[0] - o, n) / dn : 0.0;
  const float3 v = o + dd * s - t.p[0];
  const float d00 = dot(e1, e1), d01 = dot(e1, e2), d11 = dot(e2, e2), d20 = dot(v, e1), d21 = dot(v, e2);
  const float den = d00 * d11 - d01 * d01;
  const float b1 = abs(den) > 1e-30 ? (d11 * d20 - d01 * d21) / den : 0.0;
  const float b2 = abs(den) > 1e-30 ? (d00 * d21 - d01 * d20) / den : 0.0;
  return t.uv[0] + b1 * (t.uv[1] - t.uv[0]) + b2 * (t.uv[2] - t.uv[0]);
}

// Emission at a hit of the main TLAS at distance t along (o, d), both in the current
// frame's lighting space: the emissive scene's closest triangle within [t - eps, t + eps].
// `shift` = em_shift(camera now). `px` is the world size of one pixel (or ray-cone width)
// at the hit, for the maps' mips. Returns false when the hit is not emissive.
bool em_lookup(float3 o, float3 d, float t, float eps, float px, float3 shift, out float3 L, out uint rec) {
  L = float3(0, 0, 0);
  rec = 0xFFFFFFFFu;
  RayDesc ray;
  ray.Origin = o + shift;
  ray.Direction = d;
  ray.TMin = max(t - eps, 0.0);
  ray.TMax = t + eps;
  RayQuery<RAY_FLAG_FORCE_OPAQUE> q;
  q.TraceRayInline(emTlas, RAY_FLAG_NONE, 0xFF, ray);
  q.Proceed();
  if (q.CommittedStatus() != COMMITTED_TRIANGLE_HIT) return false;
  // One BLAS, one geometry per draw (layer/emissive.cpp em_as_build): the geometry index
  // is the draw's entry in emInfo.
  EmTri tri = em_triangle(q.CommittedGeometryIndex(), q.CommittedPrimitiveIndex(), q.CommittedObjectToWorld3x4());
  if (!tri.ok) return false;
  float2 b = q.CommittedTriangleBarycentrics();
  float2 uv = tri.uv[0] * (1.0 - b.x - b.y) + tri.uv[1] * b.x + tri.uv[2] * b.y;
  EmRec r = emRecs[tri.rec];
  float wa = 0.5 * length(cross(tri.p[1] - tri.p[0], tri.p[2] - tri.p[0]));
  float2 du = tri.uv[1] - tri.uv[0], dv = tri.uv[2] - tri.uv[0];
  float ua = 0.5 * abs(du.x * dv.y - du.y * dv.x);
  L = em_radiance(r, uv, sqrt(ua / max(wa, 1e-12)) * px);
  rec = tri.rec;
  return true;
}

// em_lookup with the maps filtered from the screen-space UV derivatives (filt 1/2): dx_dir and
// dy_dir are the directions of the rays through the next pixel in x and in y.
bool em_lookup_g(float3 o, float3 d, float t, float eps, float px, float3 shift, float3 dx_dir, float3 dy_dir,
                 uint filt, float aniso, out float3 L, out uint rec) {
  if (filt == 0u) return em_lookup(o, d, t, eps, px, shift, L, rec);
  L = float3(0, 0, 0);
  rec = 0xFFFFFFFFu;
  RayDesc ray;
  ray.Origin = o + shift;
  ray.Direction = d;
  ray.TMin = max(t - eps, 0.0);
  ray.TMax = t + eps;
  RayQuery<RAY_FLAG_FORCE_OPAQUE> q;
  q.TraceRayInline(emTlas, RAY_FLAG_NONE, 0xFF, ray);
  q.Proceed();
  if (q.CommittedStatus() != COMMITTED_TRIANGLE_HIT) return false;
  EmTri tri = em_triangle(q.CommittedGeometryIndex(), q.CommittedPrimitiveIndex(), q.CommittedObjectToWorld3x4());
  if (!tri.ok) return false;
  float2 b = q.CommittedTriangleBarycentrics();
  float2 uv = tri.uv[0] * (1.0 - b.x - b.y) + tri.uv[1] * b.x + tri.uv[2] * b.y;
  const float2 dx = em_uv_on_plane(tri, ray.Origin, dx_dir) - uv;
  const float2 dy = em_uv_on_plane(tri, ray.Origin, dy_dir) - uv;
  EmRec r = emRecs[tri.rec];
  float wa = 0.5 * length(cross(tri.p[1] - tri.p[0], tri.p[2] - tri.p[0]));
  float2 du = tri.uv[1] - tri.uv[0], dv = tri.uv[2] - tri.uv[0];
  float ua = 0.5 * abs(du.x * dv.y - du.y * dv.x);
  L = em_radiance_g(r, uv, sqrt(ua / max(wa, 1e-12)) * px, dx, dy, filt, aniso);
  rec = tri.rec;
  return true;
}
