// GPU copies actual native receiver UBOs, including device-local/unmapped ones.
struct Sun {uint4 words[34];};struct Aux {uint4 words[9];};struct Camera {uint4 words[14];};
struct Caster {uint4 words[12];};
struct Frame {uint4 words[2];};struct Rays {uint4 words[10];};
[[vk::binding(0,0)]] ConstantBuffer<Sun> nativeSun;
[[vk::binding(1,0)]] ConstantBuffer<Aux> nativeAux;
[[vk::binding(2,0)]] ConstantBuffer<Camera> nativeCamera;
[[vk::binding(3,0)]] ConstantBuffer<Frame> nativeFrame;
[[vk::binding(4,0)]] ConstantBuffer<Rays> nativeRays;
[[vk::binding(5,0)]] ConstantBuffer<Camera> lightingCamera;
[[vk::binding(6,0)]] ConstantBuffer<Frame> lightingFrame;
[[vk::binding(7,0)]] ConstantBuffer<Rays> lightingRays;
[[vk::binding(8,0)]] ConstantBuffer<Caster> caster0;
[[vk::binding(9,0)]] ConstantBuffer<Caster> caster1;
[[vk::binding(10,0)]] ConstantBuffer<Caster> caster2;
[[vk::binding(11,0)]] ConstantBuffer<Caster> caster3;
[[vk::binding(12,0)]] RWByteAddressBuffer parameters;
float4 caster_row(uint layer,uint row) {
  if(layer==0u)return asfloat(caster0.words[row]);if(layer==1u)return asfloat(caster1.words[row]);
  if(layer==2u)return asfloat(caster2.words[row]);return asfloat(caster3.words[row]);
}
[numthreads(64,1,1)]
void main(uint3 id:SV_DispatchThreadID) {
  uint i=id.x;
  if(i<34u)parameters.Store4(i*16u,nativeSun.words[i]);
  else if(i<43u)parameters.Store4(i*16u,nativeAux.words[i-34u]);
  else if(i<57u)parameters.Store4(i*16u,nativeCamera.words[i-43u]);
  else if(i<59u)parameters.Store4(i*16u,nativeFrame.words[i-57u]);
  else if(i<62u)parameters.Store4(i*16u,nativeRays.words[i-59u+7u]);
  if(i<12u) {
    uint layer=i/3u,axis=i%3u;
    float scale=axis==0u?.5:axis==1u?((parameters.Load(1216u)&(1u<<layer))!=0u?.5:-.5):1.0;
    float4 row=float4(caster_row(layer,8u)[axis],caster_row(layer,9u)[axis],caster_row(layer,10u)[axis],caster_row(layer,11u)[axis])*scale;
    float offset=axis<2u?.5:0.0;
    float error=1.19209304e-7*(abs(row.w)+offset);row.w+=offset;
    parameters.Store4(992u+16u*i,asuint(row));
    uint ignored;parameters.InterlockedMax(1184u+4u*layer,asuint(error),ignored);
  }
  if(i==63u) {
    bool frame=true,camera=true,rays=true;
    [unroll]for(uint j=0;j<14u;++j)camera=camera && all(nativeCamera.words[j]==lightingCamera.words[j]);
    [unroll]for(uint j=0;j<2u;++j)frame=frame && all(nativeFrame.words[j]==lightingFrame.words[j]);
    [unroll]for(uint j=7u;j<10u;++j)rays=rays && all(nativeRays.words[j]==lightingRays.words[j]);
    bool casters=true;
    [unroll]for(uint layer=0u;layer<4u;++layer) {
      [unroll]for(uint row=8u;row<12u;++row)casters=casters && all(isfinite(caster_row(layer,row)));
      casters=casters && caster_row(layer,8u).w==0.0 && caster_row(layer,9u).w==0.0 &&
        caster_row(layer,10u).w==0.0 && caster_row(layer,11u).w==1.0;
    }
    parameters.Store(1200u,(frame && camera && rays && casters?1u:0u)|(frame?2u:0u)|(camera?4u:0u)|(rays?8u:0u));
  }
}
