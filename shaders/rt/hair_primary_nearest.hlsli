// rt_hair_groom_nearest (docs/RTX_HAIR_PRIMARY.md 5u; common/shader_variant.h kSpecIdHairGroomNearest): the closest strand
// of the accepted groom rows along one pixel ray, in one non-opaque query whose candidates are filtered as
// rt_groom_blocks walks them (rt/rt_common.hlsli): a candidate of a row the caller does not accept is passed over, an
// accepted one is committed (the traversal then reports only closer candidates, so the last commit is the closest).
// Replaces hp_trace's restart loop, which gives up after kHpSkips strands of other rows in front (a dense card-kept
// groom in front of a swapped one left its pixels without a strand: hs1 125217). Accept: a struct with
// `bool ok(uint instance)`. skipped: a candidate of another row was passed over (the mask-hole diagnostic).
// Shared by shaders/rt/hair_primary_gbuffer.hlsl and tools/hair_primary_gpu_check.hlsl (HP_CHECK_NEAREST).
#ifndef HAIR_PRIMARY_NEAREST_HLSLI
#define HAIR_PRIMARY_NEAREST_HLSLI
template <typename Accept>
bool hp_nearest(RaytracingAccelerationStructure scene, RayDesc r, uint mask, Accept acc, out uint k, out uint prim, out float t,
                out bool skipped) {
  k = 0u;
  prim = 0u;
  t = 0.0;
  skipped = false;
  RayQuery<RAY_FLAG_FORCE_NON_OPAQUE> q;
  q.TraceRayInline(scene, 0u, mask, r);
  while (q.Proceed()) {
    // LSS hits come with their own candidate type (not triangle, RTX 5090 2026-11-10: the GPU check saw only it), with
    // CandidateTriangleRayT and committed like a triangle hit; only a procedural (AABB) candidate would need its own t.
    if (q.CandidateType() == CANDIDATE_PROCEDURAL_PRIMITIVE) continue;
    if (!acc.ok(q.CandidateInstanceIndex())) { skipped = true; continue; }
    q.CommitNonOpaqueTriangleHit();
  }
  if (q.CommittedStatus() == COMMITTED_NOTHING) return false;
  k = q.CommittedInstanceIndex();
  prim = q.CommittedPrimitiveIndex();
  t = q.CommittedRayT();
  return true;
}
#endif
