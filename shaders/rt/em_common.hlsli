// D-EM: the emissive scene (layer/emissive.cpp, docs/TRACK_D_RT.md 12).
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
static const uint kEmFlagLuminance = 1u << 0;   // x the global scale (emGlobal[0]; luminance-emissive materials)
static const uint kEmFlagApprox = 1u << 1;      // the family's formula is not fully reproduced (e.g. vertex colour)
static const uint kEmFlagPulse = 1u << 2;       // time-animated (sampled at record time)
static const uint kEmFlagBaseMap = 1u << 3;     // x a base-colour texture (tex2, uv2) ("BGM" families)
static const uint kEmFlagVtAlbedo = 1u << 4;    // x the virtual-texture albedo ("BGM_..._VT" families)
struct EmRec {
  float3 radiance; uint mode;      // colour x intensity [x pulse]
  uint tex; uint flags; uint fs_lo; uint fs_hi;
  float4 uv;                       // mask uv = uv.xy * uv + uv.zw
  uint tex2; uint vt_tex; uint vt_cache; uint draw;
  float4 uv2;                      // base map uv
  float4 vt_p0; float4 vt_p1;      // VT uv transform (engine CB s1b0 +0, +16)
  float vt_t[32];                  // VT tile-set constants: two row-major mat4 (SSBO s0b12 entry)
  uint cls; uint pad0, pad1, pad2;
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
// (float), 5 build frame (low 32 bits), 6 global scale bound (1), 8-10 the build camera's
// absolute position (float3); words 16+: per-group power sums.
[[vk::binding(9, EM_SET)]] RWByteAddressBuffer emHeader;
[[vk::binding(10, EM_SET)]] ByteAddressBuffer emGlobal;         // the game's set 2 b0 (float [0]: luminance scale)
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
  if (mode != kEmModeConst && r.tex != 0 && r.tex < kEmMaxTextures) {
    float4 m = emTex[NonUniformResourceIndex(r.tex)].SampleLevel(emSampler, uv * r.uv.xy + r.uv.zw,
                                                                 em_tex_lod(r.tex, g, r.uv.xy));
    L *= mode == kEmModeMaskRGB ? m.rgb : m.rrr;
  }
  if ((r.flags & kEmFlagBaseMap) != 0 && r.tex2 != 0 && r.tex2 < kEmMaxTextures)
    L *= emTex[NonUniformResourceIndex(r.tex2)].SampleLevel(emSampler, uv * r.uv2.xy + r.uv2.zw,
                                                            em_tex_lod(r.tex2, g, r.uv2.xy)).rgb;
  if ((r.flags & kEmFlagVtAlbedo) != 0) L *= em_vt_albedo(r, uv, g);
  if ((r.flags & kEmFlagLuminance) != 0 && emHeader.Load(6 * 4) != 0) {
    float k = asfloat(emGlobal.Load(0));
    L *= k > 0.0 && k < 1e6 ? k : 1.0;
  }
  return all(L >= 0.0) && all(L < 1e6) ? L : float3(0, 0, 0);
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
  EmTri tri = em_triangle(q.CommittedInstanceIndex(), q.CommittedPrimitiveIndex(), q.CommittedObjectToWorld3x4());
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
