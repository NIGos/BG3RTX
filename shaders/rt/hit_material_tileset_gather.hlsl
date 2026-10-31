// Actual native ENV_Tileset_MultipleTextures deferred scalar UBOs.
struct Channel {uint4 value;};struct Parameters {float4 value;};
[[vk::binding(0,0)]] ConstantBuffer<Channel> channel;
[[vk::binding(1,0)]] ConstantBuffer<Parameters> parameters;
[[vk::binding(3,0)]] RWByteAddressBuffer rows;
struct Push {uint row,record,reserved0,reserved1;};[[vk::push_constant]] Push push;
[numthreads(1,1,1)]void main(){
 if(!all(isfinite(parameters.value)))return;
 rows.Store4(push.row+48u,asuint(parameters.value));
 rows.Store(push.row+208u,asuint(parameters.value.w));
 rows.Store(push.row+212u,channel.value.x&7u);
 rows.Store(push.row,push.record+1u);
}
