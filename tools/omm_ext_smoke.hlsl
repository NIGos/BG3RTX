// Synthetic EXT traversal plumbing only. Three physical triangles, six rays.
[[vk::binding(0,0)]] RaytracingAccelerationStructure scene;
[[vk::binding(1,0)]] RWStructuredBuffer<uint4> output;
[[vk::binding(2,0)]] Texture2DArray<float4> endpointTexture;
[[vk::binding(3,0)]] SamplerState endpointSampler;
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
[numthreads(64,1,1)]
void endpoints(uint3 tid:SV_DispatchThreadID) {
  uint i=tid.x;if(i>=2176u)return;
  uint layer=i/(64u*17u),uvIndex=(i/17u)%64u,lodIndex=i%17u;
  const float positions[8]={-17.0,-1.125,-.5,0.0,.125,.5,1.0,17.125};
  float2 uv=float2(positions[uvIndex%8u],positions[uvIndex/8u]);
  float alpha=endpointTexture.SampleLevel(endpointSampler,float3(uv,float(layer)),float(lodIndex)/8.0).w;
  output[6u+i]=uint4(asuint(alpha),layer,lodIndex,uvIndex);
}
