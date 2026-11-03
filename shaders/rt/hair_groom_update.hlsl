// rt_hair_groom (layer/bvhsplit.cpp, docs/RTX_HAIR_INTEGRATION.md): the strand points of one matched hair draw,
// moved with the draw's captured card triangles. Point i = bary . (v0, v1, v2) of its card triangle in this
// frame's converted soup (camera-relative world, xfb_to_world.hlsl). The groom's static arrays and its points
// live in the mesh pool. A triangle with a non-finite vertex keeps last frame's point (refits stay valid).
// Also the points' bounds (orderable uints, min xyz then max xyz) for the layer's screen-size LOD; the pool is
// device-local, the layer copies the six words to a host-visible buffer.
[[vk::binding(0, 0)]] ByteAddressBuffer Soup;
[[vk::binding(1, 0)]] RWByteAddressBuffer Pool;
struct Push { uint points, soup_base, stride, tri_off, bary_off, pos_off, bounds_off, pad; };
[[vk::push_constant]] Push pc;

uint orderable(float f) { const uint u = asuint(f); return (u & 0x80000000u) != 0u ? ~u : u | 0x80000000u; }

groupshared uint gsLo[3];
groupshared uint gsHi[3];

[numthreads(128, 1, 1)]
void main(uint3 id : SV_DispatchThreadID, uint gi : SV_GroupIndex) {
  if (gi < 3u) { gsLo[gi] = 0xFFFFFFFFu; gsHi[gi] = 0u; }
  GroupMemoryBarrierWithGroupSync();
  const uint i = id.x;
  if (i < pc.points) {
    const uint tri = Pool.Load(pc.tri_off + i * 4u);
    const float2 b = asfloat(Pool.Load2(pc.bary_off + i * 8u));
    float3 v[3];
    bool ok = true;
    [unroll] for (uint k = 0; k < 3; ++k) {
      v[k] = asfloat(Soup.Load3(pc.soup_base + (3u * tri + k) * pc.stride));
      ok = ok && all(abs(v[k]) < 1e30);
    }
    if (ok) {
      const float3 p = v[0] * (1.0 - b.x - b.y) + v[1] * b.x + v[2] * b.y;
      Pool.Store3(pc.pos_off + i * 12u, asuint(p));
      [unroll] for (uint c = 0; c < 3; ++c) {
        InterlockedMin(gsLo[c], orderable(p[c]));
        InterlockedMax(gsHi[c], orderable(p[c]));
      }
    }
  }
  GroupMemoryBarrierWithGroupSync();
  if (gi < 3u && gsLo[gi] <= gsHi[gi]) {
    Pool.InterlockedMin(pc.bounds_off + gi * 4u, gsLo[gi]);
    Pool.InterlockedMax(pc.bounds_off + 12u + gi * 4u, gsHi[gi]);
  }
}
