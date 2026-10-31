// Execution-time native UBO/tileset snapshot after a verified read-only G-buffer
// pass. CPU read_mapped values never certify the material presented to Nt.
struct Engine {float4 v[6];uint4 words;};struct Physical {float4 value;};
[[vk::binding(0,0)]] ConstantBuffer<Engine> engine;
[[vk::binding(1,0)]] ConstantBuffer<Physical> physical;
[[vk::binding(2,0)]] ByteAddressBuffer tilesets;
[[vk::binding(3,0)]] RWByteAddressBuffer rows;
struct Push {uint row,record,reserved0,reserved1;};[[vk::push_constant]] Push push;
[numthreads(1,1,1)]
void main(){
 uint index=engine.words.y,bytes;tilesets.GetDimensions(bytes);
 if(index>=bytes/128u)return;
 // The ordinary alpha recipe is the association hint. If late updates make it
 // disagree with execution-time deferred state, the provider declines instead
 // of pretending that its alpha coverage/source join is qualified.
 if(any(asuint(engine.v[4])!=rows.Load4(push.row+48u)) || any(asuint(engine.v[5])!=rows.Load4(push.row+64u)))return;
 [unroll]for(uint i=0;i<8;++i)if(any(tilesets.Load4(index*128u+i*16u)!=rows.Load4(push.row+80u+i*16u)))return;
 float reflectance=physical.value.x;if(!isfinite(reflectance))return;
 rows.Store(push.row+208u,asuint(reflectance));
 rows.Store(push.row+212u,engine.words.z&7u); // native material light channel, UBO104
 // Valid is published only after all exact execution-time reads/checks.
 rows.Store(push.row,push.record+1u);
}
