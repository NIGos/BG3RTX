// Private current-sun build only. Conversion collapses an unrepresentable native
// triangle and writes w=0. Such a stream cannot certify absence of blockers.
[[vk::binding(0,0)]] RWByteAddressBuffer Stamp;
[[vk::binding(1,0)]] ByteAddressBuffer Vertices;
[[vk::binding(2,0)]] RWByteAddressBuffer Instances;
struct Push { uint tris; uint stride; uint camera_instances; };
[[vk::push_constant]] Push pc;
[numthreads(256,1,1)]
void main(uint3 id:SV_DispatchThreadID) {
  if(id.x<pc.camera_instances) {
    uint offset=id.x*64u+48u;
    uint packed=Instances.Load(offset);
    // Preserve disabled invalid transforms. Camera geometry is for primary
    // checks only; physical Sun rays see native Shadow membership (mask2).
    uint mask=(packed>>24u)!=0u?1u:0u;
    Instances.Store(offset,(packed&0x00ffffffu)|(mask<<24u));
  }
  if(id.x>=pc.tris) return;
  bool invalid=false;
  [unroll] for(uint k=0;k<3;++k) {
    float4 p=asfloat(Vertices.Load4((id.x*3+k)*pc.stride));
    invalid=invalid || !all(isfinite(p)) || p.w!=1.0;
  }
  if(invalid) Stamp.InterlockedAdd(16,1);
}
