// rt_hair_groom (layer/bvhsplit.cpp, docs/RTX_HAIR_INTEGRATION.md): the strand points of one matched hair draw,
// moved with the draw's captured card triangles. Point i = bary . (v0, v1, v2) of its card triangle in this
// frame's converted soup (camera-relative world, xfb_to_world.hlsl). The groom's static arrays and its points
// live in the mesh pool. A triangle with a non-finite vertex keeps last frame's point (refits stay valid).
// Also the points' bounds (orderable uints, min xyz then max xyz) for the layer's screen-size LOD; the pool is
// device-local, the layer copies the six words to a host-visible buffer.
// rt_hair_groom_primary (uv_off nonzero): also the point's card UV (soup vertex bytes 16..23, the captured UV0),
// read by the strand G-buffer variant (hair_primary_gbuffer.hlsl); prev_off nonzero: last frame's points are copied
// there first (the velocity variant's previous positions, camera-relative to last frame's camera).
[[vk::binding(0, 0)]] ByteAddressBuffer Soup;
[[vk::binding(1, 0)]] RWByteAddressBuffer Pool;
// soup_tris: the draw's captured triangles; every soup read is bounded by it and by the buffer (fail closed: the point
// keeps last frame's position).
struct Push { uint points, soup_base, stride, tri_off, bary_off, pos_off, bounds_off, uv_off, prev_off, soup_tris, pad1, pad2; };
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
    if (pc.prev_off != 0u) Pool.Store3(pc.prev_off + i * 12u, Pool.Load3(pc.pos_off + i * 12u));
    const uint tri = Pool.Load(pc.tri_off + i * 4u);
    const float2 b = asfloat(Pool.Load2(pc.bary_off + i * 8u));
    float3 v[3];
    uint soup_bytes;
    Soup.GetDimensions(soup_bytes);
    // Bound vertex 2, UV included, before multiplying by stride (which could wrap).
    // No shaderInt64: tri < soup_tris < 2^24 makes only 3 * tri + 2 safe in uint.
    bool ok = tri < pc.soup_tris && pc.soup_tris < (1u << 24) && pc.stride >= 24u && pc.stride <= 256u &&
              pc.soup_base <= soup_bytes;
    if (ok) {
      const uint available = soup_bytes - pc.soup_base;
      ok = available >= 24u;
      if (ok) ok = 3u * tri + 2u <= (available - 24u) / pc.stride;
    }
    [unroll] for (uint k = 0; k < 3; ++k) {
      v[k] = ok ? asfloat(Soup.Load3(pc.soup_base + (3u * tri + k) * pc.stride)) : 0.0.xxx;
      ok = ok && all(abs(v[k]) < 1e30);
    }
    if (ok) {
      const float3 p = v[0] * (1.0 - b.x - b.y) + v[1] * b.x + v[2] * b.y;
      Pool.Store3(pc.pos_off + i * 12u, asuint(p));
      if (pc.uv_off != 0u && pc.stride >= 24u) {
        float2 t[3];
        [unroll] for (uint k = 0; k < 3; ++k) t[k] = asfloat(Soup.Load2(pc.soup_base + (3u * tri + k) * pc.stride + 16u));
        Pool.Store2(pc.uv_off + i * 8u, asuint(t[0] * (1.0 - b.x - b.y) + t[1] * b.x + t[2] * b.y));
      }
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
