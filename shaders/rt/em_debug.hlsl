// D-EM debug views, rt_debug 16-20 (modes 6-10 here), and the calibration's projected check (mode 0)
// (core/emissive.cpp). One primary ray per pixel against
// the emissive TLAS (rt/em_common.hlsli): a hit within rt_em_tol of the pixel's depth is
// a visible emissive surface, and its radiance is evaluated from the draw's record. The
// game's effects target at the same pixel is the reference: both are in the effects
// target's (pre-exposed) units. The scene is the previous submitted frame's, moved into
// this frame's space by em_shift (exact for static emitters).
//
// Mode 0 runs on one interleaved slice of the pixels (pc.grid / pc.origin: every grid.x-th
// column and grid.y-th row from origin; rt_em_check_slices), the views on all of them.
//
// Statistics (words): 0 pixels with depth, 1 visible emissive hits, 2 effects-
// target pixels brighter than thr with no visible hit, 3 hits where the game shows less
// than 2% of ours, 4 of the bright pixels without a hit at the depth, those with an emissive
// surface in front of it (alpha-blended emitters: not in the depth buffer), 5 such front hits
// in all, 6 ran, 7 effects-target pixels brighter than thr; from 16, 16 words per record:
// ours and game luminance x 4096 as 64-bit sums (lo, hi; dim emitters, e.g. a lava pool at
// 0.01, are kept), pixels.
// They are reduced per workgroup (groupshared) and per wave (per record) into emDev, a
// device-local buffer of the core; `flush` then adds emDev into the host's emStats (plain
// loads and stores of the non-zero words only). Atomics on the host-visible emStats crossed
// PCIe for every pixel: about 140 ms per check at S6 Adamantine Forge (inferred, 2026-11-01).
#include "em_common.hlsli"

struct CameraCB {
  float4 v0, v1, v2;
  float4 m6_r0, m6_r1, m6_r2, m6_r3;
  float4 ray0, ray1, ray2;   // frustum rays at the viewport's (0,0), (1,0), (0,1) in ab space
};
struct ViewportCB { float2 scale; float2 bias; };
struct Camera2 { float4 v[14]; };   // the lighting's set 0 b2: v[13].xyz = absolute camera position (_m6, byte 208)
[[vk::binding(0, 0)]] ConstantBuffer<CameraCB> cam;
[[vk::binding(1, 0)]] ConstantBuffer<ViewportCB> vp;
[[vk::binding(2, 0)]] Texture2D<float> gDepth;   // R32F linear depth
[[vk::binding(3, 0)]] [[vk::image_format("r11g11b10f")]] RWTexture2D<float3> gDiffuse;
[[vk::binding(4, 0)]] [[vk::image_format("r11g11b10f")]] RWTexture2D<float3> gSpecular;
[[vk::binding(5, 0)]] Texture2D<float4> gFx;     // the game's effects target
[[vk::binding(6, 0)]] ConstantBuffer<Camera2> cam2;
[[vk::binding(7, 0)]] RWByteAddressBuffer emDev;  // device-local statistics (core/emissive.cpp g_dev), emStats' layout

// fam: rt_em_dbg_fam; grid / origin: the slice (x | y << 16), 1 | 1 << 16 and 0 for every pixel.
struct Push { uint2 full; uint mode; float tol; float thr; uint has_cam2; float pad_f0; float pad_f1; uint fam; uint overlap_probe; uint grid; uint origin; };
[[vk::push_constant]] Push pc;

static const uint kRecs = 1024;
static const uint kStatWords = 16 + kRecs * 16;

groupshared uint gsStat[16];

float3 ray_at(float2 uv) {
  float2 ab = ((uv - vp.bias) / vp.scale) * 0.5;
  return cam.ray0.xyz * (1.0 - ab.x - ab.y) + cam.ray1.xyz * ab.x + cam.ray2.xyz * ab.y;
}

void stat(uint word) { InterlockedAdd(gsStat[word], 1u); }
void stat_n(uint word, uint n) { InterlockedAdd(gsStat[word], n); }
// Observe coincident SAME-FAMILY draws, without changing nearest-hit emission.
// Distinct geometry IDs avoid counting the two triangles of one rasterized
// polygon as two draws. Eight is diagnostic storage only: overflow is explicit.
void overlap_probe(float3 direction, float distance, float eps, float3 shift, uint nearest) {
  if (pc.overlap_probe == 0u || nearest >= emHeader.Load(3 * 4) || emRecs[nearest].fs_hi != pc.fam) return;
  stat(8);
  RayDesc ray;
  ray.Origin = shift; ray.Direction = direction;
  ray.TMin = max(distance - eps, 0.0); ray.TMax = distance + eps;
  RayQuery<RAY_FLAG_FORCE_NON_OPAQUE> query;
  query.TraceRayInline(emTlas, RAY_FLAG_NONE, 0xff, ray);
  uint draws[8]; uint count = 0u, candidates = 0u;
  [unroll] for (uint initialize = 0u; initialize < 8u; ++initialize) draws[initialize] = 0xffffffffu;
  bool overflow = false, invalid = false;
  while (query.Proceed()) {
    if (query.CandidateType() != CANDIDATE_NON_OPAQUE_TRIANGLE) continue;
    uint geometry = query.CandidateGeometryIndex();
    EmTri tri = em_triangle(geometry, query.CandidatePrimitiveIndex(), query.CandidateObjectToWorld3x4());
    if (!tri.ok) { invalid = true; continue; }
    if (emRecs[tri.rec].fs_hi != pc.fam) continue;
    ++candidates;
    bool seen = false;
    [unroll] for (uint i = 0u; i < 8u; ++i) if (i < count && draws[i] == geometry) seen = true;
    if (!seen) {
      if (count < 8u) draws[count++] = geometry;
      else overflow = true;
    }
    // Never commit: retain all candidates, including equal-distance draws.
  }
  stat_n(9, candidates);
  if (count > 1u) stat(10);
  stat_n(11, count);
  if (invalid) stat(12);
  if (overflow) stat(13);
  if (candidates == 0u) stat(14);
}
// Luminance x 4096 as an integer (the sums' unit): below 2^29.
uint q4096(float lum) { return uint(clamp(lum, 0.0, 1e5) * 4096.0 + 0.5); }
// h x 2^14 + l as a 64-bit (lo, hi) pair, without 64-bit integers (shaderInt64 is optional).
uint2 join14(uint h, uint l) {
  const uint a = h << 14, lo = a + l;
  return uint2(lo, (h >> 18) + (lo < a ? 1u : 0u));
}
// The wave's 64-bit sum of values below 2^29: split at bit 14, each part's sum fits 32 bits.
uint2 wave_sum64(uint v) { return join14(WaveActiveSum(v >> 14), WaveActiveSum(v & 0x3fffu)); }
// 64-bit add at byte `a` of emDev (lo, hi): the carry from the returned old value.
void add64(uint a, uint2 v) {
  if ((v.x | v.y) == 0) return;
  uint old = 0;
  if (v.x) emDev.InterlockedAdd(a, v.x, old);
  const uint hi = v.y + (old + v.x < old ? 1u : 0u);
  if (hi) emDev.InterlockedAdd(a + 4, hi);
}
void add32(uint a, uint v) { if (v) emDev.InterlockedAdd(a, v); }

// Per record (16 words): ours (0-1), the game's (2-3), pixels (4), hits where ours is exactly 0
// (5), and ours without the maps (6-7): the record's radiance x the luminance scale only,
// which separates a zero level from a map sampled where it is black (wrong UV or texture).
// Bright hits (8) and their own sums (9-10 ours, 11-12 the game's): the ratio of the emission
// the player sees, apart from a family's dark texels. Word 13: bright hits left out of the
// bright sums because another captured emitter lies in front on the same ray (an alpha-blended
// glow, particles): the game's pixel holds both, ours only the surface at the depth, so those
// pixels do not measure this family.
// One lane per distinct record of the wave adds the wave's sums (lanes with the same record
// that the hardware runs apart add their own partial sums: the totals are the same).
void rec_stat(uint rec, float ours, float game, bool overlapped) {
  bool todo = rec < kRecs;
  const bool bright = game > pc.thr;
  const uint vo = q4096(ours), vg = q4096(game);
  [loop] while (todo) {
    const uint r = WaveReadLaneFirst(rec);
    if (rec == r) {
      todo = false;
      const uint base = (16 + r * 16) * 4;
      const bool counted = bright && !overlapped;
      const uint n = WaveActiveCountBits(true);
      const uint zero = WaveActiveCountBits(ours <= 0.0);
      const uint over = WaveActiveCountBits(bright && overlapped);
      const uint nb = WaveActiveCountBits(counted);
      const uint2 so = wave_sum64(vo), sg = wave_sum64(vg);
      const uint2 sob = wave_sum64(counted ? vo : 0u), sgb = wave_sum64(counted ? vg : 0u);
      if (WaveIsFirstLane()) {
        add64(base, so);
        add64(base + 8, sg);
        add32(base + 16, n);
        add32(base + 20, zero);
        add32(base + 52, over);
        add32(base + 32, nb);
        add64(base + 36, sob);
        add64(base + 44, sgb);
        // The record's radiance x the luminance scale: the same for every hit of the record.
        EmRec er = emRecs[r];
        float3 L = er.radiance;
        if ((er.flags & kEmFlagLuminance) != 0 && er.glob != 0xFFFFFFFFu) {
          const float k = asfloat(emGlobal.Load(er.glob * 4));
          L *= k >= 0.0 && k < 1e6 ? k : 1.0;
        }
        const uint qn = q4096(all(L >= 0.0) && all(L < 1e6) ? em_lum(L) : 0.0);
        add64(base + 24, join14((qn >> 14) * n, (qn & 0x3fffu) * n));
      }
    }
  }
}

[numthreads(8, 8, 1)]
void main(uint3 tid : SV_DispatchThreadID, uint gi : SV_GroupIndex) {
  if (gi < 16) gsStat[gi] = 0u;
  GroupMemoryBarrierWithGroupSync();
  const uint2 grid = max(uint2(pc.grid & 0xffffu, pc.grid >> 16), uint2(1, 1));
  const uint2 fp = tid.xy * grid + uint2(pc.origin & 0xffffu, pc.origin >> 16);
  // No early return: every lane reaches the reductions below.
  const bool inside = all(fp < pc.full);
  uint srec = 0xFFFFFFFFu;   // the record this pixel adds to the per-record statistics
  float sours = 0.0, sgame = 0.0;
  bool sover = false;
  float3 c = float3(0, 0, 0);
  const float d = inside ? gDepth.Load(int3(fp, 0)) : 0.0;
  const float3 fx = inside ? max(gFx.Load(int3(fp, 0)).rgb, 0.0) : float3(0, 0, 0);
  if (d > 0.0) {
    stat(0);
    const float2 uv = (float2(fp) + 0.5) / float2(pc.full);
    const float3 r = ray_at(uv);
    const float3 P = r * d;
    const float dist = length(P);
    const float3 dir = P / max(dist, 1e-20);
    const float px = length(ray_at(uv + float2(1.0 / float(pc.full.x), 0.0)) - r) / max(length(r), 1e-20) * dist;
    const float3 shift = pc.has_cam2 != 0 ? em_shift(cam2.v[13].xyz) : float3(0, 0, 0);
    float3 L;
    uint rec;
    // The emissive surface at the pixel's own depth (the depth buffer says where the
    // primary ray stops; the main TLAS is not needed).
    const float eps = dist * pc.tol + 0.01;
    const bool hit = em_lookup(float3(0, 0, 0), dir, dist, eps, px, shift, L, rec);
    const float ours = em_lum(L), game = em_lum(fx);
    bool bright = game > pc.thr;
    if (bright) stat(7);
    if (!hit) {
      // An emitter in front of the depth: an alpha-blended glow card or halo is drawn into
      // the effects target but not into the depth buffer. Counted apart (not a miss).
      float3 Lf;
      uint recf;
      if (em_lookup(float3(0, 0, 0), dir, 0.5 * max(dist - eps, 0.0), 0.5 * max(dist - eps, 0.0), px, shift, Lf, recf)) {
        stat(5);
        if (bright) {
          stat(4);
          srec = recf, sours = em_lum(Lf), sgame = game, sover = false;
        }
        if (pc.mode == 6) c = Lf;
        bright = false;   // covered, by the emitter in front
      }
    }
    if (hit) {
      overlap_probe(dir, dist, eps, shift, rec);
      stat(1);
      if (game < 0.02 * ours) stat(3);
      // Another emitter in front of this one on the same ray (see rec_stat).
      float3 Lo;
      uint reco;
      const float front = 0.5 * max(dist - eps, 0.0);
      const bool over = bright && front > 0.0 && em_lookup(float3(0, 0, 0), dir, front, front, px, shift, Lo, reco) &&
                        em_lum(Lo) > 0.0;
      srec = rec, sours = ours, sgame = game, sover = over;
    } else if (bright) {
      stat(2);
    }
    if (pc.mode == 6) {
      c = hit ? L : c;
    } else if (pc.mode == 8) {
      if (hit && bright) {
        float k = ours / max(game, 1e-6);
        c = k > 1.25 ? float3(100, 0, 0) : k < 0.8 ? float3(0, 0, 100) : float3(0, 100, 0);
      } else if (hit) {
        c = float3(100, 100, 0);   // ours only (yellow)
      } else if (bright) {
        c = float3(100, 0, 100);   // the game's only (magenta)
      }
    } else if (pc.mode == 9 || pc.mode == 10) {
      // One family (rt_em_dbg_fam, the top 32 bits of its FS hash) alone: ours (9) or the
      // game's effects target (10) at its hits, black elsewhere; a pair of traces compares
      // them pixel by pixel offline.
      const bool mine = hit && rec < kRecs && emRecs[rec].fs_hi == pc.fam;
      c = mine ? (pc.mode == 9 ? L : fx) : float3(0, 0, 0);
    }
  }
  rec_stat(srec, sours, sgame, sover);
  GroupMemoryBarrierWithGroupSync();
  if (gi < 16) add32(gi * 4, gsStat[gi]);
  if (all(tid.xy == 0)) emDev.Store(6 * 4, 1u);
  if (pc.mode != 0 && inside) {   // mode 0: statistics only (the calibration's check)
    gDiffuse[fp] = c;
    gSpecular[fp] = float3(0, 0, 0);
  }
}

// emStats += emDev, after main (core/emissive.cpp): one thread per word, the non-zero words only;
// the 64-bit sums (record words 0, 2, 6, 9, 11 with the next) as one.
bool pair_lo(uint k) { return k == 0 || k == 2 || k == 6 || k == 9 || k == 11; }
bool pair_hi(uint k) { return k == 1 || k == 3 || k == 7 || k == 10 || k == 12; }

[numthreads(256, 1, 1)]
void flush(uint3 tid : SV_DispatchThreadID) {
  const uint i = tid.x;
  if (i >= kStatWords) return;
  const uint k = i < 16 ? 16u : (i - 16) & 15u;   // 16: a global word (32-bit)
  if (pair_hi(k)) return;
  if (pair_lo(k)) {
    const uint2 v = emDev.Load2(i * 4);
    if ((v.x | v.y) == 0) return;
    const uint2 o = emStats.Load2(i * 4);
    const uint lo = o.x + v.x;
    emStats.Store2(i * 4, uint2(lo, o.y + v.y + (lo < o.x ? 1u : 0u)));
  } else {
    const uint v = emDev.Load(i * 4);
    if (v == 0) return;
    emStats.Store(i * 4, i == 6 ? 1u : emStats.Load(i * 4) + v);
  }
}
