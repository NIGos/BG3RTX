// The baseline ring receives native matrix fixups at submission time. Copy it
// on the GPU first; change ONLY accelerationStructureReference, never transforms,
// instance order, masks, custom indices or material lookup data.
[[vk::binding(0,0)]] RWByteAddressBuffer instances;
[[vk::binding(1,0)]] ByteAddressBuffer replacements;
struct Push {uint count,reserved0,reserved1,reserved2;};
[[vk::push_constant]] ConstantBuffer<Push> pc;
[numthreads(64,1,1)]
void main(uint3 id:SV_DispatchThreadID){
  const uint i=id.x;if(i>=pc.count)return;
  uint dstBytes,srcBytes;instances.GetDimensions(dstBytes);replacements.GetDimensions(srcBytes);
  if(pc.count>dstBytes/64u || pc.count>srcBytes/8u || (pc.reserved0|pc.reserved1|pc.reserved2)!=0u)return;
  const uint2 address=replacements.Load2(i*8u);
  if(any(address!=0u))instances.Store2(i*64u+56u,address);
}
