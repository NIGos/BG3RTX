// Windowless analytic fixture of the REAL hidden-hit shading functions (tools/native_hit_gpu_fixture.cpp).
// Same include chain and defines as rf_hit_shade (gi/rt_reflections.hlsl RF_HIT_SHADE: boxes, the screen's
// light policy, AO) plus gi_hit_sun's disc sampling (gi/gi_trace_rt.hlsl RT_HIT_GI_SUN), without the
// roof-volume rule (its trust slots are a separate contract) and without the ambient (FULL). The pass Push
// is the real gi_common.hlsli one. Nothing here is a copy of production shading code: every call below is
// the production function; this file only builds the material and stores results.
#define RT_HIT_MATERIAL 1   // as rf_hit_shade / gi_hit_sun (RtHit.bary, native_hit_position)
#include "../shaders/gi/gi_common.hlsli"
#include "../shaders/rt/rt_common.hlsli"
#include "../shaders/rt/bg3_lights.hlsli"
#define BG3_SUN_SET 3
#include "../shaders/rt/bg3_sun.hlsli"
#include "../shaders/rt/native_hit_material.hlsli"
#define BG3_BOX_SET 2
#define NATIVE_HIT_BOXES 1
#define NATIVE_HIT_DLS_POLICY 1
#define NATIVE_HIT_SLOTS_SET 0
#define NATIVE_HIT_SLOTS_BINDING 46
#include "../shaders/rt/native_hit_direct.hlsli"
#define NATIVE_HIT_SUN_DISC 1
#include "../shaders/rt/native_hit_sun.hlsli"
// rt_hit_emission (case 7): the real em_lookup / em_radiance / record policy over an emissive scene of
// its own (the host's set layout, shaders/rt/em_common.hlsli), here as set 4.
#define EM_SET 4
#include "../shaders/rt/em_common.hlsli"
#include "../shaders/rt/native_hit_emission.hlsli"

// Case record, 80 bytes (FxCase in the .cpp).
//   0 p, 12 mode, 16 n, 28 key, 32 v (outgoing), 44 footprint, 48 albedo, 60 roughness,
//   64 metalness, 68 reflectance, 72 channel, 76 unused. Mode 3 (white furnace) forces albedo 1 and
//   reads the incident direction wi from bytes 48..59 instead.
// Result record, 64 bytes: 0 a.rgb (point/spot/box direct, or the furnace f cos), 12 its status,
//   16 b.rgb (sun; mode 5 the sun-disc sample direction), 28 its status, 32 c.rgb (direct .native), 44 lights, 48 AO, 52 AO-on-direct factor,
//   56 unused, 60 material status.
// Mode 6 (emission): p is the ray origin, v its unit direction, c48 the absolute camera (em_shift), footprint
//   the cone width; the main TLAS gives t (rt_closest_hit), then native_hit_emission: a.rgb its radiance,
//   12 its status (kHe*), b.x the main hit t, b.y 1 if aborted, 28 the record index; material status 0.
// Mode 7 (two-sided frame, rt_hit_material_instanced): p the ray origin, v its direction, n the authored N,
//   c48 the triangle's geometric normal; rt_closest_hit, then a.x = nv_two_sided_sign(c48, n, RtHit.dir) (the
//   consumer's N-column sign), a.y the hit t, 12 1 if aborted; b.rgb RtHit.dir; material status 0.
[[vk::binding(60, 0)]] ByteAddressBuffer fxCases;
[[vk::binding(61, 0)]] RWByteAddressBuffer fxOut;
static const uint kFxDirect = 0u, kFxSun = 1u, kFxAo = 2u, kFxFurnace = 3u, kFxCombined = 4u, kFxSunDir = 5u, kFxEmission = 6u,
                  kFxTwoSided = 7u;

NativeHitMaterial fx_material(float3 albedo, float rough, float metal, float refl, float3 n, uint channel) {
  NtInput i;
  i.linear_albedo = albedo; i.roughness = rough; i.reflectance = refl; i.metalness = metal;
  i.material_class = 0u; i.evidence = NtSamplesKnown | NtLinearColorKnown | NtOpaqueCoverageKnown | NtLobesKnown;
  NativeHitMaterial m = (NativeHitMaterial)0;
  m.parameters = nt_class0_parameters(i);
  m.scattering = nt_to_fresnel_blend(m.parameters);
  m.normal = normalize(n);
  m.channel = channel;
  m.status = m.parameters.status == NtOk && m.scattering.status == NtOk ? 0u : 1u;
  return m;
}

[numthreads(64, 1, 1)]
void main(uint id : SV_DispatchThreadID) {
  uint bytes;
  fxCases.GetDimensions(bytes);
  if (id >= bytes / 80u) return;
  const uint a = id * 80u;
  const float3 p = asfloat(fxCases.Load3(a));
  const uint mode = fxCases.Load(a + 12u);
  const float3 n = asfloat(fxCases.Load3(a + 16u));
  const uint key = fxCases.Load(a + 28u);
  const float3 v = asfloat(fxCases.Load3(a + 32u));
  const float footprint = asfloat(fxCases.Load(a + 44u));
  const float3 c48 = asfloat(fxCases.Load3(a + 48u));
  const float rough = asfloat(fxCases.Load(a + 60u)), metal = asfloat(fxCases.Load(a + 64u)), refl = asfloat(fxCases.Load(a + 68u));
  const uint channel = fxCases.Load(a + 72u);
  const uint o = id * 64u;
  NativeHitMaterial m = fx_material(mode == kFxFurnace ? 1.0.xxx : c48, rough, metal, refl, n, channel);
  float3 ra = 0, rb = 0, rc = 0;
  uint sa = 0xffffffffu, sb = 0xffffffffu, lights = 0u;
  float ao = -1.0, factor = -1.0;
  if (mode == kFxDirect || mode == kFxCombined) {
    NativeHitDirect d = native_hit_direct(m, p, v, n, footprint);
    ra = d.radiance; sa = d.status; lights = d.lights; rc = d.native;
  }
  if (mode == kFxSun || mode == kFxCombined) {
    g_hs_seed = pcg3d(uint3(id, key, 0x2545f491u)).x;
    HdDirect s = native_hit_sun(m, p, v, n, footprint);
    rb = s.radiance; sb = s.status;
  }
  if (mode == kFxSunDir) {   // the disc sample itself (b.rgb), same seed rule as kFxSun
    g_hs_seed = pcg3d(uint3(id, key, 0x2545f491u)).x;
    rb = native_hit_sun_dir(normalize(cam.v0.xyz)); sb = HdOk;
  }
  if (mode == kFxAo) {
    ao = native_hit_ao(p, n, footprint, uint3(id, key, 0u));
    factor = native_hit_ao_factor(ao);
  }
  if (mode == kFxFurnace) {
    HdIncident inc;
    inc.radiance = 1.0.xxx; inc.direction = c48; inc.distance = 0; inc.status = HdOk;
    HdDirect t = hd_scatter(m.scattering, m.normal, n, v, inc, true, 1.0);
    ra = t.radiance; sa = t.status;
  }
  if (mode == kFxEmission) {
    RtCone cone = {footprint, 0.0};
    const RtHit h = rt_closest_hit(p, v, 100.0, cone);
    const HitEmission e = native_hit_emission(p, v, h.t, p + v * h.t, footprint, c48, HIT_EM_TOL);
    ra = e.radiance; sa = e.status; rb = float3(h.t, h.aborted ? 1.0 : 0.0, 0.0); sb = e.rec;
    m.status = 0u;   // no material in this mode
  }
  if (mode == kFxTwoSided) {
    RtCone cone = {footprint, 0.0};
    const RtHit h = rt_closest_hit(p, v, 100.0, cone);
    ra = float3(nv_two_sided_sign(c48, n, h.dir), h.t, 0.0); sa = h.aborted ? 1u : 0u;
    rb = h.dir; sb = 0u; m.status = 0u;
  }
  fxOut.Store3(o, asuint(ra)); fxOut.Store(o + 12u, sa);
  fxOut.Store3(o + 16u, asuint(rb)); fxOut.Store(o + 28u, sb);
  fxOut.Store3(o + 32u, asuint(rc)); fxOut.Store(o + 44u, lights);
  fxOut.Store(o + 48u, asuint(ao)); fxOut.Store(o + 52u, asuint(factor)); fxOut.Store(o + 56u, 0u);
  fxOut.Store(o + 60u, m.status);
}
