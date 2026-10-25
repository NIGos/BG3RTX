// DLSS Ray Reconstruction guide buffers from BG3's G-buffer.
// Dispatched by the host (layer/ngx.cpp) in the game's command buffer right
// before the RR evaluate, at render resolution. Recompiles on save; the host
// rebuilds the pipeline on the next frame.
//
// Outputs (formats chosen by the host):
//   DiffuseAlbedo   RGBA8    baseColor * (1 - metal)            sky: 0.5
//   SpecularAlbedo  RGBA16F  EnvBRDFApprox2(F0, rough^2, NoV)   sky: 0
//   NormalRough     RGBA16F  xyz view-space normal, w roughness  sky: 0
//                            (DLSS.Roughness.Mode = Packed)
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
  float2 tan_half_fov; // 0,0 -> NoV = |n.z|
  uint depth_inverted; // reversed Z: far plane = 0
  uint show;           // 1 diffuse, 2 specular, 3 normals, 4 roughness, 5 raw RT0, 6 depth
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
  float3 base = GBase.Load(p).rgb;
  float4 m = GMaterial.Load(p);
  float rough = m.x, spec = m.y, metal = m.z;

  float2 ndc = (float2(id.xy) + 0.5) / float2(pc.size) * 2.0 - 1.0;
  float3 v = normalize(float3(-ndc * pc.tan_half_fov, 1.0));
  float NoV = saturate(abs(dot(n, v)));

  float3 f0 = bg3_f0(base, spec, metal);
  ODiffuse[id.xy] = float4(base * (1.0 - metal), 1.0);
  OSpecular[id.xy] = float4(env_brdf_approx2(f0, rough * rough, NoV), 1.0);
  ONormalRough[id.xy] = float4(n, rough);
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
  OShow[id.xy] = c;
}
