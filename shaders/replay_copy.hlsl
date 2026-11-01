// Offline replay pack writer (layer/replay.cpp, diagnostics only; tools/frame_replay.md): copies
// `words` 32-bit words of a storage buffer without TRANSFER_SRC usage (the soup, pool, records,
// light lists, the instance ring) into the host-visible staging buffer. A shader read is the only
// way to get at such a buffer. Offsets are bytes: src relative to the bound range, dst to the chunk.
[[vk::binding(0, 0)]] ByteAddressBuffer Src;
[[vk::binding(1, 0)]] RWByteAddressBuffer Dst;

struct Push { uint src; uint dst; uint words; uint pad; };
[[vk::push_constant]] Push pc;

[numthreads(256, 1, 1)]
void main(uint i : SV_DispatchThreadID) {
  if (i < pc.words) Dst.Store(pc.dst + i * 4, Src.Load(pc.src + i * 4));
}
