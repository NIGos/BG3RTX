// DLSS Ray Reconstruction guide buffers from BG3's G-buffer.
// Dispatched by the host (layer/ngx.cpp) in the game's command buffer at the
// end of the G-buffer pass, at render resolution. Recompiles on save; the host
// rebuilds the pipeline on the next frame.
//
// Outputs (formats chosen by the host):
//   DiffuseAlbedo   RGBA8    baseColor * (1 - metal)            sky: 0.5
//   SpecularAlbedo  RGBA16F  EnvBRDFApprox2(F0, rough^2, NoV)   sky: 0
//   NormalRough     RGBA16F  xyz world normal, w roughness       sky: 0
//                            (DLSS.Roughness.Mode = Packed)
//
// RT0 holds *view-space* normals (docs/TRACK_D_RT.md section 1). With
// rr_guides_v2=1 (pc.v2) the host passes the camera of the G-buffer pass
// (set 0 binding 2, read on the CPU when the pass ends):
//   r0..r2  rows of inverse(mat3(_m0)): n_world = n_view * inverse(R), the
//           inverse of the game's n_view = n_world * R (row-vector convention);
//   proj    (P00, P11, P20, P21) of the jittered projection _m1: the view
//           direction of pixel p is ((ndc.x - P20) / P00, (ndc.y - P21) / P11, 1)
//           with D3D-style ndc (the vertex shaders negate clip y for Vulkan).
// Without v2 the normals are written as decoded (view space) and N.V is the
// constant rr_nov (the old guides, kept for A/B).
#include "common.hlsli"

[[vk::binding(0, 0)]] Texture2D<float4> GNormal;   // RT0 RG16F
[[vk::binding(1, 0)]] Texture2D<float4> GBase;     // RT1 RGBA8
[[vk::binding(2, 0)]] Texture2D<float4> GMaterial; // RT2 RGBA8
[[vk::binding(3, 0)]] Texture2D<float> GDepth;     // the depth DLSS gets (D24S8, depth aspect)
[[vk::binding(4, 0)]] [[vk::image_format("rgba8")]] RWTexture2D<float4> ODiffuse;
[[vk::binding(5, 0)]] [[vk::image_format("rgba16f")]] RWTexture2D<float4> OSpecular;
[[vk::binding(6, 0)]] [[vk::image_format("rgba16f")]] RWTexture2D<float4> ONormalRough;
// Debug view only (entry "show"): the DLSS output image.
[[vk::binding(7, 0)]] [[vk::image_format("r11g11b10f")]] RWTexture2D<float3> OShow;

struct Push {
  uint2 size;          // render resolution
  uint2 out_size;      // DLSS output resolution (show only)
  float nov;           // v1: > 0 constant N.V; <= 0 |n.z|
  uint v2;             // 1: world normals + per-pixel N.V from the camera below
  uint depth_inverted; // reversed Z: far plane = 0
  uint show;           // 1 diffuse, 2 specular, 3 normals, 4 roughness, 5 raw RT0, 6 depth, 7 N.V
  float4 r0, r1, r2;   // inverse(mat3(view)) rows (xyz)
  float4 proj;         // P00, P11, P20, P21
};
[[vk::push_constant]] Push pc;

bool is_sky(float d) { return pc.depth_inverted != 0 ? d <= 0.0 : d >= 1.0; }

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID) {
  if (any(id.xy >= pc.size)) return;
  int3 p = int3(id.xy, 0);
  float depth = GDepth.Load(p);
  if (is_sky(depth)) {
    ODiffuse[id.xy] = float4(0.5, 0.5, 0.5, 1.0);
    OSpecular[id.xy] = 0.0;
    ONormalRough[id.xy] = 0.0;
    return;
  }
  float3 n = bg3_decode_normal(GNormal.Load(p).xy);
  if (any(!isfinite(n))) n = float3(0.0, 0.0, -1.0);  // RR must never see NaN/Inf
  float3 base = GBase.Load(p).rgb;
  float4 m = GMaterial.Load(p);
  float rough = m.x, spec = m.y, metal = m.z;

  float NoV;
  float3 n_out = n;
  if (pc.v2 != 0) {
    // View direction of this pixel centre (camera at the origin, +z forward).
    float2 uv = (float2(id.xy) + 0.5) / float2(pc.size);
    float2 ndc = float2(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0);
    float3 d = float3((ndc - pc.proj.zw) / pc.proj.xy, 1.0);
    float3 v = -normalize(d);
    NoV = dot(n, v);
    float3 w = n.x * pc.r0.xyz + n.y * pc.r1.xyz + n.z * pc.r2.xyz;
    float l2 = dot(w, w);
    n_out = (l2 > 1e-12 && !isinf(l2)) ? w * rsqrt(l2) : float3(0.0, 1.0, 0.0);
    if (!isfinite(NoV)) NoV = 0.5;
  } else {
    NoV = pc.nov > 0.0 ? pc.nov : saturate(abs(n.z));
  }

  float3 f0 = bg3_f0(base, spec, metal);
  ODiffuse[id.xy] = float4(base * (1.0 - metal), 1.0);
  // env_brdf_approx2 uses |N.V|; the alpha channel carries the signed N.V for
  // rr_show=7 only (RR reads rgb).
  OSpecular[id.xy] = float4(env_brdf_approx2(f0, rough * rough, max(abs(NoV), 1e-3)),
                            pc.show == 7 ? NoV : 1.0);
  ONormalRough[id.xy] = float4(n_out, rough);
}

// rr_show=N: replaces the upscaled frame with one guide (nearest), so the
// tonemapped dump shows exactly what RR is given.
[numthreads(8, 8, 1)]
void show(uint3 id : SV_DispatchThreadID) {
  if (any(id.xy >= pc.out_size)) return;
  uint2 s = min(uint2((float2(id.xy) + 0.5) * float2(pc.size) / float2(pc.out_size)), pc.size - 1);
  float3 c = 0.0;
  if (pc.show == 1) c = ODiffuse[s].rgb;
  else if (pc.show == 2) c = OSpecular[s].rgb;
  else if (pc.show == 3) c = ONormalRough[s].xyz * 0.5 + 0.5;
  else if (pc.show == 4) c = ONormalRough[s].www;
  else if (pc.show == 5) c = float3(GNormal.Load(int3(s, 0)).xy, 0.0);
  else if (pc.show == 6) c = (GDepth.Load(int3(s, 0)) * 20.0).xxx;
  else if (pc.show == 7) {  // N.V: grey >= 0, red where the normal faces away
    float k = OSpecular[s].a;
    c = k >= 0.0 ? k.xxx : float3(-k, 0.0, 0.0);
  }
  OShow[id.xy] = c;
}
