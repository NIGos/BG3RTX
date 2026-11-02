// rt_occ_stats CPU side (common/occ_stats.h): the counter layout the shaders share, the per-frame
// fold of the 64 bins, and the log line's numbers.
#include "../common/occ_stats.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
using namespace bg3::occ_stats;
static unsigned checks;
#define CHECK(x) do { ++checks; if (!(x)) { std::printf("FAIL %d: %s\n", __LINE__, #x); std::exit(1); } } while (0)

static bool has(const std::string& s, const char* sub) {
  if (s.find(sub) != std::string::npos) return true;
  std::printf("missing \"%s\" in: %s\n", sub, s.c_str());
  return false;
}

int main() {
  // Layout: per-source blocks (kinds, near, dark, distance histogram), then the replica and pass
  // counters, all inside kOccCount; the group flush (rt/occ_stats.hlsli) covers kOccCount.
  CHECK(kOccScrSkin < kOccKindRays);
  CHECK(kOccHistN + kOccHistBins <= kOccSrcStride);
  CHECK(kOccSunMismatch == kOccSrcStride * kOccSources);
  CHECK(kOccPassLs < kOccCount);
  CHECK(kOccBytes == kOccCount * kOccBins * 4u);
  CHECK(kOccKindHeld == kOccKinds - 1u);
  CHECK(kOccKindCutoutMoving == kOccKindCutoutStatic + 1u);
  CHECK(kOccInfoHeld == 0x80000000u);   // layer/bvhsplit.cpp kInfoHeld
  CHECK(kOccHistEdge[3] == kOccNear);   // the near test is a histogram edge
  for (uint32_t i = 1; i + 1 < kOccHistBins; ++i) CHECK(kOccHistEdge[i] > kOccHistEdge[i - 1]);
  // Fixed point: a 4K frame of fractions 1 fits a 32-bit counter.
  CHECK(3840.0 * 2160.0 * kOccQ < 4294967296.0);

  std::vector<uint32_t> slot(kOccCount * kOccBins, 0u);
  auto at = [&](uint32_t counter, uint32_t bin) -> uint32_t& { return slot[counter * kOccBins + bin]; };
  const uint32_t ao = kOccAO * kOccSrcStride, sun = kOccSun * kOccSrcStride;
  // AO: 1000 character px, 400 darkened, darkening 0.25 each over the 1000 (sum 250 -> Q 64000),
  // split over two bins; 2000 rays, 800 occluded: 600 moving (500 near), 200 static (0 near).
  at(ao + kOccPx, 3) = 600, at(ao + kOccPx, 40) = 400;
  at(ao + kOccDarkPx, 0) = 400;
  at(ao + kOccDark, 5) = 64000;
  at(ao + kOccRays, 1) = 2000;
  at(ao + kOccRaysOcc, 1) = 800;
  at(ao + kOccKindRays + kOccKindMoving, 2) = 600;
  at(ao + kOccKindNear + kOccKindMoving, 2) = 500;
  at(ao + kOccKindRays + kOccKindStatic, 9) = 200;
  at(ao + kOccKindDark + kOccKindMoving, 0) = 3 * 256;
  at(ao + kOccKindDark + kOccKindStatic, 0) = 1 * 256;
  at(ao + kOccScrHair, 0) = 200;
  at(ao + kOccHistW + 0, 0) = 300, at(ao + kOccHistW + 3, 63) = 100;
  at(ao + kOccHistN + 0, 0) = 30, at(ao + kOccHistN + 3, 63) = 10;
  // Sun: 100 px, 10 darkened; 100 rays, 10 occluded: 6 by moving cutouts 3 m away, 4 held; 2 mismatches.
  at(sun + kOccPx, 0) = 100, at(sun + kOccDarkPx, 0) = 10, at(sun + kOccDark, 0) = 10 * 256;
  at(sun + kOccRays, 0) = 100, at(sun + kOccRaysOcc, 0) = 10;
  at(sun + kOccKindRays + kOccKindCutoutMoving, 0) = 6, at(sun + kOccKindRays + kOccKindHeld, 0) = 4;
  at(sun + kOccKindDark + kOccKindCutoutMoving, 0) = 6 * 256, at(sun + kOccKindDark + kOccKindHeld, 0) = 4 * 256;
  at(sun + kOccHistW + 6, 0) = 6 * 256, at(sun + kOccHistN + 6, 0) = 6;
  at(sun + kOccHistW + 2, 0) = 4 * 256, at(sun + kOccHistN + 2, 0) = 4;
  at(kOccSunMismatch, 7) = 2;
  at(kOccPassMain, 0) = 1, at(kOccPassLs, 0) = 1;

  OccWindow w;
  occ_fold(w, slot.data());
  CHECK(w.frames == 1);
  CHECK(w.c[ao + kOccPx] == 1000);
  CHECK(w.c[ao + kOccDark] == 64000);
  occ_fold(w, slot.data());   // a second identical frame: every sum doubles, the shares do not
  CHECK(w.frames == 2);
  CHECK(w.c[ao + kOccPx] == 2000);
  CHECK(w.c[ao + kOccHistW + 3] == 200);

  const std::string line = occ_format(w);
  CHECK(has(line, "occ stats (2 frames): character px"));
  CHECK(has(line, "AO: 1000 px/frame, darkened 40.0%, mean darkening 25.00%, rays 4000 occluded 40.0% unknown 0, on-screen hair 25.0% skin 0.0%"));
  CHECK(has(line, " static 25.0 [0.0] 25.0 moving 75.0 [83.3] 75.0"));   // kind order, near share, dark share
  CHECK(has(line, "occluder distance (dark %, rays): <0.05m 75.0 60 <0.1m 0.0 0 <0.2m 0.0 0 <0.5m 25.0 20"));
  CHECK(has(line, "sun: 100 px/frame, darkened 10.0%, mean darkening 10.00%"));
  CHECK(has(line, " cutout-moving 60.0 [0.0] 60.0 held 40.0 [0.0] 40.0; occluder distance (dark %, rays): <0.05m 0.0 0"));
  CHECK(has(line, "<0.2m 40.0 8 <0.5m 0.0 0 <1m 0.0 0 <2m 0.0 0 <5m 60.0 12 >=5m 0.0 0"));
  CHECK(has(line, "local: 0 px/frame, darkened 0.0%, mean darkening 0.00%, rays 0 occluded 0.0% unknown 0, on-screen hair 0.0% skin 0.0%; kinds (rays % [near %] dark %) none"));
  CHECK(has(line, "sun replica mismatch 4 of 200 rays; passes: sun/AO 2, D-LS 2 frames"));
  CHECK(line.find(" cascade ") == std::string::npos);   // kinds with no occluded ray are left out

  const std::string empty = occ_format(OccWindow{});   // no division by zero
  CHECK(has(empty, "occ stats (0 frames)"));
  CHECK(has(empty, "AO: 0 px/frame, darkened 0.0%"));
  std::printf("occ_stats_test: %u checks passed\n", checks);
  return 0;
}
