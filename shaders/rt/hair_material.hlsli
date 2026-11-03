// Shared full native material sampling. Callers provide hair_param and hair_sample.
#pragma once
struct HairMaterialSample {float4 mask;float height;bool ok;};
HairMaterialSample hair_material(uint a,float2 uv,float g,uint mask_tex,uint noise_tex,bool evaluate_depth) {
  HairMaterialSample r=(HairMaterialSample)0;
  float ns=hair_param(a,2), shift=hair_param(a,3);
  float2 nuv=uv*ns;
  if(!all(isfinite(nuv)))return r;
  float4 noise=hair_sample(noise_tex,nuv,g*abs(ns));
  float2 duv=(mad(noise.x,2.0,-1.0)*shift).xx;
  if(!all(isfinite(duv)) || !all(isfinite(uv+duv)))return r;
  float4 ca=hair_sample(mask_tex,uv+duv,g), cb=hair_sample(mask_tex,uv,g);
  if(!all(isfinite(noise)) || !all(isfinite(ca)) || !all(isfinite(cb)))return r;
  float4 center=float4(0.5,0,0,0), x=ca-center, y=cb-center;
  r.mask=mad(max(sign(x),sign(y)),max(abs(x),abs(y)),center);
  bool camera=gAlphaRec.Load(a+20u)!=0u;
  if(camera && evaluate_depth && (hair_param(a,4)!=0.0 || hair_param(a,5)!=0.0)) {
    float width=saturate(hair_param(a,7));
    float blend=smoothstep(mad(-width,0.5,hair_param(a,6)),mad(width,0.5,hair_param(a,6)),r.mask.z);
    r.height=max((1.0-r.mask.y)*lerp(hair_param(a,4)*0.01,hair_param(a,5)*0.01,blend),0.0);
  }
  r.ok=all(isfinite(r.mask)) && isfinite(r.height);return r;
}
