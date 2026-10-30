#include "../../common/native_sun_math.h"
// Private current-sun build only. Conversion collapses an unrepresentable native
// triangle and writes w=0. Such a stream cannot certify absence of blockers.
[[vk::binding(0,0)]] RWByteAddressBuffer Stamp;
[[vk::binding(1,0)]] ByteAddressBuffer Vertices;
[[vk::binding(2,0)]] RWByteAddressBuffer Instances;
[[vk::binding(3,0)]] ByteAddressBuffer Parameters;
struct Push { uint tris; uint stride; uint camera_instances; uint layer; };
[[vk::push_constant]] Push pc;
[numthreads(256,1,1)]
void main(uint3 id:SV_DispatchThreadID) {
  if(pc.layer==0u && id.x<pc.camera_instances) {
    uint offset=id.x*64u+48u;
    uint packed=Instances.Load(offset);
    // Preserve disabled invalid transforms. Camera geometry is for primary
    // checks only; physical Sun rays see native Shadow membership (mask2).
    uint mask=(packed>>24u)!=0u?1u:0u;
    Instances.Store(offset,(packed&0x00ffffffu)|(mask<<24u));
  }
  if(id.x==0u) {
    float4 nativeRows[3],casterRows[3];
    [unroll]for(uint axis=0u;axis<3u;++axis) {
      uint offset=64u+64u*pc.layer+4u*axis;
      nativeRows[axis]=asfloat(uint4(Parameters.Load(offset),Parameters.Load(offset+16u),Parameters.Load(offset+32u),Parameters.Load(offset+48u)));
      casterRows[axis]=asfloat(Parameters.Load4(992u+48u*pc.layer+16u*axis));
    }
    NativeSunAffine transform=ns_affine_from_q(nativeRows,casterRows);
    bool valid=(Parameters.Load(1200u)&1u)!=0u && ns_metric_affine(transform);
    uint instance=(pc.camera_instances+pc.layer)*64u;
    [unroll]for(uint row=0u;row<3u;++row) {
      float4 identity=row==0u?float4(1,0,0,0):row==1u?float4(0,1,0,0):float4(0,0,1,0);
      Instances.Store4(instance+16u*row,asuint(valid?transform.rows[row]:identity));
    }
    if(!valid) {
      uint packed=Instances.Load(instance+48u);Instances.Store(instance+48u,packed&0x00ffffffu);
      Stamp.InterlockedAdd(16u,1u);
    }
  }
  if(id.x>=pc.tris) return;
  bool invalid=false;
  [unroll] for(uint k=0;k<3;++k) {
    float4 p=asfloat(Vertices.Load4((id.x*3+k)*pc.stride));
    invalid=invalid || !all(isfinite(p)) || p.w!=1.0;
  }
  if(invalid) Stamp.InterlockedAdd(16,1);
}
