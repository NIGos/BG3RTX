// One workgroup per ordinary held draw. Pack only converted position/w and UV;
// native Hair and Tileset recipes retain their complete captured stride.
[[vk::binding(0,0)]] ByteAddressBuffer Soup;
[[vk::binding(1,0)]] RWByteAddressBuffer Pool;
[[vk::binding(2,0)]] ByteAddressBuffer Jobs;
struct Push { uint jobs_base, jobs_count, groups_x, pad; };
[[vk::push_constant]] Push pc;
[numthreads(64,1,1)]
void main(uint3 group : SV_GroupID, uint thread : SV_GroupIndex) {
  uint index = group.x + group.y * pc.groups_x;
  if (index >= pc.jobs_count) return;
  uint4 job = Jobs.Load4(pc.jobs_base + index * 16u);
  for (uint vertex = thread; vertex < job.z; vertex += 64u) {
    uint source = job.x + vertex * job.w, destination = job.y + vertex * 24u;
    Pool.Store4(destination, Soup.Load4(source));
    Pool.Store2(destination + 16u, Soup.Load2(source + 16u));
  }
}
