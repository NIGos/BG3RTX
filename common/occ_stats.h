#pragma once
// rt_occ_stats (docs/DIALOGUE_SHADOWS.md "Occluder-kind diagnostic"): which occluder darkens the
// character pixels (G-buffer class skin 3 / hair 5), per light source. Counter layout shared by
// the shaders (rt/occ_stats.hlsli: gi/rt_occ_stats.hlsl for the sun and the AO, the D-LS
// RT_OCC_STATS variant for the point/spot and box lights) and core/gi.cpp, which sums the 64
// bins of each counter per frame and logs one line per window (occ_format below).
// Device-local counters, reduced per 8x8 group in groupshared memory, copied to a host ring
// after the timed passes (never host-visible atomics, tests/host_atomics_lint_test.py).
#ifdef __cplusplus
#include <cstdint>
#include <cstdio>
#include <string>
namespace bg3::occ_stats {
#define OCC_UINT inline constexpr uint32_t
#else
#define OCC_UINT static const uint
#endif

// Sources: the RT sun ray, the D-LS point/spot rays, the D-LS box (projector) rays, the GI/AO rays.
OCC_UINT kOccSun = 0u, kOccLocal = 1u, kOccBox = 2u, kOccAO = 3u, kOccSources = 4u;
// Occluder kinds of the committed (closest) hit: 0 unknown (query aborted by the candidate
// cap), 1 cascade copy (the F-1/F-2 sun caster soup, instance id 0xFFFFFC..0xFFFFFF),
// 2 static mesh (cached pool instance, opaque), 3 moving opaque geometry (per-draw soup BLAS of this
// frame's VS output: characters, skinned and wind-animated opaque draws), 4 static cutout (pool
// instance with an alpha record: alpha-table kinds 1/2/5), 5 moving cutout (soup instance with an
// alpha record: wind-animated foliage and other per-frame cutouts), 6 scalp (alpha kind 3), 7 hair
// (alpha kind 4 or the native hair procedural), 8 held draw (an rt_dyn_hold copy of an older frame,
// opaque or cutout).
OCC_UINT kOccKinds = 9u;
OCC_UINT kOccKindUnknown = 0u, kOccKindCascade = 1u, kOccKindStatic = 2u, kOccKindMoving = 3u, kOccKindCutoutStatic = 4u,
         kOccKindCutoutMoving = 5u, kOccKindScalp = 6u, kOccKindHair = 7u, kOccKindHeld = 8u;
// "Near": hit distance <= 0.5 m, the stand-in for self-shadowing (the TLAS has no object identity).
#ifdef __cplusplus
inline constexpr float kOccNear = 0.5f;
#else
static const float kOccNear = 0.5;
#endif
// InstInfo meta bit 31 (layer/bvhsplit.cpp): a dynamic instance held by rt_dyn_hold. Bits 16..31
// are the pool alpha slot only with nh_pool_meta (bit 15), which a held instance never has.
OCC_UINT kOccInfoHeld = 1u << 31;

// Per source s, counters kOccSrcStride * s + :
OCC_UINT kOccPx = 0u;         // character pixels evaluated (sun: lit side or back-lit; local: lit by >= 1 traced light)
OCC_UINT kOccDarkPx = 1u;     // of those, darkened by this source's rays
OCC_UINT kOccDark = 2u;       // sum of the darkening fraction, x kOccQ (sun: game mask - traced; local/box: removed
                              // light / the pixel's diffuse + specular; AO: 1 - ambient visibility)
OCC_UINT kOccRays = 3u;       // rays traced from character pixels
OCC_UINT kOccRaysOcc = 4u;    // of those, occluded (a committed hit)
OCC_UINT kOccRaysUnknown = 5u;
OCC_UINT kOccScrHair = 6u;    // occluded rays whose hit is the visible surface at its screen pixel, class hair
OCC_UINT kOccScrSkin = 7u;    // ... class skin
OCC_UINT kOccKindRays = 8u;   // + kind: occluded rays by occluder kind
OCC_UINT kOccKindNear = kOccKindRays + kOccKinds;   // 17 + kind: of those, hit t <= kOccNear
OCC_UINT kOccKindDark = kOccKindNear + kOccKinds;   // 26 + kind: darkening fraction by occluder kind, x kOccQ
// Occluder hit-distance histogram of the source (occluded rays from character pixels): darkening
// x kOccQ (AO: the occlusion weight / rays) and ray count per bin, bin edges kOccHistEdge (metres):
// self-shadowing sits below 0.5 m, a tree or a roof metres away.
OCC_UINT kOccHistBins = 8u;
OCC_UINT kOccHistW = kOccKindDark + kOccKinds;      // 35 + bin
OCC_UINT kOccHistN = kOccHistW + kOccHistBins;      // 43 + bin
OCC_UINT kOccSrcStride = 56u;                       // (51 used)
OCC_UINT kOccSunMismatch = kOccSrcStride * kOccSources;   // 224: the sun replica ray disagrees with rtVis.r
OCC_UINT kOccPassMain = kOccSunMismatch + 1u;              // 225: frames the sun/AO pass ran (rt_occ_stats.hlsl)
OCC_UINT kOccPassLs = kOccPassMain + 1u;                   // 226: frames the D-LS RT_OCC_STATS variant ran
OCC_UINT kOccCount = 232u;    // counters (the group flush: each of the 64 threads adds every 64th)
OCC_UINT kOccBins = 64u;
OCC_UINT kOccBytes = kOccCount * kOccBins * 4u;
// Fixed point of the fraction sums: 1/256 per pixel (a whole 4K frame of fractions <= 1 fits 32 bits).
#ifdef __cplusplus
inline constexpr float kOccQ = 256.0f;
inline constexpr float kOccHistEdge[kOccHistBins] = {0.05f, 0.1f, 0.2f, 0.5f, 1.0f, 2.0f, 5.0f, 1e30f};
#else
static const float kOccQ = 256.0;
#endif

#ifdef __cplusplus
inline const char* const kOccSourceName[kOccSources] = {"sun", "local", "box", "AO"};
inline const char* const kOccKindName[kOccKinds] = {"unknown", "cascade", "static", "moving", "cutout-static",
                                                    "cutout-moving", "scalp", "hair", "held"};

// One logging window: per-frame slots folded in (the 64 bins of each counter summed).
struct OccWindow {
  uint64_t c[kOccCount] = {};
  uint32_t frames = 0;
};

// Adds one frame's read-back slot (kOccCount x kOccBins uint32, counter-major).
inline void occ_fold(OccWindow& w, const uint32_t* slot) {
  for (uint32_t i = 0; i < kOccCount; ++i) {
    uint64_t s = 0;
    for (uint32_t b = 0; b < kOccBins; ++b) s += slot[i * kOccBins + b];
    w.c[i] += s;
  }
  ++w.frames;
}

// Percent with one decimal of a / b (0 when b is 0).
inline double occ_pct(uint64_t a, uint64_t b) { return b ? 100.0 * double(a) / double(b) : 0.0; }

// The log line: per source, the share of character pixels darkened and the mean darkening over the
// evaluated pixels, the occluded share of the rays; per occluder kind its share of the occluded rays
// (near share in brackets) and of the darkening, then the occluder-distance histogram (share of the
// darkening and rays per bin). Kinds with no occluded ray are left out.
inline std::string occ_format(const OccWindow& w) {
  char b[512];
  std::string out;
  std::snprintf(b, sizeof b, "occ stats (%u frames): character px", w.frames);
  out += b;
  for (uint32_t s = 0; s < kOccSources; ++s) {
    const uint64_t* c = w.c + s * kOccSrcStride;
    const double px = double(c[kOccPx]) / (w.frames ? w.frames : 1);
    std::snprintf(b, sizeof b, "; %s: %.0f px/frame, darkened %.1f%%, mean darkening %.2f%%, rays %llu occluded %.1f%% "
                  "unknown %llu, on-screen hair %.1f%% skin %.1f%%; kinds (rays %% [near %%] dark %%)",
                  kOccSourceName[s], px, occ_pct(c[kOccDarkPx], c[kOccPx]),
                  c[kOccPx] ? 100.0 * double(c[kOccDark]) / kOccQ / double(c[kOccPx]) : 0.0,
                  (unsigned long long)c[kOccRays], occ_pct(c[kOccRaysOcc], c[kOccRays]),
                  (unsigned long long)c[kOccRaysUnknown], occ_pct(c[kOccScrHair], c[kOccRaysOcc]),
                  occ_pct(c[kOccScrSkin], c[kOccRaysOcc]));
    out += b;
    uint64_t dark = 0;
    for (uint32_t k = 0; k < kOccKinds; ++k) dark += c[kOccKindDark + k];
    bool any = false;
    for (uint32_t k = 0; k < kOccKinds; ++k) {
      if (!c[kOccKindRays + k]) continue;
      std::snprintf(b, sizeof b, " %s %.1f [%.1f] %.1f", kOccKindName[k], occ_pct(c[kOccKindRays + k], c[kOccRaysOcc]),
                    occ_pct(c[kOccKindNear + k], c[kOccKindRays + k]), occ_pct(c[kOccKindDark + k], dark));
      out += b;
      any = true;
    }
    if (!any) out += " none";
    uint64_t hw = 0;
    for (uint32_t i = 0; i < kOccHistBins; ++i) hw += c[kOccHistW + i];
    out += "; occluder distance (dark %, rays):";
    for (uint32_t i = 0; i < kOccHistBins; ++i) {
      if (i + 1 < kOccHistBins) std::snprintf(b, sizeof b, " <%gm %.1f %llu", kOccHistEdge[i], occ_pct(c[kOccHistW + i], hw),
                                              (unsigned long long)c[kOccHistN + i]);
      else std::snprintf(b, sizeof b, " >=%gm %.1f %llu", kOccHistEdge[i - 1], occ_pct(c[kOccHistW + i], hw),
                         (unsigned long long)c[kOccHistN + i]);
      out += b;
    }
  }
  std::snprintf(b, sizeof b, "; sun replica mismatch %llu of %llu rays; passes: sun/AO %llu, D-LS %llu frames",
                (unsigned long long)w.c[kOccSunMismatch], (unsigned long long)w.c[kOccSun * kOccSrcStride + kOccRays],
                (unsigned long long)w.c[kOccPassMain], (unsigned long long)w.c[kOccPassLs]);
  out += b;
  return out;
}
}  // namespace bg3::occ_stats
#endif
