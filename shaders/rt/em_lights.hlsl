// D-EM: the emissive light list (layer/emissive.cpp), one thread per light, right after
// the emissive TLAS build. A light is a cluster of `k` consecutive triangles of one
// instance (k = 1 while the scene fits rt_em_max_lights; above it, k grows so that no
// triangle is dropped): its area and power are the cluster's sums, its radiance the
// area-weighted mean, and p0/e1/e2 the cluster's largest triangle (its shape for a
// sampler; prim is the cluster's first triangle and flags >> 16 its triangle count, so a
// sampler can pick a triangle of the cluster by area). Per triangle, the radiance is the
// maps averaged at the mip where one texel covers the triangle, the power
// pi * area * luminance (a one-sided Lambertian emitter). Invalid triangles add nothing.
// Positions: the scene's camera-relative world (em_common.hlsli em_shift).
#define EM_SET 0
#define EM_BUILDER 1
#include "em_common.hlsli"

// Builder-only inputs (push descriptors, set 1).
[[vk::binding(0, 1)]] ByteAddressBuffer instances;   // VkAccelerationStructureInstanceKHR, 64 B each
[[vk::binding(1, 1)]] ByteAddressBuffer prefix;      // per instance: first light, lights

struct Push { uint lights; uint instances; uint k; uint pad1; };
[[vk::push_constant]] Push pc;

groupshared float gs_power[256];

bool finite3(float3 v) { return all(abs(v) < 1e30); }

[numthreads(256, 1, 1)]
void main(uint3 id : SV_DispatchThreadID, uint3 gid : SV_GroupID, uint gi : SV_GroupIndex) {
  const uint j = id.x;
  float power = 0.0;
  // The game's global scale buffer is per frame: keep the value this build used.
  if (j == 0 && emHeader.Load(6 * 4) != 0) {
    emHeader.Store(11 * 4, emGlobal.Load(0));
    emHeader.Store(13 * 4, 1u);
  }
  if (j < pc.lights && pc.instances > 0) {
    // The instance whose range holds j (prefix is sorted by first light).
    uint lo = 0, hi = pc.instances - 1;
    while (lo < hi) {
      uint mid = (lo + hi + 1) >> 1;
      if (prefix.Load(mid * 8) <= j) lo = mid; else hi = mid - 1;
    }
    const uint inst = lo;
    const uint2 range = prefix.Load2(inst * 8);
    const uint k = max(pc.k, 1u);
    const uint prim0 = (j - range.x) * k;
    const uint ntri = emInfo.Load(inst * 16 + 12);
    const uint ib = inst * 64;
    float3x4 o2w = float3x4(asfloat(instances.Load4(ib)), asfloat(instances.Load4(ib + 16)), asfloat(instances.Load4(ib + 32)));
    const uint mask = instances.Load(ib + 48) >> 24;
    EmLight l = (EmLight)0;
    l.inst = inst;
    l.prim = prim0;
    l.rec = 0xFFFFFFFFu;
    float area_sum = 0.0, best = -1.0;
    float3 la = float3(0, 0, 0);
    uint count = 0, flags = 0;
    if (j - range.x < range.y && mask != 0) {
      for (uint t = 0; t < k && prim0 + t < ntri; ++t) {
        EmTri tri = em_triangle(inst, prim0 + t, o2w);
        ++count;
        if (!tri.ok) continue;
        float3 e1 = tri.p[1] - tri.p[0], e2 = tri.p[2] - tri.p[0];
        float area = 0.5 * length(cross(e1, e2));
        if (!(finite3(tri.p[0]) && finite3(e1) && finite3(e2) && area > 0.0 && area < 1e8)) continue;
        EmRec r = emRecs[tri.rec];
        float2 du = tri.uv[1] - tri.uv[0], dv = tri.uv[2] - tri.uv[0];
        float uv_area = 0.5 * abs(du.x * dv.y - du.y * dv.x);
        float2 uvc = (tri.uv[0] + tri.uv[1] + tri.uv[2]) / 3.0;
        float3 L = em_radiance(r, uvc, sqrt(uv_area));   // footprint: the whole triangle (its mean)
        float p = 3.14159265 * area * em_lum(L);
        if (!(p >= 0.0 && p < 1e30)) continue;
        power += p;
        area_sum += area;
        la += L * area;
        flags |= r.flags;
        if (area > best) {
          best = area;
          l.rec = tri.rec;
          l.p0 = tri.p[0];
          l.e1 = e1;
          l.e2 = e2;
        }
      }
    }
    l.area = area_sum;
    l.power = power;
    l.radiance = area_sum > 0.0 ? la / area_sum : float3(0, 0, 0);
    l.flags = (flags & 0xFFFFu) | (count << 16);
    emLights[j] = l;
  }
  // Per-group power sums (the host adds them up for the log; consumers may too).
  gs_power[gi] = power;
  GroupMemoryBarrierWithGroupSync();
  [unroll] for (uint s = 128; s > 0; s >>= 1) {
    if (gi < s) gs_power[gi] += gs_power[gi + s];
    GroupMemoryBarrierWithGroupSync();
  }
  if (gi == 0 && gid.x < 1024) emHeader.Store((16 + gid.x) * 4, asuint(gs_power[0]));
}
