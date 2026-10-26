// Shared bindings and helpers for the screen-space GI passes. Everything the
// game provides is bound straight from its own descriptors at the tiled
// lighting dispatch (see docs/FRAME_MAP.md and docs/TRACK_C_GI.md).
#pragma once

// set 0 binding 1 of the lighting shader. Positions in this space are
// camera-relative: P = ray(uv) * linear_depth.
struct CameraCB {
  float4 v0, v1, v2;                   // v0.xyz = direction towards the sun (_m0), v1.xyz = sun colour (_m2)
  float4 m6_r0, m6_r1, m6_r2, m6_r3;   // _m6, row-major: view normal -> lighting space
  float4 ray0, ray1, ray2;             // _m7: frustum rays at the viewport's (0,0), (1,0), (0,1) in ab space
};
// set 0 binding 4: viewport mapping, ab = ((uv - bias) / scale) * 0.5
struct ViewportCB {
  float2 scale;
  float2 bias;
};

[[vk::binding(0, 0)]] ConstantBuffer<CameraCB> cam;
[[vk::binding(1, 0)]] ConstantBuffer<ViewportCB> vp;
[[vk::binding(2, 0)]] Texture2D<float4> gNormal;    // RG16F, stereographic, view space
[[vk::binding(3, 0)]] Texture2D<float4> gAlbedo;    // RGBA8 base colour
[[vk::binding(4, 0)]] Texture2D<float4> gMaterial;  // RGBA8: roughness, specular, metalness, flags
[[vk::binding(5, 0)]] Texture2D<float> gDepth;      // R32F linear depth
[[vk::binding(6, 0)]] [[vk::image_format("r11g11b10f")]] RWTexture2D<float3> gDiffuse;
[[vk::binding(7, 0)]] [[vk::image_format("r11g11b10f")]] RWTexture2D<float3> gSpecular;
// GI state, half resolution: rgb = one-bounce radiance, a = visibility
// (fraction of rays that found no occluder within the search radius).
[[vk::binding(8, 0)]] [[vk::image_format("rgba16f")]] RWTexture2D<float4> giRaw;
[[vk::binding(9, 0)]] [[vk::image_format("rgba16f")]] RWTexture2D<float4> giHistIn;
[[vk::binding(10, 0)]] [[vk::image_format("rgba16f")]] RWTexture2D<float4> giHistOut;
// Last frame's lit diffuse + specular at render resolution: radiance at ray hits.
[[vk::binding(11, 0)]] [[vk::image_format("rgba16f")]] RWTexture2D<float4> prevRadiance;
// The game's AO target (RGB10A2, AO in .x), read by its ambient term.
[[vk::binding(12, 0)]] [[vk::image_format("rgb10a2")]] RWTexture2D<float4> gAO;
[[vk::binding(13, 0)]] [[vk::image_format("r32f")]] RWTexture2D<float> giRawDepth;
[[vk::binding(14, 0)]] [[vk::image_format("r32f")]] RWTexture2D<float> giHistDepthIn;
[[vk::binding(15, 0)]] [[vk::image_format("r32f")]] RWTexture2D<float> giHistDepthOut;
// The game's screen-space sun shadow mask (R8), read by its sun term.
[[vk::binding(16, 0)]] [[vk::image_format("r8")]] RWTexture2D<float> gSunShadow;

struct Push {
  uint2 full;       // render resolution
  uint2 half;       // GI resolution
  uint frame;
  float strength;
  float radius;     // march length as a fraction of the pixel's depth
  float thickness;  // depth-relative hit thickness
  uint rays;
  uint steps;
  uint debug;       // 0 off, 1 GI irradiance only, 2 added term x4
  float history;    // temporal blend weight of the new sample
  float ao;         // how strongly visibility multiplies the game's AO (0..1)
  float cs_length;  // contact-shadow march length, fraction of camera distance
  float cs_strength;
  uint cs_steps;
};
[[vk::push_constant]] Push pc;

float3 decode_normal(float2 rg) {
  float2 e = rg * 3.5554 - 1.7777;
  float t = dot(e, e) + 1.0;
  if (t > 5.4e10) return float3(0, 0, 1);
  float s = 2.0 / t;
  return float3(e * s, 1.0 - s);
}

// Same transform as the game: n_out = n * mat3(columns of _m6).
float3 to_lighting_space(float3 n) {
  return normalize(n.x * cam.m6_r0.xyz + n.y * cam.m6_r1.xyz + n.z * cam.m6_r2.xyz);
}

float2 uv_to_ab(float2 uv) { return ((uv - vp.bias) / vp.scale) * 0.5; }
float2 ab_to_uv(float2 ab) { return (ab * 2.0) * vp.scale + vp.bias; }

float3 ray_at(float2 uv) {
  float2 ab = uv_to_ab(uv);
  return cam.ray0.xyz * (1.0 - ab.x - ab.y) + cam.ray1.xyz * ab.x + cam.ray2.xyz * ab.y;
}

// Inverse of P = d * (r0 + (r1 - r0) a + (r2 - r0) b).
float3x3 frustum_inverse() {
  float3 c0 = cam.ray0.xyz, c1 = cam.ray1.xyz - cam.ray0.xyz, c2 = cam.ray2.xyz - cam.ray0.xyz;
  float3 r0 = cross(c1, c2), r1 = cross(c2, c0), r2 = cross(c0, c1);
  float det = dot(c0, r0);
  return float3x3(r0, r1, r2) / det;   // rows: applying it to P gives (d, d*a, d*b)
}

// Returns false behind the camera. uv in [0,1] when on screen.
bool project(float3x3 inv, float3 P, out float2 uv, out float d) {
  float3 x = mul(inv, P);
  d = x.x;
  uv = ab_to_uv(x.yz / max(d, 1e-12));
  return d > 0.0;
}

uint hash(uint x) {
  x ^= x >> 16; x *= 0x7feb352d; x ^= x >> 15; x *= 0x846ca68b; x ^= x >> 16;
  return x;
}
// Depth-aware 3x3 gather of the accumulated half-resolution history.
float4 gather_history(uint2 fp, float d) {
  int2 hp = int2(fp / 2);
  float4 acc = 0;
  float wsum = 0;
  for (int y = -1; y <= 1; ++y)
    for (int x = -1; x <= 1; ++x) {
      int2 q = clamp(hp + int2(x, y), int2(0, 0), int2(pc.half) - 1);
      float hd = giHistDepthOut[q];
      float w = hd > 0.0 ? exp(-abs(hd - d) / (d * 0.01)) * (x == 0 && y == 0 ? 2.0 : 1.0) : 0.0;
      acc += giHistOut[q] * w;
      wsum += w;
    }
  return wsum > 1e-4 ? acc / wsum : float4(0, 0, 0, 1);
}

float rand01(inout uint s) { s = hash(s); return float(s & 0x00ffffff) / 16777216.0; }

float3 cosine_hemisphere(float3 n, float u1, float u2) {
  float r = sqrt(u1), phi = 6.2831853 * u2;
  float3 t = abs(n.z) < 0.999 ? normalize(cross(float3(0, 0, 1), n)) : float3(1, 0, 0);
  float3 b = cross(n, t);
  return normalize(t * (r * cos(phi)) + b * (r * sin(phi)) + n * sqrt(max(0.0, 1.0 - u1)));
}
