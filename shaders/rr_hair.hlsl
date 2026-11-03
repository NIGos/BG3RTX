// Experimental spatial raster-coverage resolve. The G-buffer does not retain
// material alpha: capture records only surviving class-5 samples (including scalp).
// Independent entries keep the ordinary RR guide/post modules unchanged.
[[vk::binding(0, 0)]] Texture2D<float4> ChainInput;
[[vk::binding(1, 0)]] Texture2D<float4> Sharp;
[[vk::binding(2, 0)]] Texture2D<float4> MaterialOrOccupancy;
[[vk::binding(7, 0)]] [[vk::image_format("r11g11b10f")]] RWTexture2D<float3> Result;
struct Push {
  uint2 size, src;
  float4 uvmap, uvclamp;
  float2 uvscale;
  int dof_m0;
  uint flags;
  float4 grade;
  uint2 bmin, bmax;
};
[[vk::push_constant]] Push pc;

// Called with GuidePush: only the common first uint2 (size) is accessed.
[numthreads(8, 8, 1)]
void capture(uint3 id : SV_DispatchThreadID) {
  if (any(id.xy >= pc.size)) return;
  uint cls = uint(round(MaterialOrOccupancy.Load(int3(id.xy, 0)).a * 255.0)) & 7u;
  Result[id.xy] = (cls == 5u ? 1.0 : 0.0).xxx;
}

// The native resolve uses a 4x4 stencil footprint and geometric group means.
// This alternative uses disjoint 4x4 cells: arithmetic coverage weights preserve
// each cell's sharp RGB mean before R11G11B10 quantization. No history, exposure,
// grade or motion-vector changes. Up to 3 render pixels of silhouette softening
// is possible; this is an unqualified spatial candidate, not recovered alpha.
[numthreads(8, 8, 1)]
void hair_resolve(uint3 id : SV_DispatchThreadID) {
  uint2 p = id.xy + pc.bmin;
  if (any(p > pc.bmax)) return;
  uint2 lo = max(p & ~3u, pc.bmin), hi = min((p & ~3u) + 3u, pc.bmax);
  float3 sum = 0.0;
  uint hair = 0u, count = 0u;
  [unroll] for (uint y = 0u; y < 4u; ++y) {
    [unroll] for (uint x = 0u; x < 4u; ++x) {
      uint2 q = lo + uint2(x, y);
      if (all(q <= hi)) {
        float3 color = Sharp.Load(int3(q, 0)).rgb;
        if (any(!isfinite(color))) return; // retain native resolve on invalid input
        sum += color;
        hair += MaterialOrOccupancy.Load(int3(q, 0)).r > 0.5 ? 1u : 0u;
        ++count;
      }
    }
  }
  if (hair != 0u && hair != count) Result[p] = sum / float(count);
}
