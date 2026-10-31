// Runs after copying the immutable headers of the micromaps selected by this
// scene. A successful validation permits that exact scene to use its fast TLAS.
// Every later ray must still compare the global content epoch, atomically.
[[vk::binding(0,0)]] ByteAddressBuffer rows;
[[vk::binding(1,0)]] globallycoherent RWByteAddressBuffer epochs;
[[vk::binding(2,0)]] RWByteAddressBuffer sceneStamp;
struct Push { uint count, generation, reserved0, reserved1; };
[[vk::push_constant]] ConstantBuffer<Push> pc;
uint atomic_epoch(uint offset) {uint value;epochs.InterlockedOr(offset,0u,value);return value;}
[numthreads(1,1,1)]
void main(uint3 id:SV_DispatchThreadID) {
  // Never alter Hair byte12, Sun16..111 or the camera packet at256.
  sceneStamp.Store4(128u,uint4(0u,0u,0u,0u));
  uint rowBytes,epochBytes;rows.GetDimensions(rowBytes);epochs.GetDimensions(epochBytes);
  if(pc.count==0u || pc.generation==0u || pc.count>rowBytes/32u || epochBytes<16u || pc.reserved0!=0u || pc.reserved1!=0u)return;
  if(atomic_epoch(8u)!=0u)return;
  DeviceMemoryBarrier();
  const uint global=atomic_epoch(0u);
  if(global==0xffffffffu || atomic_epoch(4u)!=0u)return;
  for(uint i=0u;i<pc.count;++i) {
    // Header{imageEpoch,globalEpoch,invalid,magic} copied from producer data[0].
    // Remaining words are host-frozen metadata, not guessed counter values.
    const uint4 header=rows.Load4(i*32u);
    const uint4 source=rows.Load4(i*32u+16u);
    if(header.x==0u || header.x==0xffffffffu || header.y>global ||
       header.z!=0u || header.w!=0x4f4d4d31u || source.x<16u || source.x%4u!=0u ||
       source.x>epochBytes || epochBytes-source.x<4u || any(source.yzw!=0u))return;
    if(atomic_epoch(source.x)!=header.x)return;
  }
  // An unrelated write after a bake is fine if all selected image epochs still
  // match; a write during this validation requires reference traversal instead.
  DeviceMemoryBarrier();
  if(atomic_epoch(4u)!=0u || atomic_epoch(0u)!=global || atomic_epoch(8u)!=0u)return;
  sceneStamp.Store4(128u,uint4(pc.generation,global,pc.count,0x4f4d4d32u));
}
