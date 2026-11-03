// Layout capability only: the setting itself is a Vulkan specialization constant.
// The ordinary requests do not include any of this code (key-off byte identity).
#pragma once
#ifdef WATER_HIT_SHADE_LAYOUT
#ifdef WATER_HIT_SHADE_REFERENCE
static const uint kWaterHitShade = WATER_HIT_SHADE_REFERENCE;
#else
[[vk::constant_id(1016)]] const uint kWaterHitShade = 0u;
#endif
#include "bg3_lights.hlsli"
#include "bg3_ambient.hlsli"
#define BG3_SUN_SET 3
#include "bg3_sun.hlsli"
#define RT_HIT_INSTANCED 1
#define RT_HIT_WIND_RGB 1
#include "native_hit_material.hlsli"
#define BG3_BOX_SET 2
#define NATIVE_HIT_BOXES 1
#define NATIVE_HIT_DLS_POLICY 1
#define NATIVE_HIT_SLOTS_SET 3
#define NATIVE_HIT_SLOTS_BINDING 7
#include "native_hit_direct.hlsli"
#define NATIVE_HIT_SUN_ROOF 1
#define NATIVE_HIT_SHADE_FULL 1
#include "native_hit_shade.hlsli"
// Reasons: 0 accepted, 1 unpublished/material, 2 light source, 3 sun/roof, 4 invalid.
// A qualified material row alone is insufficient: all lighting must also be supported.
uint water_hit_shade(RtHit h,float3 H,float3 R,RtCone cone,uint2 pixel,out float3 L) {
  L=0;
  if(!hit_material_publication_ready()) return 1u;
  NativeHitShade hs=native_hit_shade(h,H,-R,cone.f0+cone.spread*h.t,uint3(pixel,pc.frame));
  if(hs.status==kHitShadeOk) L=hs.radiance;
  return hs.status;
}
#endif
