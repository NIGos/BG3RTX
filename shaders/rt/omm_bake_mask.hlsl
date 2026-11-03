// rt_omm_dyn (docs/P4_OMM.md 11.2c): four-state micromaps of masked per-draw BLAS (camp grass: wind-deformed,
// refit every frame) from the immutable offline mask at mip 0. No ordinary shader includes this file.
//   bake:  one workgroup per triangle of the draw's soup range, one thread per micro-triangle (subdivision 4);
//          writes the EXT OC1_4_STATE data (64 bytes per triangle) and a snapshot of the triangle's uv bits.
//   check: every frame, before the BLAS build or refit: a triangle whose uv bits still equal the snapshot keeps
//          its micro-map (index = triangle), any other gets FULLY_UNKNOWN_OPAQUE (the any-hit decides). So the
//          micro-map is exact per triangle and per frame, whatever the identity it was found under.
// Counters (device-local, wave-reduced, copied to a host readback after the build; layer/bvhsplit.cpp):
//   0 known opaque, 1 known transparent, 2 unknown micro-triangles baked; 3 triangles checked, 4 of them with
//   changed uvs; 5 triangles baked.
struct Push {
  uint soup_off, stride, tris, groups_x;
  float cut_lo, cut_hi, scale_u, scale_v;
  uint index_off, mask_w, mask_h, pad;
};
[[vk::push_constant]] ConstantBuffer<Push> pc;
[[vk::binding(0, 0)]] ByteAddressBuffer soup;
[[vk::binding(1, 0)]] Texture2D<float4> maskTexture;
[[vk::binding(2, 0)]] RWByteAddressBuffer opacityData;
[[vk::binding(3, 0)]] RWByteAddressBuffer uvSnapshot;
[[vk::binding(4, 0)]] RWByteAddressBuffer bakeStats;
[[vk::binding(5, 0)]] RWByteAddressBuffer microIndex;

float omb_fetch(int x, int y) { return maskTexture.Load(int3(x, y, 0)).x; }
#include "../../common/omm_mask_bake.h"

void count(uint slot, bool c) {
  const uint n = WaveActiveCountBits(c);
  if (WaveIsFirstLane() && n) bakeStats.InterlockedAdd(slot * 4u, n);
}

groupshared uint gWords[16];

[numthreads(256, 1, 1)]
void bake(uint3 group : SV_GroupID, uint3 lane : SV_GroupThreadID) {
  const uint tri = group.y * pc.groups_x + group.x;
  const uint micro = lane.x;
  if (micro < 16u) gWords[micro] = 0u;
  GroupMemoryBarrierWithGroupSync();
  if (tri >= pc.tris) return;   // uniform per group
  const uint base = pc.soup_off + tri * 3u * pc.stride;
  const uint2 r0 = soup.Load2(base + 16u), r1 = soup.Load2(base + pc.stride + 16u), r2 = soup.Load2(base + 2u * pc.stride + 16u);
  const float2 scale = float2(pc.scale_u, pc.scale_v);
  const float2 u0 = asfloat(r0) * scale, u1 = asfloat(r1) * scale, u2 = asfloat(r2) * scale;
  uint w, h, levels;
  maskTexture.GetDimensions(0, w, h, levels);
  uint state = 3u;
  if (w == pc.mask_w && h == pc.mask_h)
    state = omb_micro_state(micro, kOmbLevel, u0.x, u0.y, u1.x, u1.y, u2.x, u2.y, w, h, pc.cut_lo, pc.cut_hi);
  InterlockedOr(gWords[micro / 16u], state << (2u * (micro % 16u)));
  count(0u, state == 1u);
  count(1u, state == 0u);
  count(2u, state == 3u);
  GroupMemoryBarrierWithGroupSync();
  if (micro < 16u) opacityData.Store(tri * 64u + micro * 4u, gWords[micro]);
  if (micro == 0u) {
    uvSnapshot.Store4(tri * 24u, uint4(r0, r1));
    uvSnapshot.Store2(tri * 24u + 16u, r2);
    bakeStats.InterlockedAdd(20u, 1u);
  }
}

[numthreads(64, 1, 1)]
void check(uint3 id : SV_DispatchThreadID) {
  const uint tri = id.x;
  if (tri >= pc.tris) return;
  const uint base = pc.soup_off + tri * 3u * pc.stride;
  const uint4 a = uint4(soup.Load2(base + 16u), soup.Load2(base + pc.stride + 16u));
  const uint2 b = soup.Load2(base + 2u * pc.stride + 16u);
  const uint4 sa = uvSnapshot.Load4(tri * 24u);
  const uint2 sb = uvSnapshot.Load2(tri * 24u + 16u);
  const bool same = all(a == sa) && all(b == sb);
  microIndex.Store(pc.index_off + tri * 4u, same ? tri : kOmbUnknownIndex);
  count(3u, true);
  count(4u, !same);
}
