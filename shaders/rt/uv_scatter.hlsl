// EN2 (layer/hitmat.cpp): the UV of a G-buffer draw, captured by transform feedback in the
// G-buffer pass (stride 24: clip position, UV at +16), copied into the UV slots of the
// same triangles in the scene's vertex data (stride 24, UV at +16): the depth prepass's
// capture of a dynamic draw (binding 1), or a cached mesh's object-space vertices in the
// split's mesh pool (binding 2). Positions are not touched. One workgroup per job.
[[vk::binding(0, 0)]] ByteAddressBuffer Src;       // this frame's G-buffer UV capture
[[vk::binding(1, 0)]] RWByteAddressBuffer Soup;    // the depth prepass capture
[[vk::binding(2, 0)]] RWByteAddressBuffer Pool;    // the mesh pool
[[vk::binding(3, 0)]] ByteAddressBuffer Jobs;      // 16 B per job: src offset, dst offset, vertices, target

struct Push { uint jobs, pad0, pad1, pad2; };
[[vk::push_constant]] Push pc;

[numthreads(64, 1, 1)]
void main(uint3 gid : SV_GroupID, uint3 tid : SV_GroupThreadID) {
  if (gid.x >= pc.jobs) return;
  uint4 j = Jobs.Load4(gid.x * 16u);
  uint sbytes, dbytes;
  Src.GetDimensions(sbytes);
  if (j.w == 0u) Soup.GetDimensions(dbytes);
  else Pool.GetDimensions(dbytes);
  for (uint v = tid.x; v < j.z; v += 64u) {
    uint s = j.x + v * 24u + 16u, d = j.y + v * 24u + 16u;
    if (s + 8u > sbytes || d + 8u > dbytes) break;   // never outside the buffers (no robust access)
    uint2 uv = Src.Load2(s);
    if (j.w == 0u) Soup.Store2(d, uv);
    else Pool.Store2(d, uv);
  }
}
