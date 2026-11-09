// Scene handout increment 1 fixture shader (tools/scene_handout_gpu_fixture.cpp): reads the bindings of an
// update-after-bind set laid out like the material set 1 (stamp b9, alpha views b2, immutable sampler b4, the OMM
// reference TLAS b10) and writes what it saw. The values tell which descriptors were current at the submit.
[[vk::binding(0, 0)]] RWByteAddressBuffer gOut;
[[vk::binding(9, 1)]] ByteAddressBuffer gStamp;
[[vk::binding(2, 1)]] Texture2D<float> gTex[4096];
[[vk::binding(4, 1)]] SamplerState gWrap;
[[vk::binding(10, 1)]] RaytracingAccelerationStructure gRef;

[numthreads(1, 1, 1)] void main() {
  const uint value = gStamp.Load(0u), index = gStamp.Load(4u);
  gOut.Store(0u, value);
  gOut.Store(4u, asuint(gTex[index].Load(int3(0, 0, 0))));
  RayQuery<RAY_FLAG_NONE> q;
  RayDesc r;
  r.Origin = float3(0.0f, 0.0f, 0.0f);
  r.Direction = float3(0.0f, 0.0f, 1.0f);
  r.TMin = 0.0f;
  r.TMax = 1.0e4f;
  q.TraceRayInline(gRef, 0u, 0xFFu, r);
  while (q.Proceed()) {}
  gOut.Store(8u, q.CommittedStatus() == COMMITTED_NOTHING ? 1u : 0u);   // the empty TLAS: no hit
  gOut.Store(12u, gTex[0].SampleLevel(gWrap, float2(0.5f, 0.5f), 0.0f) >= 0.0f ? 0xC0FFEEu : 0u);
}
