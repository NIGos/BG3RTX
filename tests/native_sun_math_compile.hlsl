// CPU compilation fixture only; never installed or dispatched.
#include "../common/native_sun_math.h"
StructuredBuffer<float4> input:register(t0);
RWStructuredBuffer<float4> result:register(u0);
[numthreads(1,1,1)]void main(uint3 id:SV_DispatchThreadID){
 float3 q[4];for(uint i=0;i<4;++i)q[i]=input[i].xyz;
 NativeSunSelection selection=ns_select(q,input[4].x,input[5],input[6],asuint(input[4].y),true,true,false);
 NativeSunCone cone=ns_cone_bounds(q[0],input[7].xyz,input[8].xyz,input[9].xyz,input[10].xyz);
 NativeSunFront front=ns_cone_front(cone,true);
 NativeSunRayFront ray=ns_ray_front(q[0],input[7].xyz,true);
 NativeSunAux aux=ns_aux(asuint(input[11].x),input[11].y,input[11].z,input[11].w);
 result[id.x]=float4(selection.selectedWeight,front.maxDistance,ray.upper,aux.active);
}
