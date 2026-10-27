// D-EM: the emissive light list (layer/emissive.cpp), one thread per triangle of the
// emissive TLAS, right after its build. Triangles are in camera-relative world space;
// each gets its area, its mean radiance (the mask sampled at the centroid at the mip
// where one texel covers the triangle) and its power pi * area * luminance (a one-sided
// Lambertian emitter). Collapsed or invalid triangles get zero power and stay in the
// list, so a triangle's index is stable for the whole build.
#define EM_SET 0
#include "em_common.hlsli"

// Builder-only inputs (push descriptors, set 1).
[[vk::binding(0, 1)]] ByteAddressBuffer instances;   // VkAccelerationStructureInstanceKHR, 64 B each
[[vk::binding(1, 1)]] ByteAddressBuffer prefix;      // per instance: first light, triangles in the list

struct Push { uint lights; uint instances; uint pad0, pad1; };
[[vk::push_constant]] Push pc;

groupshared float gs_power[256];

bool finite3(float3 v) { return all(abs(v) < 1e30); }

[numthreads(256, 1, 1)]
void main(uint3 id : SV_DispatchThreadID, uint3 gid : SV_GroupID, uint gi : SV_GroupIndex) {
  const uint j = id.x;
  float power = 0.0;
  if (j < pc.lights && pc.instances > 0) {
    // The instance whose range holds j (prefix is sorted by first light).
    uint lo = 0, hi = pc.instances - 1;
    while (lo < hi) {
      uint mid = (lo + hi + 1) >> 1;
      if (prefix.Load(mid * 8) <= j) lo = mid; else hi = mid - 1;
    }
    const uint inst = lo;
    const uint2 range = prefix.Load2(inst * 8);
    const uint prim = j - range.x;
    // The instance's object-to-world rows and mask (0: invalid matrix, set at submit).
    const uint ib = inst * 64;
    float3x4 o2w = float3x4(asfloat(instances.Load4(ib)), asfloat(instances.Load4(ib + 16)), asfloat(instances.Load4(ib + 32)));
    const uint mask = instances.Load(ib + 48) >> 24;
    EmLight l = (EmLight)0;
    l.inst = inst;
    l.prim = prim;
    l.rec = 0xFFFFFFFFu;
    if (prim < range.y && mask != 0) {
      EmTri t = em_triangle(inst, prim, o2w);
      l.rec = t.rec;
      float3 e1 = t.p[1] - t.p[0], e2 = t.p[2] - t.p[0];
      float area = 0.5 * length(cross(e1, e2));
      if (t.ok && finite3(t.p[0]) && finite3(e1) && finite3(e2) && area > 0.0 && area < 1e8) {
        EmRec r = emRecs[t.rec];
        float2 du = t.uv[1] - t.uv[0], dv = t.uv[2] - t.uv[0];
        float uv_area = 0.5 * abs(du.x * dv.y - du.y * dv.x);
        float2 uvc = (t.uv[0] + t.uv[1] + t.uv[2]) / 3.0;
        float3 L = em_radiance(r, uvc, em_lod(r, uv_area, area, sqrt(area)));
        l.p0 = t.p[0];
        l.e1 = e1;
        l.e2 = e2;
        l.area = area;
        l.radiance = L;
        power = 3.14159265 * area * em_lum(L);
        power = power >= 0.0 && power < 1e30 ? power : 0.0;
        l.power = power;
        l.flags = r.flags;
      }
    }
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
