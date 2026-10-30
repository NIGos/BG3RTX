// Synthetic EXT traversal plumbing only. Three physical triangles, six rays.
[[vk::binding(0,0)]] RaytracingAccelerationStructure scene;
[[vk::binding(1,0)]] RWStructuredBuffer<uint4> output;
[numthreads(6,1,1)]
void main(uint3 tid:SV_DispatchThreadID) {
  uint i=tid.x,p=i/2;
  float alpha=p==0?1.0:p==1?0.0:(i&1)?.5:.499;
  float cut=p==0?1.0:.5;
  RayDesc ray;ray.Origin=float3(float(p)*4.0+.25,.25,1);
  ray.Direction=float3(0,0,-1);ray.TMin=0;ray.TMax=2;
  RayQuery<RAY_FLAG_NONE> q;q.TraceRayInline(scene,RAY_FLAG_NONE,255,ray);
  uint candidates=0;
  while(q.Proceed())if(q.CandidateType()==CANDIDATE_NON_OPAQUE_TRIANGLE) {
    ++candidates;
    if(alpha>=cut)q.CommitNonOpaqueTriangleHit();
  }
  output[i]=uint4(q.CommittedStatus()==COMMITTED_TRIANGLE_HIT,candidates,alpha>=cut,p);
}
