// DLSS Ray Reconstruction at the start of post-processing (docs/AUDIT_SOTA.md #1,
// docs/TRACK_A_RR.md "RR before post").
//
// BG3 runs its post chain at render resolution and hands DLSS the *graded*
// image (FRAME_MAP step 14-15):
//   X  scene colour after compose, transparents and c1fb2881 (pre-exposed HDR)
//   -> god rays a7466e4e (+ a low-frequency radial blur term)
//   -> depth of field (12 passes, composite 96bb806d: mix(sharp, blurred, w))
//   -> 7401d785 (stencil-tagged dither resolve: 4x4 geometric means)
//   -> exposure 2cdd9f08 (x * b0[1] * b1[0])
//   -> bloom composite 0463a527 (+ 0.2 * bloom)
//   -> grade 0592680b (HSV adjust, PQ encode, 32^3 LUT)  = DLSS input.
// With rr_post=1 RR gets X (dither-resolved) instead, and the chain is carried
// to the output resolution as a per-pixel affine map of the sharp colour:
//   chain(X) = A * X + C.
// The layer (layer/rrpost.cpp) records one entry of this file at each step, in
// the game's command buffer, at render resolution:
//   start      at the first post step: Xs = X, A = 1, C = 0
//   dof        at the DoF composite's end: A *= (1 - w_far)(1 - w_near), the
//              composite's own weights (96bb806d), from the game's CoC images
//   resolve    before 7401d785: Xr = the game's dither resolve of Xs (RR's
//              colour input), and C = U - A * Xs with U the game's own chain
//              value there (god rays and the blurred DoF layers, exactly)
//   exposure   at 2cdd9f08's end: A, C *= b0[1] * b1[0] (the game's buffers)
//   bloom      at 0463a527's end: C += 0.2 * bloom
// and after RR, at output resolution:
//   final      out = grade(A * RR + C), A and C bilinear from render resolution
// The grade is the game's own: its UBO (4 floats) and LUT, bound live, so
// per-area grades follow. `check` evaluates the same at render resolution on
// Xr, for comparison with the game's grade output in a trace.

[[vk::binding(0, 0)]] Texture2D<float4> In0;
[[vk::binding(1, 0)]] Texture2D<float4> In1;
[[vk::binding(2, 0)]] Texture2D<float4> In2;
[[vk::binding(3, 0)]] Texture3D<float4> Lut;      // the grade's 32^3 LUT (game image)
[[vk::binding(4, 0)]] Texture2D<uint4> Stencil;   // the game's stencil view (it reads .y)
[[vk::binding(5, 0)]] SamplerState LinClamp;
[[vk::binding(6, 0)]] [[vk::image_format("rgba16f")]] RWTexture2D<float4> AC;      // rgb C, a A
[[vk::binding(7, 0)]] [[vk::image_format("r11g11b10f")]] RWTexture2D<float3> OutR11;
[[vk::binding(8, 0)]] [[vk::image_format("rgba16f")]] RWTexture2D<float4> Out16;
[[vk::binding(9, 0)]] StructuredBuffer<float> BufA;
[[vk::binding(10, 0)]] StructuredBuffer<float> BufB;
[[vk::binding(11, 0)]] StructuredBuffer<float> BufC;
[[vk::binding(12, 0)]] RWStructuredBuffer<float> Probe;

struct Push {
  uint2 size;        // dispatch domain
  uint2 src;         // render resolution (final: size of AC)
  float4 uvmap;      // fullscreen VS: uv = uv01 * m0 + m1 (set 0 b4 _m0, _m1)
  float4 uvclamp;    // texture uv clamp (set 0 b4 _m11, _m12)
  float2 uvscale;    // set 0 b4 _m15
  int dof_m0;        // DoF composite set 1 b2 _m0
  uint flags;        // bit 0: clamp negatives in final
  float4 grade;      // grade UBO _m0.._m3
  uint2 bmin, bmax;  // 7401d785 bounds (set 0 b4 _m3, _m4)
};
[[vk::push_constant]] Push pc;

float3 finite3(float3 v) { return float3(isfinite(v.x) ? v.x : 0.0, isfinite(v.y) ? v.y : 0.0, isfinite(v.z) ? v.z : 0.0); }

// The game's uv for render pixel p of a fullscreen pass, mapped into a texture
// the way its fragment shaders do: clamp(uv * m15, m11, m12).
float2 game_uv(uint2 p) {
  float2 uv01 = (float2(p) + 0.5) / float2(pc.size);
  float2 uv = uv01 * pc.uvmap.xy + pc.uvmap.zw;
  return clamp(uv * pc.uvscale, pc.uvclamp.xy, pc.uvclamp.zw);
}

// ---- 0592680b: HSV adjust, PQ encode, 3D LUT (line by line) ----
float3 bg3_grade(float3 c) {
  float b = c.z, g = c.y, r = c.x;
  float4 t76 = lerp(float4(b, g, -1.0, 0.666666686534881591796875), float4(g, b, 0.0, -0.3333333432674407958984375),
                    step(b, g).xxxx);
  float t78 = t76.x;
  float4 t86 = lerp(float4(t78, t76.y, t76.w, r), float4(r, t76.y, t76.z, t78), step(t78, r).xxxx);
  float t87 = t86.x, t88 = t86.w, t89 = t86.y;
  float t91 = t87 - min(t88, t89);
  float h = abs(t86.z + ((t88 - t89) / mad(6.0, t91, 1.0000000133514319600180897396058e-10))) + pc.grade.y;
  float3 rgb = clamp(abs((frac(h.xxx + float3(1.0, 0.666666686534881591796875, 0.3333333432674407958984375)) * 6.0) - 3.0) - 1.0,
                     0.0, 1.0);
  float3 t117 = lerp(1.0.xxx, rgb, lerp(0.0, t91 / (t87 + 1.0000000133514319600180897396058e-10), pc.grade.x).xxx) * t87;
  float3 adj = pc.grade.z < 0.0 ? t117 * (1.0 + pc.grade.z) : lerp(t117, 1.0.xxx, pc.grade.z);
  float3 t131 = lerp(c, adj, pc.grade.w) * 0.0045045041479170322418212890625;
  float3 t135 = select(t131 == 0.0, 0.0, pow(abs(t131), 0.1593017578125));
  float3 coord = clamp(pow(abs((0.8359375 + t135 * 18.8515625) / (1.0 + t135 * 18.6875)), 78.84375), 0.0, 1.0);
  return Lut.SampleLevel(LinClamp, coord, 0.0).xyz;
}

// start: X (In0, the game's chain input) -> Xs; A = 1, C = 0.
[numthreads(8, 8, 1)]
void start(uint3 id : SV_DispatchThreadID) {
  if (any(id.xy >= pc.size)) return;
  OutR11[id.xy] = In0.Load(int3(id.xy, 0)).rgb;
  AC[id.xy] = float4(0.0, 0.0, 0.0, 1.0);
}

// dof: the composite's blend weights (96bb806d). In0 = full-res CoC (set 1 b5,
// texelFetch), In1 = near-field alpha (set 1 b4, sampled at the game's uv).
[numthreads(8, 8, 1)]
void dof(uint3 id : SV_DispatchThreadID) {
  if (any(id.xy >= pc.size)) return;
  float coc = saturate(In0.Load(int3(id.xy, 0)).x);
  float wf = 0.0;
  if (coc > 0.0) wf = saturate(((1600.0 / float(pc.dof_m0 * pc.dof_m0)) * coc) * coc);
  float na = In1.SampleLevel(LinClamp, game_uv(id.xy), 0.0).x;
  float a233 = saturate(na);
  float wn = a233 > 0.0 ? saturate(a233 * 5.0) : 0.0;
  float4 ac = AC[id.xy];
  ac.a *= (1.0 - wf) * (1.0 - wn);
  AC[id.xy] = ac;
}

// resolve: 7401d78564eb8bac ported line by line (16x16 groups like the game's,
// so its border clamp quirk is the same). In0 = U (the game's input of 7401,
// i.e. the chain value after god rays and DoF), In1 = Xs (ours, GENERAL).
// Writes Xr (OutR11) and C = U - A * Xs.
uint2 tap(uint2 p, int dx, int dy) {
  uint2 q = uint2(int2(p) + int2(dx, dy));  // wraps below 0 like the game's uint math
  return clamp(q, pc.bmin, pc.bmax);
}
[numthreads(16, 16, 1)]
void resolve(uint3 id : SV_DispatchThreadID) {
  uint2 p = id.xy + pc.bmin;
  if (!(all(p >= pc.bmin) && all(p <= pc.bmax))) return;
  float3 xc = In1.Load(int3(p, 0)).rgb;
  float4 u = In0.Load(int3(p, 0));
  float4 ac = AC[p];
  AC[p] = float4(finite3(u.rgb - ac.a * xc), ac.a);

  uint sc = Stencil.Load(int3(p, 0)).y;
  if (((sc >> 4u) & 1u) != 0u) { OutR11[p] = xc; return; }
  uint n0 = 0u, n5 = 0u;
  float4 acc0 = float4(1.0, 1.0, 1.0, 0.0);   // stencil bit 5 clear
  float4 acc1 = float4(1.0, 1.0, 1.0, 0.0);   // stencil bit 5 set
  [unroll] for (int i = 0; i < 4; ++i) {
    [unroll] for (int j = 0; j < 4; ++j) {
      uint2 q = tap(p, i - 1, j - 1);
      uint s = Stencil.Load(int3(q, 0)).y;
      n0 += 1u - ((s >> 4u) & 1u);
      uint b5 = (s >> 5u) & 1u;
      n5 += b5;
      if ((i != 3 && j != 3) || acc1.w == 0.0 || acc0.w == 0.0) {
        float3 col = In1.Load(int3(q, 0)).rgb;
        if (b5 != 0u) acc1 = float4(acc1.rgb * col, acc1.w + 1.0);
        else acc0 = float4(acc0.rgb * col, acc0.w + 1.0);
      }
    }
  }
  if (acc1.w == 0.0 || acc0.w == 0.0) { OutR11[p] = xc; return; }
  float3 g1 = pow(abs(acc1.rgb), 1.0 / acc1.w);
  float3 g0 = pow(abs(acc0.rgb), 1.0 / acc0.w);
  float r = float(n5) / float(n0);
  float3 a1, a0;
  if (((sc >> 5u) & 1u) == 1u) { a1 = lerp(g1, xc, r); a0 = g0; }
  else { a1 = g1; a0 = lerp(g0, xc, r); }
  OutR11[p] = finite3(lerp(a0, a1, r));
}

// exposure: 2cdd9f08 multiplies by b0[1] * b1[0] (BufA = its set 1 b0, BufB = b1).
[numthreads(8, 8, 1)]
void exposure(uint3 id : SV_DispatchThreadID) {
  if (any(id.xy >= pc.size)) return;
  float k = BufA[1] * BufB[0];
  float4 ac = AC[id.xy];
  AC[id.xy] = float4(finite3(ac.rgb * k), isfinite(ac.a * k) ? ac.a * k : 0.0);
}

// bloom: 0463a527 adds 0.2 * bloom (In0 = its set 1 b2) at the game's uv.
[numthreads(8, 8, 1)]
void bloom(uint3 id : SV_DispatchThreadID) {
  if (any(id.xy >= pc.size)) return;
  float3 b = In0.SampleLevel(LinClamp, game_uv(id.xy), 0.0).rgb;
  float4 ac = AC[id.xy];
  AC[id.xy] = float4(finite3(ac.rgb + b * 0.2), ac.a);
}

// final (output resolution): In0 = RR output (ours, GENERAL), In1 = AC (GENERAL,
// bilinear), the game's LUT; writes the game's DLSS output image.
[numthreads(8, 8, 1)]
void final(uint3 id : SV_DispatchThreadID) {
  if (any(id.xy >= pc.size)) return;
  float2 uv = (float2(id.xy) + 0.5) / float2(pc.size);
  float4 ac = In1.SampleLevel(LinClamp, uv, 0.0);
  float3 z = finite3(In0.Load(int3(id.xy, 0)).rgb);
  float3 v = finite3(ac.a * z + ac.rgb);
  v = max(v, 0.0);  // the game's grade input is unsigned (R11G11B10)
  OutR11[id.xy] = finite3(bg3_grade(v));
}

// check (render resolution): grade(A * Xr + C) for comparison with the game's
// own grade output in a trace. In0 = Xr, In1 = AC.
[numthreads(8, 8, 1)]
void check(uint3 id : SV_DispatchThreadID) {
  if (any(id.xy >= pc.size)) return;
  float4 ac = In1.Load(int3(id.xy, 0));
  float3 v = max(finite3(ac.a * In0.Load(int3(id.xy, 0)).rgb + ac.rgb), 0.0);
  Out16[id.xy] = float4(bg3_grade(v), 1.0);
}

// probe: copies the exposure values the game uses into a host-visible buffer.
// BufA/BufB = 2cdd9f08's set 1 b0/b1, BufC = the lighting's set 1 b13.
[numthreads(1, 1, 1)]
void probe(uint3 id : SV_DispatchThreadID) {
  [unroll] for (uint i = 0; i < 6; ++i) {
    Probe[i] = BufA[i];
    Probe[6 + i] = BufB[i];
  }
  [unroll] for (uint j = 0; j < 4; ++j) Probe[12 + j] = BufC[j];
}
