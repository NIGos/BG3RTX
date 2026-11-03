// common/hair_groom_asset.h: index-only content ID (shared fixture with tests/hair_groom_test.py) and
// the .hgrm parser's range checks.
#include "../common/hair_groom_asset.h"
#include "../common/hair_groom_budget.h"
#include "../common/hair_primary.h"
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <cmath>
#include <string>
#include <vector>
using namespace bg3::hair_groom;
#define CHECK(x) do { if (!(x)) { std::printf("FAIL %d: %s\n", __LINE__, #x); std::exit(1); } } while (0)

static void put(std::vector<uint8_t>& b, const void* p, size_t n) {
  const auto* c = static_cast<const uint8_t*>(p);
  b.insert(b.end(), c, c + n);
}
template <class T> static void put(std::vector<uint8_t>& b, T v) { put(b, &v, sizeof v); }

// One card of 2 triangles (6 indices), one LOD: 3 points on triangle 1, 2 segments.
static std::vector<uint8_t> asset(float radius = 0.0001f, uint32_t tri = 1, uint32_t last_segment = 1) {
  std::vector<uint8_t> b;
  put(b, "HGRM", 4);
  put<uint32_t>(b, 1); put<uint64_t>(b, 7); put<uint64_t>(b, 9);
  put<uint32_t>(b, 4); put<uint32_t>(b, 6); put<uint32_t>(b, 1); put<uint32_t>(b, 0);
  put<float>(b, 1.0f); put<uint32_t>(b, 3); put<uint32_t>(b, 2); put<uint32_t>(b, 0);
  for (int i = 0; i < 3; ++i) put<uint32_t>(b, tri);
  for (int i = 0; i < 3; ++i) { put<float>(b, 0.25f); put<float>(b, 0.25f); }
  for (int i = 0; i < 3; ++i) put<float>(b, radius);
  for (int i = 0; i < 3; ++i) put<uint8_t>(b, uint8_t(i * 100));
  put<uint32_t>(b, 0); put<uint32_t>(b, last_segment);
  return b;
}

int main() {
  const uint32_t quad[6] = {0, 1, 2, 0, 2, 3};
  CHECK(index_content_id([&](uint32_t i) { return quad[i]; }, 6) == 0xce53baad8d797436ull);
  // Offsets and index widths do not matter, only the referenced pattern.
  const uint32_t moved[6] = {10, 11, 12, 10, 12, 13};
  CHECK(index_content_id([&](uint32_t i) { return moved[i]; }, 6) == 0xce53baad8d797436ull);
  const uint32_t other[6] = {0, 1, 2, 0, 3, 2};
  CHECK(index_content_id([&](uint32_t i) { return other[i]; }, 6) != 0xce53baad8d797436ull);
  CHECK(index_content_id([&](uint32_t i) { return quad[i]; }, 4) == 0);

  // Join verdicts, in the order the layer tests them.
  CHECK(join_miss(false, true, true, true) == JoinMiss::NotIndexed);
  CHECK(join_miss(true, false, true, true) == JoinMiss::CountUnknown);
  CHECK(join_miss(true, true, false, false) == JoinMiss::NotHostMapped);
  CHECK(join_miss(true, true, true, false) == JoinMiss::NoFile);
  CHECK(join_miss(true, true, true, true) == JoinMiss::None);
  for (int m = 0; m < int(JoinMiss::Count); ++m) CHECK(std::string(join_miss_name(JoinMiss(m))) != "?");
  // UV join: exact, within tolerance, v flipped; a different sample or NaN does not match.
  float want[64], got[64];
  for (int i = 0; i < 64; ++i) want[i] = got[i] = 0.01f * float(i % 50);
  CHECK(uv_samples_match(got, want));
  got[5] += 5e-4f;
  CHECK(uv_samples_match(got, want));
  for (int s = 0; s < 32; ++s) got[2 * s + 1] = 1.0f - want[2 * s + 1];
  CHECK(uv_samples_match(got, want));
  got[10] += 0.01f;
  CHECK(!uv_samples_match(got, want));
  for (int i = 0; i < 64; ++i) got[i] = want[i];
  got[0] = std::nanf("");
  CHECK(!uv_samples_match(got, want));
  CHECK(uv_sample_corner(0, 6) == 0 && uv_sample_corner(31, 6) == 5 && uv_sample_corner(16, 120270) == 62074);

  Asset a;
  auto b = asset();
  CHECK(parse(b.data(), b.size(), a) && !a.has_uv);
  {   // v2: the same file with 64 UV floats after the header
    std::vector<uint8_t> v2(b.begin(), b.begin() + 40);
    const uint32_t two = 2;
    std::memcpy(v2.data() + 4, &two, 4);
    for (int i = 0; i < 64; ++i) put<float>(v2, 0.25f);
    v2.insert(v2.end(), b.begin() + 40, b.end());
    CHECK(parse(v2.data(), v2.size(), a) && a.has_uv && a.uv[63] == 0.25f && a.lods.size() == 1);
    put<float>(v2, 0.0f);
    CHECK(!parse(v2.data(), v2.size(), a));
  }
  CHECK(parse(b.data(), b.size(), a));
  CHECK(a.index_id == 9 && a.position_id == 7 && a.indices == 6 && a.lods.size() == 1);
  CHECK(a.lods[0].points == 3 && a.lods[0].segments == 2 && a.lods[0].root[2] == 200);
  uint32_t seg1;
  std::memcpy(&seg1, a.lods[0].segment + 1, 4);
  CHECK(seg1 == 1);
  b.push_back(0);
  CHECK(!parse(b.data(), b.size(), a));                       // trailing bytes
  b = asset(0.02f);
  CHECK(!parse(b.data(), b.size(), a));                       // radius beyond a hair clump
  b = asset(0.0001f, 2);
  CHECK(!parse(b.data(), b.size(), a));                       // triangle out of range
  b = asset(0.0001f, 1, 2);
  CHECK(!parse(b.data(), b.size(), a));                       // segment past the last point
  b = asset();
  CHECK(!parse(b.data(), b.size() - 1, a));                   // truncated
  b[0] = 'X';
  CHECK(!parse(b.data(), b.size(), a));
  // Header-only index entry: ids, counts and the LOD count, no arrays.
  b = asset();
  CHECK(parse_header(b.data(), 40, a) && a.index_id == 9 && a.indices == 6 && a.lod_count == 1 && a.lods.empty());
  CHECK(!parse_header(b.data(), 39, a));

  // Pool budget: a quarter of the capacity; evictions oldest first, never pinned or recently used ones.
  CHECK(pool_budget(2047ull << 20) == (2047ull << 20) / 4);
  std::vector<uint64_t> ev;
  std::vector<Resident> res = {{1, 40, 10, false}, {2, 30, 5, false}, {3, 50, 990, false}, {4, 20, 1, true}};
  CHECK(plan_evictions(res, 140, 10, 200, 1000, 120, ev) && ev.empty());
  CHECK(plan_evictions(res, 140, 70, 200, 1000, 120, ev) && ev == std::vector<uint64_t>{2});
  CHECK(plan_evictions(res, 140, 100, 200, 1000, 120, ev) && (ev == std::vector<uint64_t>{2, 1}));
  CHECK(!plan_evictions(res, 140, 140, 200, 1000, 120, ev) && ev.empty());
  CHECK(!plan_evictions(res, 0, 201, 200, 1000, 120, ev));
  {   // LOD headers (the crowd plan's segment counts at index time)
    const std::vector<uint8_t> b = asset();
    Asset h;
    uint64_t at = 0;
    CHECK(parse_header(b.data(), b.size(), h, &at));
    uint32_t points = 0, segments = 0;
    uint64_t next = 0;
    CHECK(lod_header(b.data() + at, at, &points, &segments, &next) && points == 3 && segments == 2 && next == b.size());
    std::vector<uint8_t> bad = b;
    std::memset(bad.data() + at, 0, 4);   // keep 0
    CHECK(!lod_header(bad.data() + at, at, &points, &segments, &next));
  }
  {   // crowd plan (common/hair_groom_budget.h plan_crowd)
    std::vector<CrowdCandidate> c(4);
    const uint32_t seg[3] = {855000, 451000, 236000};   // Shadowheart LOD 0, 1/2, 1/4 at 2 mm
    for (auto& k : c) { std::copy(seg, seg + 3, k.segments); k.lods = 3; }
    c[0].size = 0.2f; c[0].want = 0;   // close
    c[1].size = 0.1f; c[1].want = 0;
    c[2].size = 0.05f; c[2].want = 1;
    c[3].size = 0.01f; c[3].want = 2;   // far
    CHECK((plan_crowd(c, 0) == std::vector<int>{0, 0, 1, 2}));                 // off: every screen-size LOD
    CHECK((plan_crowd(c, 4000000) == std::vector<int>{0, 0, 1, 2}));           // enough for all
    CHECK((plan_crowd(c, 1600000) == std::vector<int>{0, 1, 2, -1}));          // the far one keeps its cards
    CHECK((plan_crowd(c, 900000) == std::vector<int>{0, -1, -1, -1}));         // never coarser than needed for the biggest
    CHECK((plan_crowd(c, 100000) == std::vector<int>{-1, -1, -1, -1}));        // nothing fits: cards
    std::swap(c[0], c[3]);                                                       // order comes from size, not position
    CHECK((plan_crowd(c, 1600000) == std::vector<int>{-1, 1, 2, 0}));
  }
  // ---- primary strand hair fail-safes (common/hair_primary.h) ----
  {
    using hair_primary::soup_update_ok;
    const uint64_t soup = 2146435072ull;   // the 2 GB soup of the 2026-11-03 17:01 device loss
    const uint32_t tris = 28372, stride = 88;   // the matched 85116-index draw
    CHECK(soup_update_ok(true, false, 1000000, tris, stride, soup, tris));
    // The crash: a held draw (its offset is a mesh-pool snapshot offset, ~1.97 GB) read as a soup offset.
    CHECK(!soup_update_ok(true, true, 2063785984ull - 100000ull, tris, stride, soup, tris));
    CHECK(!soup_update_ok(false, false, 1000000, tris, stride, soup, tris));                 // not captured this frame
    CHECK(!soup_update_ok(true, false, soup - uint64_t(tris) * 3 * stride + 88, tris, stride, soup, tris));  // past the end
    CHECK(soup_update_ok(true, false, soup - uint64_t(tris) * 3 * stride, tris, stride, soup, tris));        // exactly fits
    CHECK(!soup_update_ok(true, false, soup + 16, tris, stride, soup, tris));
    CHECK(!soup_update_ok(true, false, 0, tris - 1, stride, soup, tris));    // fewer captured triangles than the groom's
    CHECK(!soup_update_ok(true, false, 0, tris, 16, soup, tris));            // no UV in the stream
  }
  {
    hair_primary::SwapGate g;
    const uint64_t k = 0x1234;
    // Published twice: the G-buffer may swap, the prepass only after the G-buffer variant ran for this draw.
    g.publish(&k, 1);
    g.publish(&k, 1);
    CHECK(g.allow_gbuffer(k) && !g.allow_prepass(k) && !g.allow_velocity(k));
    // The bald player (2026-11-03): a matched draw whose G-buffer pipeline has no variant never ran it, so its
    // prepass card depth is never dropped, frame after frame.
    for (int f = 0; f < 5; ++f) { g.publish(&k, 1); CHECK(!g.allow_prepass(k)); }
    g.gbuffer_drew(k);
    g.publish(&k, 1);
    CHECK(g.allow_prepass(k) && g.allow_velocity(k));
    g.publish(&k, 1);   // a frame without the G-buffer variant: the prepass stops at once
    CHECK(!g.allow_prepass(k));
    // Feedback: strand pixels keep it; kStrikes counted frames with the variant and no strand pixel retire it.
    CHECK(!g.feedback(k, true, 500));
    CHECK(!g.feedback(k, false, 0));   // the variant did not run that frame: no evidence
    CHECK(!g.feedback(k, true, 0));
    CHECK(!g.feedback(k, true, 7));    // a strand pixel resets the strikes
    CHECK(!g.feedback(k, true, 0));
    CHECK(!g.feedback(k, true, 0, 900));   // hv5: the pixels carry an adjacent frame's tag: no evidence, no strike
    CHECK(g.feedback(k, true, 0));     // the second strike in a row
    g.gbuffer_drew(k);
    g.publish(&k, 1);
    g.publish(&k, 1);
    CHECK(!g.allow_gbuffer(k) && !g.allow_prepass(k) && !g.allow_velocity(k));
    g.clear();
    g.publish(&k, 1);
    g.publish(&k, 1);
    CHECK(!g.allow_gbuffer(k));   // retired, also across a reset, until its retry frame
    for (uint64_t f = 0; f < hair_primary::kRetryFrames; ++f) g.publish(&k, 1);
    CHECK(g.allow_gbuffer(k));    // retried after kRetryFrames (a draw off screen or behind another groom comes back)
    CHECK(!g.feedback(k, true, 0) && g.feedback(k, true, 0));
    CHECK(g.failed[k].wait == 2 * hair_primary::kRetryFrames);   // a second failure waits twice as long
    for (int i = 0; i < 8; ++i) {
      for (uint64_t f = 0; f < g.failed[k].wait; ++f) g.publish(&k, 1);
      CHECK(!g.feedback(k, true, 0) && g.feedback(k, true, 0));
    }
    CHECK(g.failed[k].wait == hair_primary::kRetryMaxFrames);   // capped
    // A key that leaves the published set stops at once in every pass.
    const uint64_t k2 = 0x99;
    g.publish(&k2, 1);
    g.publish(&k2, 1);
    g.gbuffer_drew(k2);
    g.publish(&k2, 1);
    CHECK(g.allow_prepass(k2));
    g.publish(nullptr, 0);
    CHECK(!g.allow_gbuffer(k2) && !g.allow_prepass(k2));
  }
  {   // A whole counted frame (hv5 2026-11-03: every draw of a counted frame struck at once).
    hair_primary::SwapGate g;
    const uint64_t a = 0xA, b = 0xB;
    const bool drew[3] = {true, true, true};
    // Two characters with the same hairstyle share one index-range key: one on screen, one not. Summed: no strike.
    const uint64_t shared[3] = {a, a, b};
    const uint32_t px[3] = {0, 1200, 800}, none[3] = {0, 0, 0};
    for (int f = 0; f < 4; ++f) CHECK(g.feedback_frame(shared, drew, px, none, 3).empty());
    // Every groom 0 with the counted tag but pixels with an adjacent tag (the G-buffer ran against another publish).
    const uint64_t keys[3] = {a, b, 0xC};
    const uint32_t zero[3] = {0, 0, 0}, adjacent[3] = {700, 0, 300};
    CHECK(g.feedback_frame(keys, drew, zero, adjacent, 3).empty());
    CHECK(g.feedback_frame(keys, drew, zero, adjacent, 3) == std::vector<uint64_t>{b});   // only b: no pixel at all
    CHECK(g.feedback_frame(keys, drew, zero, adjacent, 3).empty());
    CHECK(g.retired(b) && !g.retired(a) && !g.retired(0xC));
    // Genuinely no strand pixel anywhere, twice: every drawn key is retired.
    hair_primary::SwapGate h;
    CHECK(h.feedback_frame(keys, drew, zero, zero, 3).empty());
    CHECK(h.feedback_frame(keys, drew, zero, zero, 3).size() == 3);
  }
  {   // level reload (hv1 2026-11-03 18:40, device lost after F8): LoadGuard + SwapGate as hair_primary_publish uses them
    hair_primary::LoadGuard load;
    hair_primary::SwapGate g;
    const uint64_t k = 0x77;
    uint64_t frame = 100;
    auto publish = [&](uint64_t depth_image) {   // one frame's publish: boundary check first, then the keys
      if (load.observe(depth_image, frame)) g.clear();
      g.publish(&k, 1);
      ++frame;
    };
    publish(0xA);
    publish(0xA);
    g.gbuffer_drew(k);
    publish(0xA);
    CHECK(!load.quiet(frame) && g.allow_gbuffer(k) && g.allow_prepass(k));
    publish(0xB);   // F8: the reload recreated the G-buffer images
    CHECK(load.quiet(frame) && !g.allow_prepass(k));
    CHECK(load.quiet(frame + hair_primary::kLoadQuietFrames - 3));
    frame += hair_primary::kLoadQuietFrames;
    CHECK(!load.quiet(frame));
    publish(0xB);
    CHECK(g.allow_gbuffer(k) && !g.allow_prepass(k));   // the G-buffer has to prove it again in the new scene
    g.gbuffer_drew(k);
    publish(0xB);
    CHECK(g.allow_prepass(k));
    CHECK(!load.observe(0, frame) && !load.observe(0xB, frame));   // no image or the same image: no boundary
  }
  std::printf("hair_groom_asset_test: all passed\n");
  return 0;
}
