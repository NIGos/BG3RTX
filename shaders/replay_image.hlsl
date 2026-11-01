// Offline replay pack writer (layer/replay.cpp, diagnostics only; tools/frame_replay.md): reads the
// texels of a game image without TRANSFER_SRC usage through a sampled 2D-array view (Load: the
// decoded value a shader fetches, no filter) and stores them packed into the host-visible staging
// buffer, `channels` components encoded as common/replay_pack.h Encode (shader_read_format): BCn
// decoded to 8-bit UNORM / SNORM / sRGB or half. Texels [first, first + count) of mip `mip`
// (w x h, layer-major, packed rows); dst is bytes into the staging. Each thread writes whole words:
// shader_read_per_thread texels (1, 2 or 4) or one texel of 2 or 4 words.
[[vk::binding(0, 0)]] Texture2DArray<float4> Src;
[[vk::binding(1, 0)]] RWByteAddressBuffer Dst;

struct Push { uint dst; uint first; uint count; uint w; uint h; uint mip; uint encode; uint channels; };
[[vk::push_constant]] Push pc;

// replay_pack.h Encode: 1 unorm8, 2 snorm8, 3 srgb8, 4 unorm16, 5 half, 6 float.
uint encode_bits(uint e) { return e == 6u ? 32u : (e == 4u || e == 5u) ? 16u : 8u; }
float srgb_encode(float l) {
  l = saturate(l);
  return l <= 0.0031308 ? l * 12.92 : 1.055 * pow(l, 1.0 / 2.4) - 0.055;
}
uint encode(float v, uint e) {
  if (e == 1u) return uint(round(saturate(v) * 255.0));
  if (e == 2u) return uint(int(round(clamp(v, -1.0, 1.0) * 127.0))) & 255u;
  if (e == 3u) return uint(round(srgb_encode(v) * 255.0));
  if (e == 4u) return uint(round(saturate(v) * 65535.0));
  if (e == 5u) return f32tof16(v);
  return asuint(v);
}

[numthreads(256, 1, 1)]
void main(uint i : SV_DispatchThreadID) {
  const uint bits = encode_bits(pc.encode), texel_bits = bits * pc.channels;
  const uint per = texel_bits >= 32u ? 1u : 32u / texel_bits;
  const uint t0 = i * per;
  if (t0 >= pc.count) return;
  uint words[4] = {0u, 0u, 0u, 0u};
  for (uint j = 0; j < per; ++j) {
    const uint t = t0 + j;
    if (t >= pc.count) break;
    const uint lin = pc.first + t, plane = pc.w * pc.h;
    const float4 v = Src.Load(int4(int(lin % pc.w), int((lin % plane) / pc.w), int(lin / plane), int(pc.mip)));
    for (uint c = 0; c < pc.channels; ++c) {
      const uint bit = (j * pc.channels + c) * bits;
      const uint e = pc.encode == 3u && c == 3u ? 1u : pc.encode;   // sRGB alpha is linear
      words[bit >> 5] |= encode(v[c], e) << (bit & 31u);
    }
  }
  const uint nwords = max(1u, (per * texel_bits) >> 5);
  const uint at = pc.dst + i * nwords * 4u;
  for (uint k = 0; k < nwords; ++k) Dst.Store(at + k * 4u, words[k]);
}
