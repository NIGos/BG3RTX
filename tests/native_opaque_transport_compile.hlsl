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
    output_data[i*5]=float4(r.value.diffuse_color,float(r.status));
    output_data[i*5+1]=float4(r.value.F0,r.value.base_alpha);
    output_data[i*5+2]=float4(r.value.perceptual_roughness,r.value.base_roughness,aa.native_roughness,aa.alpha);
    output_data[i*5+3]=float4(r.value.linear_albedo,r.value.reflectance);
    output_data[i*5+4]=float4(float(aa.status),r.value.metalness,0,0);
}
