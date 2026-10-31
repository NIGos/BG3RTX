// rt_sun_replace: the trust header and roof volumes (core/sun_trust.h), staged by the core
// into a device-local R32_UINT image, one texel per 4 bytes of the host slot.
// Header (bytes): 0 volume count, 4 flags (1 replace, 2 roof volumes known, 4 off-screen
// test, 8 counters), 8 rise (m), 12 scene top (absolute y), 16 frustum margin (uv),
// 20 ramp start, 24 roof mode, 28 rt_sun_near (m; the A/B debug split only, never trust).
// Written here: 32 scene top (ordered uint), 36 camera (absolute), 48 the farthest pixel
// that needs the roof test (m, float bits; sizes the roof query), 64.. counters.
// Volumes from byte 192 (counters 64-191), 48 bytes each
// (Bg3DbVolume: centre, kind, rotation quaternion, half extents, trigger; absolute world).
#pragma once

// The hidden-hit sun (rt/native_hit_sun.hlsli NATIVE_HIT_SUN_ROOF) reads the same image from the
// D-LS sun set's b8 (D-RF's push set is full): SUN_TRUST_BINDING / SUN_TRUST_SET.
#ifndef SUN_TRUST_BINDING
#define SUN_TRUST_BINDING 47
#define SUN_TRUST_SET 0
#endif
[[vk::binding(SUN_TRUST_BINDING, SUN_TRUST_SET)]] [[vk::image_format("r32ui")]] RWTexture2D<uint> sunTrust;

uint st_u(uint byte_off) { return sunTrust[uint2(byte_off >> 2, 0)]; }
float st_f(uint byte_off) { return asfloat(st_u(byte_off)); }
float4 st_f4(uint byte_off) {
  return float4(st_f(byte_off), st_f(byte_off + 4u), st_f(byte_off + 8u), st_f(byte_off + 12u));
}
void st_store(uint byte_off, uint v) { sunTrust[uint2(byte_off >> 2, 0)] = v; }

// Adds the number of lanes with `cond` to counter `index` (bytes 64 + 4 index), wave-reduced.
void trust_add(uint index, bool cond) {
  uint nb = WaveActiveCountBits(cond);
  if (WaveIsFirstLane() && nb) InterlockedAdd(sunTrust[uint2(16u + index, 0)], nb);
}
void trust_add_n(uint index, uint n) {
  uint s = WaveActiveSum(n);
  if (WaveIsFirstLane() && s) InterlockedAdd(sunTrust[uint2(16u + index, 0)], s);
}
