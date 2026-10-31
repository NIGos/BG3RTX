// Compile/validate only. No device dispatch; dynamic data keeps every path alive.
#include "../common/transport_bsdf.h"
StructuredBuffer<float4> input_data : register(t0);
RWStructuredBuffer<float4> output_data : register(u1);
[numthreads(1,1,1)]
void main(uint3 tid : SV_DispatchThreadID) {
    uint i=tid.x;float4 a=input_data[i*6],b=input_data[i*6+1],c=input_data[i*6+2];
    TbMaterial m;m.rho=a.xyz;m.F0=b.xyz;m.diffuse_weight=a.w;m.specular_weight=b.w;m.alpha=c.w;
    TbEval e=tb_evaluate(m,c.xyz,input_data[i*6+3].xyz);
    float3 u=input_data[i*6+4].xyz;
    TbSample s=tb_sample(m,c.xyz,u.x,u.y,u.z);
    output_data[i*4]=float4(e.f,e.pdf);output_data[i*4+1]=float4(s.wi,float(s.status));
    output_data[i*4+2]=float4(s.f,s.pdf);output_data[i*4+3]=float4(float(e.status),0,0,0);
}
