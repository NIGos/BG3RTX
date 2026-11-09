// rr_hair_strand_aa (docs/RTX_HAIR_PRIMARY.md 5n, layer/rrpost.cpp): the dither resolve step of the RR-before-post chain
// (7401d785, rr_post.hlsl `resolve`) for frames with on-screen strands (rt_hair_groom_primary).
//
// 1 (strand footprint untagged): the game's resolve undoes the ordered alpha dither of its hair cards: per pixel it mixes
//   the geometric means of the stencil bit-5 and the other pixels of a 4 x 4 window by their share. A swapped strand
//   draw keeps the card pipeline's stencil state, so its tags follow the card's dithered alpha (the prepass clone keeps
//   the native stencil writes) while its colour shows strands: the resolve then mixes by the card's dither pattern.
//   Here every pixel a strand G-buffer fragment ran on this frame (its pixel entry sealed with this frame's tag) reads as
//   untagged: strands, which carry no ordered dither, pass to RR unresolved. Elsewhere the resolve is the game's.
// 2 (+ analytic strand coverage): the strand footprint's pixels become c Lh + (1 - c) Lb. c is the box coverage of the
//   unjittered pixel by the strands that the pixel or its 8 neighbours hit this frame (their pixel entries: segment,
//   parameter, depth), each strand weighted by 1 / P, P the probability that the frame's shared jitter puts one of those
//   9 rays on it (the box coverage of the jitter square by the 9 shifted bands): an unbiased coverage estimate with no
//   per-frame Bernoulli noise once a strand is found (GBAA/phone-wire AA with the geometry from the strand record,
//   docs 5n). Lh: the mean colour of the strand pixels around; Lb: the pixel's own colour when it shows no strand, else
//   the mean of the non-strand pixels of its 5 x 5 window (none: unchanged). Depth, motion and guides stay the pixel's.
//   Math twin: tools/hair_groom/edge_stability.py (box_strand_coverage, found probability), CTest hair_groom_test.
// The strand data come from strand_snapshot, dispatched at the end of the scene's G-buffer pass of the same frame (the
// strand fragments have just sealed the pixel record; at the post chain it no longer holds this frame's seal, he1
// 2026-11-09 f-debug), into one image; strand_resolve reads it (when it is its frame's), the colours and the stencil.
// A separate request: rr_post.hlsl and every other request keep their preprocessed text.
#include "rt/hair_primary_rebase.hlsli"

[[vk::binding(0, 0)]] Texture2D<float4> In0;      // U: the game's input of 7401d785
[[vk::binding(1, 0)]] Texture2D<float4> In1;      // Xs: the sharp colour (ours)
[[vk::binding(4, 0)]] Texture2D<uint4> Stencil;   // the game's stencil view (it reads .y)
[[vk::binding(6, 0)]] [[vk::image_format("rgba16f")]] RWTexture2D<float4> AC;
[[vk::binding(7, 0)]] [[vk::image_format("r11g11b10f")]] RWTexture2D<float3> OutR11;

struct StrandPush {           // layer/rrpost.cpp StrandPush (96 bytes, the pipeline layout's push range)
  row_major float4x4 vp;      // this frame's G-buffer camera-relative jittered view-projection (v * M)
  float3 cam;                 // its camera position
  uint jitter;                // half2: the jitter in render pixels (raster = unjittered + jitter)
  uint bmin, bmax;            // 7401d785 bounds, x | y << 15; bmin bits 30-31: the mode (1, 2)
  uint2 header;               // the hair primary header's device address (common/hair_primary.h)
};
[[vk::push_constant]] StrandPush sp;

static const uint kMarkerCard = 0x40u;          // common/hair_primary.h kMarkerCard
static const uint kKeyMapOffset = 3248u;        // common/hair_primary.h kKeyMapOffset
static const uint kMaxFound = 9u;

float3 finite3(float3 v) { return float3(isfinite(v.x) ? v.x : 0.0, isfinite(v.y) ? v.y : 0.0, isfinite(v.z) ? v.z : 0.0); }
uint64_t sa_addr(uint64_t a) { const uint2 v = vk::RawBufferLoad<uint2>(a, 4); return (uint64_t(v.y) << 32) | uint64_t(v.x); }
uint sa_ld(uint64_t a) { return vk::RawBufferLoad<uint>(a, 4); }
float3 sa_f3(uint64_t a) { return asfloat(vk::RawBufferLoad<uint3>(a, 4)); }
uint2 unpack15(uint v) { return uint2(v & 0x7FFFu, (v >> 15) & 0x7FFFu); }

// ---- the box coverage of a band (twin of edge_stability.box_strand_coverage) ----
// CDF at x of the unit pixel box projected on a unit normal (a, b): a trapezoid of support |a| + |b|.
float box_cdf(float x, float2 n) {
  const float lo = min(abs(n.x), abs(n.y)), hi = max(abs(n.x), abs(n.y));
  const float u = clamp(x, -0.5 * (lo + hi), 0.5 * (lo + hi)) + 0.5 * (lo + hi);
  const float los = max(lo, 1e-6);
  if (u <= lo) return u * u / (2.0 * los * hi);
  if (u <= hi) return (u - 0.5 * lo) / hi;
  return 1.0 - (lo + hi - u) * (lo + hi - u) / (2.0 * los * hi);
}
// Share of the box covered by the band |dot(x - centre, n) - d| < w.
float hp_box_coverage(float d, float w, float2 n) { return saturate(box_cdf(d + w, n) - box_cdf(d - w, n)); }

// P: the box coverage of the jitter square by the union of the bands at d + dot(n, (dx, dy)), dx, dy in -1..1 (the 3 x 3
// rays, each hitting when the jitter's projection falls in its band). Sorted merge of the 9 intervals.
float found_probability(float d, float w, float2 n) {
  float c[9];
  [unroll] for (int i = 0; i < 9; ++i) c[i] = d + n.x * float(i % 3 - 1) + n.y * float(i / 3 - 1);
  [unroll] for (int a = 1; a < 9; ++a)      // insertion sort (9 values)
    [unroll] for (int b = a; b > 0; --b)
      if (c[b] < c[b - 1]) { const float t = c[b]; c[b] = c[b - 1]; c[b - 1] = t; }
  float total = 0.0, lo = c[0] - w, hi = c[0] + w;
  [unroll] for (int k = 1; k < 9; ++k) {
    if (c[k] - w <= hi) { hi = max(hi, c[k] + w); continue; }
    total += box_cdf(hi, n) - box_cdf(lo, n);
    lo = c[k] - w; hi = c[k] + w;
  }
  return saturate(total + box_cdf(hi, n) - box_cdf(lo, n));
}

struct Frame {
  uint64_t rec, pixels, marks, pool;
  uint tag, width, height, grooms, pool_bytes;
  float4 view;
  float3 org;
  bool ok;
};
Frame frame() {
  Frame f = (Frame)0;
  f.rec = (uint64_t(sp.header.y) << 32) | uint64_t(sp.header.x);
  if (f.rec == 0u) return f;
  f.tag = sa_ld(f.rec);
  f.width = sa_ld(f.rec + 4u); f.height = sa_ld(f.rec + 8u);
  f.grooms = min(sa_ld(f.rec + 12u), 64u);   // common/hair_primary.h kMaxGrooms: key map and table reads stay in the header
  f.pool = sa_addr(f.rec + 24u); f.pixels = sa_addr(f.rec + 32u);
  f.view = asfloat(vk::RawBufferLoad<uint4>(f.rec + 40u, 8));
  f.pool_bytes = sa_ld(f.rec + 56u);
  f.marks = f.pixels + uint64_t(f.width) * f.height * hair_primary_pixel_bytes;
  f.ok = f.tag != 0u && f.width != 0u && f.height != 0u && f.pixels != 0u && hp_rebase(f.rec, sp.cam, f.org);
  return f;
}
bool inside(Frame f, int2 q) { return all(q >= 0) && q.x < int(f.width) && q.y < int(f.height); }
// A strand G-buffer fragment ran on q this frame (the first one seals the entry, hit or miss).
bool footprint(Frame f, int2 q, out uint4 e) {
  e = uint4(hair_primary_no_strand, 0u, 0u, 0u);
  if (!f.ok || !inside(f, q)) return false;
  e = vk::RawBufferLoad<uint4>(f.pixels + uint64_t(uint(q.y) * f.width + uint(q.x)) * hair_primary_pixel_bytes, 16);
  return e.w == hp_seal(e.xyz, f.tag);
}
// q shows a strand this frame: accepted by its draw (marker word 1, not a card-mode card) and a valid hit.
bool strand_at(Frame f, int2 q, uint4 e) {
  if (e.x == hair_primary_no_strand || (e.x >> 24) >= f.grooms) return false;
  const uint acc = sa_ld(f.marks + uint64_t(uint(q.y) * f.width + uint(q.x)) * 8u + 4u);
  return (acc >> 8u) == (f.tag & 0xFFFFFFu) && (acc & kMarkerCard) == 0u &&
         sa_ld(f.rec + kKeyMapOffset + (e.x >> 24) * 4u) == (acc & 0xFFu);
}

float2 to_raster(Frame f, float3 p_rel, out bool front) {   // camera-relative point -> raster pixel coordinates
  const float4 c = mul(float4(p_rel, 1.0), sp.vp);
  front = c.w > 1e-6;
  const float2 ndc = c.xy / max(c.w, 1e-6) * float2(1.0, -1.0);
  return (ndc * 0.5 + 0.5) * f.view.zw + f.view.xy;
}

// The analytic coverage of pixel p by the strands found around it (sum of c / P, clamped) and how many of the 3 x 3
// neighbours show a strand.
float strand_coverage(Frame f, int2 p, float4x4 inv, float2 jit, out uint hits) {
  uint keys[kMaxFound];
  uint nk = 0u;
  float cov = 0.0;
  hits = 0u;
  const float2 centre = float2(p) + 0.5 + jit;   // the unjittered pixel centre, in raster coordinates
  [unroll] for (int i = 0; i < 9; ++i) {
    const int2 q = p + int2(i % 3 - 1, i / 3 - 1);
    uint4 e;
    if (!footprint(f, q, e) || !strand_at(f, q, e)) continue;
    ++hits;
    bool seen = false;
    [unroll] for (uint s = 0u; s < kMaxFound; ++s) seen = seen || (s < nk && keys[s] == e.x);
    if (seen || nk >= kMaxFound) continue;
    keys[nk++] = e.x;
    const uint4 row = vk::RawBufferLoad<uint4>(f.rec + hair_primary_table_offset + (e.x >> 24) * 16u, 16);
    uint i0;
    if (!hp_segment(f.pool, f.pool_bytes, row, e.x & hair_primary_max_segment, i0)) continue;
    const float3 p0 = sa_f3(hp_at(f.pool, row.x, i0, 12u)), p1 = sa_f3(hp_at(f.pool, row.x, i0 + 1u, 12u));
    // The strand radius from q's own hit: its surface point (q's ray at the stored depth) to the axis point at v.
    const float2 ndc = ((float2(q) + 0.5 - f.view.xy) / f.view.zw * 2.0 - 1.0) * float2(1.0, -1.0);
    const float4 hh = mul(float4(ndc, asfloat(e.z), 1.0), inv);
    const float3 axis = lerp(p0, p1, asfloat(e.y));
    const float r = clamp(length(hh.xyz / hh.w + f.org - axis), 1e-6, hair_primary_max_radius);
    bool f0, f1, f2;
    const float2 s0 = to_raster(f, p0 - f.org, f0), s1 = to_raster(f, p1 - f.org, f1);
    const float2 ds = s1 - s0;
    const float len = length(ds);
    if (!f0 || !f1 || !(len > 1e-6)) continue;
    const float2 t2 = ds / len, n2 = float2(-t2.y, t2.x);
    const float tt = saturate(dot(centre - s0, t2) / len);
    const float3 a3 = lerp(p0, p1, tt);
    const float3 b = normalize(cross(p1 - p0, a3 - f.org));
    const float w = length(to_raster(f, a3 + b * r - f.org, f2) - to_raster(f, a3 - f.org, f0));
    if (!f2 || !(w > 0.0)) continue;
    const float d = dot(centre - s0, n2);
    const float along = saturate(box_cdf(dot(s1 - centre, t2), t2) - box_cdf(dot(s0 - centre, t2), t2));
    // The found probability: the 3 x 3 rays at the raster points q + 0.5 = unjittered q centres + jit.
    const float pf = found_probability(d, w, n2);
    cov += hp_box_coverage(d, w, n2) * along / max(pf, 1e-3);
  }
  return saturate(cov);
}

// ---- snapshot (he1 2026-11-09 02:04: at the post chain the pixel record no longer carried this frame's seal: the
// f-debug Xr showed no strand footprint on any pixel, header usable). Dispatched at the scene G-buffer's end of the same
// frame (layer/rrpost.cpp rrpost_pass_end -> strand_snapshot): per pixel (c, strand pixel, strand
// footprint, 0) into the snapshot image that the resolve reads. bmax = the render size - 1; header and camera set.
// bmin bits 0-1: the hook site (0 G-buffer end, 1 decal end, 2 lighting); bit 2: merge (a later site of the same frame:
// write only the pixels where this site finds a strand footprint, keep the earlier site's elsewhere).
// Counters (he1 03:38: the G-buffer-end snapshot changed no hair pixel): per site 4 words, footprint, strand and
// coverage pixels and dispatches, accumulated over a period in a device-local buffer, reduced per workgroup
// (layer/rrpost.cpp copies them to the host after the period; host_atomics_lint_test).
[[vk::binding(8, 0)]] [[vk::image_format("rgba16f")]] RWTexture2D<float4> Out16;
[[vk::binding(12, 0)]] RWStructuredBuffer<uint> Counters;
groupshared uint gsCount[3];
[numthreads(8, 8, 1)]
void strand_snapshot(uint3 id : SV_DispatchThreadID, uint gi : SV_GroupIndex, uint3 gid : SV_GroupID) {
  const uint site = min(sp.bmin & 3u, 2u);
  const bool merge = (sp.bmin & 4u) != 0u;
  if (gi < 3u) gsCount[gi] = 0u;
  GroupMemoryBarrierWithGroupSync();
  const uint2 size = unpack15(sp.bmax) + 1u;
  if (all(id.xy < size)) {
    const int2 p = int2(id.xy);
    const Frame f = frame();
    uint4 e;
    float4 s = 0.0;
    if (footprint(f, p, e)) {
      uint hits;
      const float c = strand_coverage(f, p, hp_inverse4(sp.vp), f16tof32(uint2(sp.jitter & 0xFFFFu, sp.jitter >> 16)), hits);
      s = float4(hits > 0u ? c : 0.0, strand_at(f, p, e) ? 1.0 : 0.0, 1.0, 0.0);
      InterlockedAdd(gsCount[0], 1u);
      if (s.y != 0.0) InterlockedAdd(gsCount[1], 1u);
      if (s.x > 0.0) InterlockedAdd(gsCount[2], 1u);
    }
    if (!merge || s.z != 0.0) Out16[id.xy] = s;
  }
  GroupMemoryBarrierWithGroupSync();
  if (gi < 3u && gsCount[gi] != 0u) InterlockedAdd(Counters[site * 4u + gi], gsCount[gi]);
  if (gi == 0u && all(gid.xy == 0u)) InterlockedAdd(Counters[site * 4u + 3u], 1u);
}

// ---- the resolve, at the post chain: the snapshot in In2 (x coverage, y strand pixel, z footprint) ----
// Read only when it is this frame's: PairFlags (binding 12 of this entry: the host-visible pairing flags, layer/rrpost.cpp
// g_strand_flags; read only, no atomics) word sp.jitter holds this resolve's token (in sp.vp[0][0]) when, at
// vkQueueSubmit, this frame's snapshot was the one submitted last (common/strand_snapshot.h Pairing); else no snapshot:
// exactly the game's resolve.
[[vk::binding(2, 0)]] Texture2D<float4> In2;
[[vk::binding(12, 0)]] StructuredBuffer<uint> PairFlags;
static bool g_own;
float4 snap_at(int2 q) { return g_own ? In2.Load(int3(q, 0)) : 0.0.xxxx; }
float4 snap(int2 q, uint2 bmin, uint2 bmax) { return snap_at(clamp(q, int2(bmin), int2(bmax))); }

[numthreads(16, 16, 1)]
void strand_resolve(uint3 id : SV_DispatchThreadID) {
  const uint2 bmin = unpack15(sp.bmin), bmax = unpack15(sp.bmax);
  const uint mode = sp.bmin >> 30;
  const uint2 p = id.xy + bmin;
  if (!(all(p >= bmin) && all(p <= bmax))) return;
  g_own = PairFlags[sp.jitter] == asuint(sp.vp[0][0]);
  const float3 xc = In1.Load(int3(p, 0)).rgb;
  const float4 u = In0.Load(int3(p, 0));
  const float4 ac = AC[p];
  AC[p] = float4(finite3(u.rgb - ac.a * xc), ac.a);
  const float4 me = snap(int2(p), bmin, bmax);

  // ---- the game's resolve (rr_post.hlsl resolve, line for line) with the strand footprint untagged ----
  float3 xr = xc;
  const uint sc = Stencil.Load(int3(p, 0)).y;
  const bool self5 = ((sc >> 5u) & 1u) != 0u && me.z == 0.0;
  if (((sc >> 4u) & 1u) == 0u) {
    uint n0 = 0u, n5 = 0u;
    float4 acc0 = float4(1.0, 1.0, 1.0, 0.0), acc1 = float4(1.0, 1.0, 1.0, 0.0);
    float3 log0 = 0.0, log1 = 0.0;
    bool3 zero0 = false, zero1 = false;
    [unroll] for (int i = 0; i < 4; ++i) {
      [unroll] for (int j = 0; j < 4; ++j) {
        uint2 q = uint2(int2(p) + int2(i - 1, j - 1));
        q = clamp(q, bmin, bmax);
        const uint s = Stencil.Load(int3(q, 0)).y;
        n0 += 1u - ((s >> 4u) & 1u);
        const uint b5 = ((s >> 5u) & 1u) != 0u && snap_at(int2(q)).z == 0.0 ? 1u : 0u;
        n5 += b5;
        if ((i != 3 && j != 3) || acc1.w == 0.0 || acc0.w == 0.0) {
          const float3 col = In1.Load(int3(q, 0)).rgb;
          const float3 logs = log2(select(col == 0.0, 1.0.xxx, abs(col)));
          if (b5 != 0u) { acc1 = float4(acc1.rgb * col, acc1.w + 1.0); log1 += logs; zero1 |= col == 0.0; }
          else { acc0 = float4(acc0.rgb * col, acc0.w + 1.0); log0 += logs; zero0 |= col == 0.0; }
        }
      }
    }
    if (acc1.w != 0.0 && acc0.w != 0.0) {
      const float3 o1 = pow(abs(acc1.rgb), 1.0 / acc1.w), s1 = exp2(log1 / acc1.w);
      const float3 o0 = pow(abs(acc0.rgb), 1.0 / acc0.w), s0 = exp2(log0 / acc0.w);
      const float3 g1 = select(zero1, 0.0.xxx, select(isfinite(acc1.rgb) & (acc1.rgb != 0.0), o1, s1));
      const float3 g0 = select(zero0, 0.0.xxx, select(isfinite(acc0.rgb) & (acc0.rgb != 0.0), o0, s0));
      const float r = float(n5) / float(n0);
      float3 a1, a0;
      if (self5) { a1 = lerp(g1, xc, r); a0 = g0; }
      else { a1 = g1; a0 = lerp(g0, xc, r); }
      xr = lerp(a0, a1, r);
    }
  }

  // ---- mode 2: the analytic strand coverage on the strand footprint ----
  if (mode >= 2u && me.z != 0.0) {
    float3 lh = 0.0.xxx, lb = 0.0.xxx;
    uint hits = 0u, nb = 0u;
    [unroll] for (int k = 0; k < 25; ++k) {
      const int2 q = int2(p) + int2(k % 5 - 2, k / 5 - 2);
      if (any(q < int2(bmin)) || any(q > int2(bmax))) continue;
      const float4 sq = snap_at(q);
      const float3 col = In1.Load(int3(q, 0)).rgb;
      if (sq.y != 0.0) {
        if (abs(k % 5 - 2) <= 1 && abs(k / 5 - 2) <= 1) { lh += col; ++hits; }   // strand colours of the 3 x 3
      } else {
        lb += col; ++nb;                                                         // background colours of the 5 x 5
      }
    }
    lh /= max(float(hits), 1.0);
    lb = me.y == 0.0 ? xc : lb / max(float(nb), 1.0);
    if (me.y == 0.0) nb = 1u;
    if (hits > 0u && nb > 0u) xr = me.x * lh + (1.0 - me.x) * lb;
    if (mode == 3u)   // debug view: red footprint, green strand pixel, blue the applied coverage
      xr = float3(1.0, me.y, hits > 0u && nb > 0u ? me.x : 0.0) * max(dot(xc, 0.333), 0.05) * 4.0;
  } else if (mode == 3u) {
    xr = xc * 0.25;
  }
  OutR11[p] = finite3(xr);
}