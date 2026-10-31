#include "../common/native_opaque_transport.h"
[[vk::binding(0,0)]] StructuredBuffer<float4> input_data;
[[vk::binding(1,0)]] RWStructuredBuffer<float4> output_data;
[numthreads(1,1,1)]
void main(uint3 tid:SV_DispatchThreadID){
    uint i=tid.x;float4 a=input_data[i*5],b=input_data[i*5+1],c=input_data[i*5+2];
    NtInput data;data.linear_albedo=a.xyz;data.roughness=a.w;data.reflectance=b.x;data.metalness=b.y;
    data.material_class=asuint(b.z);data.evidence=asuint(b.w);
    NtResult r=nt_class0_parameters(data);
    NtRoughness aa=nt_native_quad_roughness(r,c.xyz,input_data[i*5+3].xyz,input_data[i*5+4].x!=0);
    NtFresnelBlendResult physical=nt_to_fresnel_blend(r);
    TbEval e=tb_fresnel_evaluate(physical.value,c.xyz,input_data[i*5+3].xyz);
    float3 u=input_data[i*5+4].yzw;
    TbSample sample=tb_fresnel_sample(physical.value,c.xyz,u.x,u.y,u.z);
    output_data[i*9]=float4(r.value.diffuse_color,float(r.status));
    output_data[i*9+1]=float4(r.value.F0,r.value.base_alpha);
    output_data[i*9+2]=float4(r.value.perceptual_roughness,r.value.base_roughness,aa.native_roughness,aa.alpha);
    output_data[i*9+3]=float4(r.value.linear_albedo,r.value.reflectance);
    output_data[i*9+4]=float4(float(aa.status),r.value.metalness,float(physical.status),float(e.status));
    output_data[i*9+5]=float4(e.f,e.pdf);
    output_data[i*9+6]=float4(sample.wi,float(sample.status));
    output_data[i*9+7]=float4(sample.f,sample.pdf);
    output_data[i*9+8]=float4(physical.value.Rd,physical.value.alpha);
}
