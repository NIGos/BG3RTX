// Exact opt-in terrain brush bytes; no reinterpretation of native parameters.
[[vk::binding(0,0)]] ByteAddressBuffer source;
[[vk::binding(1,0)]] RWByteAddressBuffer destination;
[[vk::push_constant]] struct Push { uint words; } push;
[numthreads(64,1,1)]
void main(uint3 id:SV_DispatchThreadID) {
 if(id.x<push.words)destination.Store(id.x*4u,source.Load(id.x*4u));
}
