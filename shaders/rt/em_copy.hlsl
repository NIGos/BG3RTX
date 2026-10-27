// D-EM: keeps the game's luminance scale (set 2 b0 [0] of a luminance-emissive draw) as the
// draw saw it. The game rewrites that buffer later in the same batch, so it is copied right
// after the effects pass, in the pass's command buffer, into slot `slot` of our own buffer
// (layer/emissive.cpp); records refer to the slot (em_common.hlsli, EmRec.glob).
[[vk::binding(0, 0)]] ByteAddressBuffer Src;
[[vk::binding(1, 0)]] RWByteAddressBuffer Dst;

struct Push { uint slot; uint pad0, pad1, pad2; };
[[vk::push_constant]] Push pc;

[numthreads(1, 1, 1)]
void main() {
  float k = asfloat(Src.Load(0));
  Dst.Store(pc.slot * 4, asuint(k > 0.0 && k < 1e6 ? k : 0.0));
}
