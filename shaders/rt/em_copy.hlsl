// D-EM: keeps the game's luminance scale (set 2 b0 [0] of a luminance-emissive draw) as the
// draw saw it. The game rewrites that buffer later in the same batch, so it is copied right
// after the effects pass, in the pass's command buffer, into slot `slot` of our own buffer
// (layer/emissive.cpp); records refer to the slot (em_common.hlsli, EmRec.glob).
[[vk::binding(0, 0)]] ByteAddressBuffer Src;
[[vk::binding(1, 0)]] RWByteAddressBuffer Dst;

struct Push { uint slot; uint diag; uint bytes; uint pad; };   // diag: word index of the source's first 4 words; bytes: bound
[[vk::push_constant]] Push pc;

[numthreads(1, 1, 1)]
void main() {
  float k = asfloat(Src.Load(0));
  Dst.Store(pc.slot * 4, asuint(k > 0.0 && k < 1e6 ? k : 0.0));
  // Diagnostics (logged): the source's first 4 words, raw, as far as they are bound.
  for (uint i = 0; i < 4; ++i) Dst.Store(pc.diag * 4 + i * 4, i * 4 + 4 <= pc.bytes ? Src.Load(i * 4) : 0u);
}
