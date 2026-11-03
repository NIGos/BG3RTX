// Separate entries preserve the ordinary rr_post SPIR-V byte for byte.
#include "rr_post.hlsl"
[[vk::binding(15, 0)]] Texture2D<float> Depth;
[[vk::binding(16, 0)]] Texture2D<float4> HistoryAC;
[[vk::binding(17, 0)]] Texture2D<float4> HistoryEB;
[[vk::binding(18, 0)]] Texture2D<float4> HistoryMeta;
[[vk::binding(6, 0)]] [[vk::image_format("rgba32f")]] RWTexture2D<float4> HistoryOutAC;
[[vk::binding(14, 0)]] [[vk::image_format("rgba32f")]] RWTexture2D<float4> HistoryOutEB;
[[vk::binding(19, 0)]] [[vk::image_format("rgba32f")]] RWTexture2D<float4> HistoryOutMeta;

// 16 Halton-x phases; 32-bit history avoids fp16 EMA stalling for small deltas.
// Motion is unjittered, in render pixels. History stays on the jittered grid:
// previous = current + scaled MV + previous jitter - current jitter.
[numthreads(8, 8, 1)]
void temporal(uint3 id : SV_DispatchThreadID) {
  if (any(id.xy >= pc.size)) return;
  int2 p = int2(id.xy);
  float4 ac = In0.Load(int3(p, 0)), eb = In1.Load(int3(p, 0));
  float d = Depth.Load(int3(p, 0));
  float2 mv = In2.Load(int3(p, 0)).xy * pc.uvscale;
  float4 loAC = ac, hiAC = ac;
  float3 loB = eb.rgb, hiB = eb.rgb;
  float loD = d, hiD = d;
  [unroll] for (int y = -1; y <= 1; ++y) {
    [unroll] for (int x = -1; x <= 1; ++x) {
      int2 q = clamp(p + int2(x,y), int2(0,0), int2(pc.size)-1);
      float4 a = In0.Load(int3(q,0));
      float3 b = In1.Load(int3(q,0)).rgb;
      loAC = min(loAC,a); hiAC = max(hiAC,a);
      loB = min(loB,b); hiB = max(hiB,b);
      float z = Depth.Load(int3(q,0)); loD = min(loD,z); hiD = max(hiD,z);
    }
  }
  float2 prev = float2(p) + mv + pc.uvmap.xy;
  // Test before conversion: NaN/Inf motion never becomes an integer coordinate.
  bool valid = (pc.flags & 1u) != 0u && all(isfinite(prev)) && isfinite(d) &&
               all(prev >= 0.0) && all(prev <= float2(pc.size)-1.0);
  if (valid) {
    int2 base = int2(floor(prev));
    float2 f = frac(prev);
    float4 ha = 0.0; float3 hb = 0.0; float weight = 0.0;
    [unroll] for (int ty = 0; ty <= 1; ++ty) {
      [unroll] for (int tx = 0; tx <= 1; ++tx) {
        int2 q = min(base + int2(tx,ty), int2(pc.size)-1);
        float4 meta = HistoryMeta.Load(int3(q,0));
        float4 a = HistoryAC.Load(int3(q,0)), b = HistoryEB.Load(int3(q,0));
        // D24 two-rounding error plus the measured 3x3 depth footprint. No relative-z tuning.
        // R16G16 MV rounding (two frames) plus one render-pixel footprint per axis.
        float2 mvTolerance = 1.0 + (abs(mv) + abs(meta.yz)) / 1024.0;
        bool accept = all(isfinite(meta)) && all(isfinite(a)) && all(isfinite(b.rgb)) &&
            meta.w == 1.0 && meta.x >= loD - 2.0/16777215.0 && meta.x <= hiD + 2.0/16777215.0 &&
            all(abs(meta.yz - mv) <= mvTolerance);
        if (accept) {
          float w = (tx ? f.x : 1.0-f.x) * (ty ? f.y : 1.0-f.y);
          ha += a*w; hb += b.rgb*w; weight += w;
        }
      }
    }
    if (weight > 0.0) {
      ac = lerp(clamp(ha/weight, loAC, hiAC), ac, 1.0/16.0);
      eb.rgb = lerp(clamp(hb/weight, loB, hiB), eb.rgb, 1.0/16.0);
    }
  }
  HistoryOutAC[p] = ac;
  HistoryOutEB[p] = eb; // exposure is fresh, never accumulated
  HistoryOutMeta[p] = float4(d, mv, all(isfinite(mv)) && isfinite(d) ? 1.0 : 0.0);
}

[numthreads(8, 8, 1)]
void temporal_final(uint3 id : SV_DispatchThreadID) {
  if (any(id.xy >= pc.size)) return;
  float2 uv = (float2(id.xy)+0.5)/float2(pc.size);
  uv += pc.uvmap.xy / float2(pc.src); // align jittered A/C/B with unjittered RR
  float4 ac = In1.SampleLevel(LinClamp, uv, 0.0);
  float4 e = In2.SampleLevel(LinClamp, uv, 0.0);
  float3 z = finite3(In0.Load(int3(id.xy,0)).rgb);
  OutR11[id.xy] = finite3(bg3_grade(chain_value(z,ac,e)));
}
