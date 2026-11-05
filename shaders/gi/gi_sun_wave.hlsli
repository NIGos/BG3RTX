#pragma once
#include "../../common/sun_wave.h"
#if RT_HIT_GI_SUN || defined(GI_SUN_WAVE_AUX)
[[vk::constant_id(GI_SUN_WAVE_SPEC_ID)]] const uint kGiSunWave = 0u;
[[vk::binding(51, 0)]] RWByteAddressBuffer giSunWave;
static uint g_sw_addr = 0u;
static bool g_sw_queued = false;
static float3 g_sw_sun_lit = 0.0.xxx;
uint sw_slots() { return pc.half.x * pc.half.y * pc.rays; }
uint sw_records() { return GI_SUN_WAVE_HEADER_BYTES + sw_slots() * 4u; }
uint sw_record(uint slot) { return sw_records() + slot * GI_SUN_WAVE_RECORD_BYTES; }
uint sw_tail(uint2 id) { return sw_records() + sw_slots() * GI_SUN_WAVE_RECORD_BYTES + (id.y * pc.half.x + id.x) * GI_SUN_WAVE_PIXEL_BYTES; }
void sw_begin(uint2 id, uint r) {
  if (kGiSunWave == 0u) return;
  g_sw_addr = sw_record((id.y * pc.half.x + id.x) * pc.rays + r);
  giSunWave.Store4(g_sw_addr, uint4(0,0,0,0));
  giSunWave.Store2(g_sw_addr + 64u, uint2(0,0));
  g_sw_queued = false;
}
void sw_bounce(inout float3 bounce, float3 term) {
  if (kGiSunWave != 0u) {
    uint count = giSunWave.Load(g_sw_addr);
    giSunWave.Store4(g_sw_addr + 16u + count * 16u, asuint(float4(term,1.0)));
    giSunWave.Store(g_sw_addr, count + 1u);
  }
  bounce += term;
}
void sw_bounce_mul(inout float3 bounce, float3 term, float weight) {
  if (kGiSunWave != 0u) {
    uint count=giSunWave.Load(g_sw_addr);
    giSunWave.Store4(g_sw_addr+16u+count*16u,asuint(float4(term,weight)));
    giSunWave.Store(g_sw_addr,count+1u);
  }
  bounce += term * weight;
}
void sw_bounce_far(inout float3 bounce, float3 term, float3 operands, float weight) {
  if (kGiSunWave != 0u) {
    uint count=giSunWave.Load(g_sw_addr);
    giSunWave.Store4(g_sw_addr+16u+count*16u,asuint(float4(operands,weight)));
    giSunWave.Store(g_sw_addr,count+1u);
  }
  bounce += term;
}
void sw_occ(inout float occ, float term) {
  if (kGiSunWave != 0u) { giSunWave.Store(g_sw_addr + 64u, asuint(term)); giSunWave.Store(g_sw_addr + 72u, asuint(1.0)); giSunWave.Store(g_sw_addr+12u,giSunWave.Load(g_sw_addr+12u)|4u); }
  occ += term;
}
void sw_occ_far(inout float occ, float term, float factor, float weight) {
  if (kGiSunWave != 0u) {
    giSunWave.Store(g_sw_addr+64u,asuint(factor));
    giSunWave.Store(g_sw_addr+72u,asuint(weight));
    giSunWave.Store(g_sw_addr+12u,giSunWave.Load(g_sw_addr+12u)|4u);
  }
  occ += term;
}
void sw_fall(inout float fall, float term) {
  if (kGiSunWave != 0u) { giSunWave.Store(g_sw_addr + 68u, asuint(term)); giSunWave.Store(g_sw_addr+12u,giSunWave.Load(g_sw_addr+12u)|8u); }
  fall += term;
}
void sw_pixel(uint2 id, float3 seen, float nseen, float depth) {
  uint a = sw_tail(id);
  giSunWave.Store4(a, asuint(float4(seen, nseen)));
  giSunWave.Store(a + 16u, asuint(depth));
}
void sw_queue(float3 origin, float3 direction, float tmax, float footprint, float3 lit) {
  giSunWave.Store3(g_sw_addr + 132u, asuint(origin));
  giSunWave.Store3(g_sw_addr + 144u, asuint(direction));
  giSunWave.Store2(g_sw_addr + 156u, asuint(float2(footprint,tmax)));
  g_sw_sun_lit = lit;
  uint queue;
  giSunWave.InterlockedAdd(0u, 1u, queue);
  giSunWave.Store(GI_SUN_WAVE_HEADER_BYTES + queue * 4u, (g_sw_addr-sw_records()) / GI_SUN_WAVE_RECORD_BYTES);
  g_sw_queued = true;
}
void sw_pending(float3 local, float3 sun, float w, float t, float rb, float w0, uint local_state, float rho, bool far_on, bool roof) {
  giSunWave.Store(g_sw_addr + 4u, 0x80000000u | giSunWave.Load(g_sw_addr));
  giSunWave.Store(g_sw_addr + 12u, (far_on ? 1u : 0u) | (roof ? 2u : 0u));
  giSunWave.Store3(g_sw_addr + 80u, asuint(local));
  giSunWave.Store4(g_sw_addr + 96u, asuint(float4(sun,w)));
  giSunWave.Store3(g_sw_addr + 112u, asuint(float3(t,rb,w0)));
  giSunWave.Store(g_sw_addr + 124u, local_state);
  giSunWave.Store(g_sw_addr + 128u, asuint(rho));
}
#else
// Ordinary GI pipelines do not have the native-hit union's binding 51.
static const uint kGiSunWave=0u;
void sw_begin(uint2 id,uint r) {}
void sw_bounce(inout float3 bounce,float3 term) { bounce+=term; }
void sw_bounce_mul(inout float3 bounce,float3 term,float weight) { bounce+=term*weight; }
void sw_bounce_far(inout float3 bounce,float3 term,float3 operands,float weight) { bounce+=term; }
void sw_occ(inout float occ,float term) { occ+=term; }
void sw_occ_far(inout float occ,float term,float factor,float weight) { occ+=term; }
void sw_fall(inout float fall,float term) { fall+=term; }
void sw_pixel(uint2 id,float3 seen,float nseen,float depth) {}
#endif
