// rt_hit_material_instanced (common/native_hit_instanced.h match_instance / remap_instances, the CPU
// reference): the instance order of one instanced row, after hit_material_join.hlsl copied it. The depth
// draw (BVH soup and the row's `pre` copy) and the G-buffer draw (the packet's XFB chunk) of one mesh may
// hold the same instances in another order; the hit's clip + UV check then rejects every reordered one.
// Mode 0, one thread per depth instance i: the first G-buffer instance j (from i on, wrapping) whose
// first triangle has i's clip and UV bit for bit; perm[i] = j, or i when none (unmatched: the hit check
// still rejects it, fail closed). Mode 1, one thread per 4 B of the row's deferred copy: the bytes of
// G-buffer instance perm[i] where perm[i] != i. Counters per family (4 words at push.counters): rows,
// instances in the same order, permuted, unmatched. Runs only on a published query whose join published
// this row (the same control word and row word as hit_material_join.hlsl).
[[vk::binding(0,0)]] ByteAddressBuffer queryRows;
[[vk::binding(1,0)]] ByteAddressBuffer deferredBasis;
[[vk::binding(2,0)]] RWByteAddressBuffer queryBasis;
[[vk::binding(3,0)]] RWByteAddressBuffer remap;
struct Push {uint control,row,record,pre,def,deferred,instances,block,stride,perm,counters,mode;};
[[vk::push_constant]] Push push;
bool same_first_triangle(uint pre,uint def){
 [unroll]for(uint v=0;v<3;++v){
  const uint a=pre+v*push.stride,b=def+v*push.stride;
  if(any(queryBasis.Load4(a)!=deferredBasis.Load4(b)) || any(queryBasis.Load2(a+16u)!=deferredBasis.Load2(b+16u)))return false;
 }
 return true;
}
[numthreads(64,1,1)]
void main(uint3 id:SV_DispatchThreadID){
 if(queryRows.Load(push.control)!=1u || queryRows.Load(push.row)!=push.record+1u)return;
 if(push.mode==0u){
  const uint i=id.x;if(i>=push.instances)return;
  uint j=i,found=0xffffffffu;
  for(uint k=0;k<push.instances;++k){
   if(same_first_triangle(push.pre+i*push.block,push.deferred+j*push.block)){found=j;break;}
   j=j+1u==push.instances?0u:j+1u;
  }
  remap.Store(push.perm+4u*i,found==0xffffffffu?i:found);
  uint ignored;
  remap.InterlockedAdd(push.counters+(found==i?4u:(found==0xffffffffu?12u:8u)),1u,ignored);
  if(i==0u)remap.InterlockedAdd(push.counters,1u,ignored);
  return;
 }
 const uint offset=id.x*4u;if(offset>=push.instances*push.block)return;
 const uint i=offset/push.block,j=remap.Load(push.perm+4u*i);
 if(j==i || j>=push.instances)return;
 queryBasis.Store(push.def+offset,deferredBasis.Load(push.deferred+j*push.block+(offset-i*push.block)));
}
