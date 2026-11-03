// Shaders of tools/hair_lss_bench.cpp (card any-hit vs LSS groom cost gate). Entry points:
//   trace_opaque  ray queries, everything opaque (LSS groom, or cards without alpha test)
//   trace_alpha   ray queries against non-opaque card triangles: 3 mask samples per candidate
//   deform        moves the card vertices (stand-in for the captured deformed hair)
//   update        groom points = barycentrics . deformed card triangle (the runtime embedding)
// Ray origins are random points on random card triangles: the same rays for every geometry.

[[vk::binding(0)]] RaytracingAccelerationStructure gScene;
[[vk::binding(1)]] RWStructuredBuffer<float> gCardPos;     // deformed card positions, xyz per vertex
[[vk::binding(2)]] StructuredBuffer<uint> gCardTri;        // 3 indices per triangle
[[vk::binding(3)]] StructuredBuffer<float2> gCardUV;
[[vk::binding(4)]] Texture2D<float4> gMask;
[[vk::binding(5)]] SamplerState gSampler;
[[vk::binding(6)]] RWStructuredBuffer<uint> gCounters;     // [0] hits, [1] candidates (device local)
[[vk::binding(7)]] StructuredBuffer<float> gCardRest;
[[vk::binding(8)]] RWStructuredBuffer<float> gGroomPos;
[[vk::binding(9)]] StructuredBuffer<uint> gEmbedTri;
[[vk::binding(10)]] StructuredBuffer<float> gEmbedBary;

struct Push { uint count; uint kind; uint seed; uint triangles; float time; float3 light; };
[[vk::push_constant]] Push pc;

uint hash(uint x) {
  x ^= x >> 16; x *= 0x7feb352du; x ^= x >> 15; x *= 0x846ca68bu; x ^= x >> 16;
  return x;
}
float rnd(inout uint s) { s = hash(s); return float(s >> 8) * (1.0 / 16777216.0); }

float3 card_vertex(uint v) { return float3(gCardPos[3 * v], gCardPos[3 * v + 1], gCardPos[3 * v + 2]); }

RayDesc make_ray(uint id) {
  uint s = hash(id * 9781u + pc.seed);
  uint t = min(uint(rnd(s) * pc.triangles), pc.triangles - 1);
  float3 a = card_vertex(gCardTri[3 * t]), b = card_vertex(gCardTri[3 * t + 1]), c = card_vertex(gCardTri[3 * t + 2]);
  float u = rnd(s), v = rnd(s);
  if (u + v > 1) { u = 1 - u; v = 1 - v; }
  float3 n = normalize(cross(b - a, c - a));
  float z = rnd(s) * 2 - 1, phi = rnd(s) * 6.2831853;
  float3 sphere = float3(sqrt(1 - z * z) * cos(phi), sqrt(1 - z * z) * sin(phi), z);
  RayDesc r;
  r.Origin = a + (b - a) * u + (c - a) * v;
  r.TMin = 1e-3;
  if (pc.kind == 0) {            // shadow: toward the light, small cone
    r.Direction = normalize(pc.light + 0.05 * sphere);
    r.TMax = 10;
  } else if (pc.kind == 1) {     // ambient occlusion: cosine lobe around the card normal, 0.5 m
    float3 d = normalize(n * (dot(sphere, n) < 0 ? -1 : 1) + sphere);
    r.Direction = d;
    r.TMax = 0.5;
  } else {                       // diffuse GI: any direction, closest hit
    r.Direction = sphere;
    r.TMax = 10;
  }
  return r;
}

groupshared uint gsHits;
groupshared uint gsCandidates;

void count(uint gi, bool hit, uint candidates) {
  if (hit) InterlockedAdd(gsHits, 1u);
  if (candidates) InterlockedAdd(gsCandidates, candidates);
  GroupMemoryBarrierWithGroupSync();
  if (gi == 0) {
    InterlockedAdd(gCounters[0], gsHits);
    InterlockedAdd(gCounters[1], gsCandidates);
  }
}

[numthreads(64, 1, 1)]
void trace_opaque(uint id : SV_DispatchThreadID, uint gi : SV_GroupIndex) {
  if (gi == 0) { gsHits = 0; gsCandidates = 0; }
  GroupMemoryBarrierWithGroupSync();
  bool hit = false;
  if (id < pc.count) {
    RayDesc r = make_ray(id);
    if (pc.kind == 2) {
      RayQuery<RAY_FLAG_FORCE_OPAQUE> q;
      q.TraceRayInline(gScene, 0, 0xFF, r);
      while (q.Proceed()) {}
      hit = q.CommittedStatus() != COMMITTED_NOTHING;
    } else {
      RayQuery<RAY_FLAG_FORCE_OPAQUE | RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH> q;
      q.TraceRayInline(gScene, 0, 0xFF, r);
      while (q.Proceed()) {}
      hit = q.CommittedStatus() != COMMITTED_NOTHING;
    }
  }
  count(gi, hit, 0);
}

// The card path's candidate test: coverage plus two more mask fetches (the native recipe reads
// coverage, noise and depth/root); a candidate is committed when coverage >= 0.5.
bool card_candidate(uint prim, float2 bc) {
  uint i0 = gCardTri[3 * prim], i1 = gCardTri[3 * prim + 1], i2 = gCardTri[3 * prim + 2];
  float2 uv = gCardUV[i0] * (1 - bc.x - bc.y) + gCardUV[i1] * bc.x + gCardUV[i2] * bc.y;
  float a = gMask.SampleLevel(gSampler, uv, 0).a;
  float noise = gMask.SampleLevel(gSampler, uv * 3.7, 2).r;
  float root = gMask.SampleLevel(gSampler, uv, 1).b;
  return a + (noise - root) * 1e-6 >= 0.5;
}

[numthreads(64, 1, 1)]
void trace_alpha(uint id : SV_DispatchThreadID, uint gi : SV_GroupIndex) {
  if (gi == 0) { gsHits = 0; gsCandidates = 0; }
  GroupMemoryBarrierWithGroupSync();
  bool hit = false;
  uint candidates = 0;
  if (id < pc.count) {
    RayDesc r = make_ray(id);
    if (pc.kind == 2) {
      RayQuery<0> q;
      q.TraceRayInline(gScene, 0, 0xFF, r);
      while (q.Proceed()) {
        candidates++;
        if (card_candidate(q.CandidatePrimitiveIndex(), q.CandidateTriangleBarycentrics())) q.CommitNonOpaqueTriangleHit();
      }
      hit = q.CommittedStatus() != COMMITTED_NOTHING;
    } else {
      RayQuery<RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH> q;
      q.TraceRayInline(gScene, 0, 0xFF, r);
      while (q.Proceed()) {
        candidates++;
        if (card_candidate(q.CandidatePrimitiveIndex(), q.CandidateTriangleBarycentrics())) q.CommitNonOpaqueTriangleHit();
      }
      hit = q.CommittedStatus() != COMMITTED_NOTHING;
    }
  }
  count(gi, hit, candidates);
}

[numthreads(64, 1, 1)]
void deform(uint v : SV_DispatchThreadID) {
  if (v >= pc.count) return;
  float3 p = float3(gCardRest[3 * v], gCardRest[3 * v + 1], gCardRest[3 * v + 2]);
  float sway = 0.02 * saturate(1.87 - p.y) * sin(pc.time + p.y * 20);
  gCardPos[3 * v] = p.x + sway;
  gCardPos[3 * v + 1] = p.y;
  gCardPos[3 * v + 2] = p.z + 0.5 * sway;
}

[numthreads(64, 1, 1)]
void update(uint i : SV_DispatchThreadID) {
  if (i >= pc.count) return;
  uint t = gEmbedTri[i];
  float3 b = float3(gEmbedBary[3 * i], gEmbedBary[3 * i + 1], gEmbedBary[3 * i + 2]);
  float3 p = card_vertex(gCardTri[3 * t]) * b.x + card_vertex(gCardTri[3 * t + 1]) * b.y + card_vertex(gCardTri[3 * t + 2]) * b.z;
  gGroomPos[3 * i] = p.x;
  gGroomPos[3 * i + 1] = p.y;
  gGroomPos[3 * i + 2] = p.z;
}
