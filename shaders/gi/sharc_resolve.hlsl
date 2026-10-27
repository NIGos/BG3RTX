// dxc: -enable-16bit-types
// SHaRC v1 resolve (sharc=1; core/gi.cpp run_pre, before the GI trace queries the cache).
// One thread per cache entry: merges last frame's injections into the cross-frame average
// (SharcResolveEntry: accumulation capped at sharc_frames, eviction after sharc_stale frames
// without a sample), with last injection's camera as SHaRC's previous camera. A camera jump
// of more than sharc_reset_jump metres in one frame (a teleport, a level load) clears every
// entry instead: the old positions belong to another place.
#include "sharc_common.hlsli"

[numthreads(256, 1, 1)]
void main(uint3 gid : SV_GroupID, uint gi : SV_GroupIndex) {
  const uint e = (gid.y * shc.groups_x + gid.x) * 256u + gi;
  if (e >= shc.capacity) return;
  sharc_bin = e >> 8;
  const float3 cam = sharc_cam();
  const float4 prev = asfloat(shState.Load4(0));
  const float3 cprev = prev.w > 0.5 ? prev.xyz : cam;
  const bool was = shHash[e] != 0;
  const float3 dc = cam - cprev;
  if (shc.reset_jump > 0.0 && dot(dc, dc) > shc.reset_jump * shc.reset_jump) {
    if (e == 0u) sharc_stat(kShReset, true);
    shHash[e] = 0;
    shAccum[e] = SharcZeroAccumulationData();
    shResolved[e] = SharcZeroPackedData();
    sharc_stat(kShEvicted, was);
    return;
  }
  SharcResolveParameters rp;
  rp.cameraPositionPrev = cprev;
  rp.accumulationFrameNum = shc.accum_frames;
  rp.responsiveFrameNum = shc.accum_frames;
  rp.staleFrameNumMax = shc.stale_frames;
  rp.frameIndex = shc.frame;
  SharcResolveEntry(e, sharc_params(cam), rp);
  const bool now = shHash[e] != 0;
  sharc_stat(kShLive, now);
  sharc_stat(kShEvicted, was && !now);
}
