// CPU-only compile fixture. No device dispatch or material/quality gate.
#include "../common/native_terrain_surface.h"
StructuredBuffer<float4> inputs:register(t0);
RWStructuredBuffer<float4> outputs:register(u1);
[numthreads(1,1,1)]
void main(uint3 id:SV_DispatchThreadID) {
    uint i=id.x;float4 a=inputs[i*5],b=inputs[i*5+1],c=inputs[i*5+2],d=inputs[i*5+3],e=inputs[i*5+4];
    NttProjection p=ntt_project(a.x,a.y,a.z,ntt_v3(b.x,b.y,b.z));
    NttScore s=ntt_height_score(c.x,c.y,c.z,c.w,d.x,d.y);
    NttBlend blend=ntt_blend(ntt_v4(s.value,d.z,d.w,e.x),ntt_v4(c.x,e.y,e.z,e.w),a.w);
    NttBaseBlend base=ntt_base_curves(blend.base,ntt_v2(b.w,d.x),ntt_v2(d.y,e.x));
    outputs[i*4]=float4(p.uv.x,p.uv.y,p.orientation,float(p.status));
    outputs[i*4+1]=float4(blend.painted.x,blend.painted.y,blend.painted.z,blend.painted.w);
    outputs[i*4+2]=float4(base.weights.x,base.weights.y,float(s.status),float(base.status));
    NttInputEvidence missing;missing.packet_bytes=0;missing.mask_mapping=0;missing.samplers=0;missing.derivatives=0;
    outputs[i*4+3]=float4(float(ntt_input_status(missing)),float(blend.status),float(p.rotate_xy),blend.base);
}
