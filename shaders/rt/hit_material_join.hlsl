// Query-owned assembly. An invalid actual-submit proof never reads producers.
[[vk::binding(0,0)]] ByteAddressBuffer sourceBasis;
[[vk::binding(1,0)]] ByteAddressBuffer deferredBasis;
[[vk::binding(2,0)]] ByteAddressBuffer snapshotRows;
[[vk::binding(3,0)]] RWByteAddressBuffer queryRows;
[[vk::binding(4,0)]] RWByteAddressBuffer queryBasis;
struct Push {uint row,snapshot,source,deferred,bytes,control,record,local;};
[[vk::push_constant]] Push push;
[numthreads(64,1,1)]
void main(uint3 id:SV_DispatchThreadID){
 // Host-coherent, query-private word is initialized zero before any GPU use.
 // Written only before the FIRST successful submission; never a frame-age flag.
 if(queryRows.Load(push.control)!=1u)return;
 if(snapshotRows.Load(push.snapshot)!=push.local+1u)return;
 uint family=queryRows.Load(push.row+44u);bool opaque=family==1u,tileset=family==2u;
 if(!opaque && !tileset){[unroll]for(uint i=0;i<10;++i)
  if(any(snapshotRows.Load4(push.snapshot+48u+i*16u)!=queryRows.Load4(push.row+48u+i*16u)))return;}
 // Opaque depth has no alpha-material UBO. Its matched native deferred draw
 // is the authoritative execution snapshot; geometry is still checked per hit.
 if(opaque && id.x==0u){[unroll]for(uint i=0;i<10;++i)queryRows.Store4(push.row+48u+i*16u,snapshotRows.Load4(push.snapshot+48u+i*16u));}
 if(tileset && id.x==0u)queryRows.Store4(push.row+48u,snapshotRows.Load4(push.snapshot+48u));
 uint3 header=queryRows.Load3(push.row+8u); // pre, deferred, stride (byte offsets)
 uint offset=id.x*4u;
 if(offset<push.bytes){
  queryBasis.Store(header.x+offset,sourceBasis.Load(push.source+offset));
  queryBasis.Store(header.y+offset,deferredBasis.Load(push.deferred+offset));
 }
 if(id.x==0u){
  queryRows.Store2(push.row+208u,snapshotRows.Load2(push.snapshot+208u));
  queryRows.Store(push.row,push.record+1u);
 }
 // The following command-buffer CS-write -> CS-read dependency covers ALL
 // dispatch groups, including rows published before the last basis copy group.
}
