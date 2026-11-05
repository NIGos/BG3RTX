// Ordered replay of individual additions. Never sum a ray before adding it.
#include "gi_common.hlsli"
#define GI_SUN_WAVE_AUX 1
#include "gi_sun_wave.hlsli"
#include "gi_far.hlsli"
[numthreads(8,8,1)]
void main(uint3 tid:SV_DispatchThreadID) {
  uint2 id=tid.xy;
  if(any(id>=pc.half))return;
  uint tail=sw_tail(id);
  float depth=asfloat(giSunWave.Load(tail+16u));
  if(depth<=0.0)return;
  // The trace's optimistic lit continuation is already the final answer unless
  // at least one deferred query disproves it. Preserve that exact stored value.
  bool changed=false;
  for(uint r=0;r<pc.rays;++r){
    const uint a=sw_record((id.y*pc.half.x+id.x)*pc.rays+r);
    changed=changed||((giSunWave.Load(a+4u)&0x80000000u)!=0u&&giSunWave.Load(a+8u)!=0u);
  }
  if(!changed)return;
  float3 bounce=0;
  float occ=0,fall=0;
  for(uint r=0;r<pc.rays;++r){
    const uint a=sw_record((id.y*pc.half.x+id.x)*pc.rays+r);
    const uint pending=giSunWave.Load(a+4u);
    const bool hidden=(pending&0x80000000u)!=0u;
    const uint count=hidden?(pending&0x7fffffffu):giSunWave.Load(a);
    for(uint e=0;e<count;++e){const float4 event=asfloat(giSunWave.Load4(a+16u+e*16u));bounce+=event.xyz*event.w;}
    if(!hidden){
      // Preserve conditional no-add paths: adding zero can change a signed zero.
      const uint flags=giSunWave.Load(a+12u);
      if((flags&4u)!=0u)occ+=asfloat(giSunWave.Load(a+64u))*asfloat(giSunWave.Load(a+72u));
      if((flags&8u)!=0u)fall+=asfloat(giSunWave.Load(a+68u));
      continue;
    }
    const uint flags=giSunWave.Load(a+12u), visibility=giSunWave.Load(a+8u);
    const bool sun_ok=visibility!=2u;
    const float3 local=asfloat(giSunWave.Load3(a+80u));
    const float3 sun=visibility==0u?asfloat(giSunWave.Load3(a+96u)):0.0.xxx;
    const float w=asfloat(giSunWave.Load(a+108u));
    float3 term,guard_term;float weight;
    bool radiance=true;
    if((flags&1u)!=0u){
      const float3 tw=asfloat(giSunWave.Load3(a+112u));
      const uint local_state=giSunWave.Load(a+124u);
      const float rho=asfloat(giSunWave.Load(a+128u));
      GiFarRay fr={w,tw.z};
      const GiFarHit h=gi_far_hit(fr,tw.x,tw.y,local_state,local,sun_ok&&(flags&2u)==0u,sun,rho);
      term=h.bounce;guard_term=term;weight=h.occ;radiance=h.radiance;
    }else{guard_term=local+sun;term=guard_term*w;weight=w;}
    if(radiance&&all(isfinite(guard_term))&&all(guard_term>=0.0)){
      bounce+=term;occ+=weight;
    }else if(pc.gi_rt_fallback!=1u){
      const float fw=(flags&1u)!=0u?asfloat(giSunWave.Load(a+120u)):w;
      occ+=fw;fall+=fw;
    }
  }
  const float4 seen=asfloat(giSunWave.Load4(tail));
  if(pc.gi_rt_fallback==2u&&seen.w>0.0)bounce+=seen.xyz/seen.w*fall;
  const float n=float(max(pc.rays,1u));
  float3 gi_out=bounce/n;
  gi_out=select(gi_out<=65504.0,gi_out,65504.0.xxx);
  giRaw[id]=float4(gi_out,saturate(1.0-occ/n));
  giRawDepth[id]=depth;
}
