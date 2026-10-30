// Private current-sun build only. Conversion collapses an unrepresentable native
// triangle and writes w=0. Such a stream cannot certify absence of blockers.
[[vk::binding(0,0)]] RWByteAddressBuffer Stamp;
[[vk::binding(1,0)]] ByteAddressBuffer Vertices;
struct Push { uint tris; uint stride; };
[[vk::push_constant]] Push pc;
[numthreads(256,1,1)]
void main(uint3 id:SV_DispatchThreadID) {
  if(id.x>=pc.tris) return;
  bool invalid=false;
  [unroll] for(uint k=0;k<3;++k) {
    float4 p=asfloat(Vertices.Load4((id.x*3+k)*pc.stride));
    invalid=invalid || !all(isfinite(p)) || p.w!=1.0;
  }
  if(invalid) Stamp.InterlockedAdd(16,1);
}
