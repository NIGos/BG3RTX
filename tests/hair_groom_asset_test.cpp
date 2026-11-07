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

static void test_upload_submission() {
  using namespace hair_primary;
  CHECK(readback_state(100, 101, true) == ReadbackState::Wait);
  CHECK(readback_state(100, 102, false) == ReadbackState::Drop);
  CHECK(readback_state(102, 103, true) == ReadbackState::Wait); // redone upload
  CHECK(readback_state(102, 104, true) == ReadbackState::Ready);
}
static void test_compaction_submission() {
  using namespace hair_primary;
  // A stale, nonzero size is no authority to compact an unsubmitted build.
  const uint64_t stale_size = 64;
  CHECK(stale_size && readback_state(10, 50, false) == ReadbackState::Drop);
  CHECK(readback_state(50, 51, true) == ReadbackState::Wait);
  CHECK(readback_state(50, 52, true) == ReadbackState::Ready);
}
static void test_uv_submission() {
  using namespace hair_primary;
  CHECK(readback_state(20, 20, false) == ReadbackState::Wait);
  CHECK(readback_state(20, 22, false) == ReadbackState::Drop); // never match old style samples
  CHECK(readback_state(20, 22, true) == ReadbackState::Ready);
}
static void test_count_submission() {
  using namespace hair_primary;
  CHECK(readback_state(0, 1, true) == ReadbackState::Wait);
  CHECK(readback_state(0, 2, false) == ReadbackState::Drop); // neither success nor failure evidence
  CHECK(readback_state(0, 2, true) == ReadbackState::Ready);
}
static void test_gbuffer_depth_latch() {
  hair_primary::DepthLatch latch;
  CHECK(!latch.dropped(7, 100)); // refused prepass: cards plus probe
  latch.prepass(8, 100);
  CHECK(!latch.dropped(7, 100)); // another draw key cannot authorize a swap
  for (uint64_t f = 100; f <= 102; ++f) CHECK(latch.dropped(8, f));
  CHECK(!latch.dropped(8, 99));
  CHECK(!latch.dropped(8, 103));
}
static void test_prepass_drop_decision() {
  using namespace hair_primary;
  constexpr uint64_t now = 100, key = 7;
  for (uint64_t applied : {now, now + 1}) { // prepass before/after G-buffer recording
    DepthLatch latch;
    CHECK(prepass_drop_allowed(applied, now, true, true, latch.allow_prepass(key, now)));
    latch.prepass(key, now);
    CHECK(latch.dropped(key, now + 1));
    latch.observe_pipeline(key, now, false, false);
    CHECK(!prepass_drop_allowed(applied, now, true, true, latch.allow_prepass(key, now)));
  }
  CHECK(!prepass_drop_allowed(now - 1, now, true, true, true));
  CHECK(!prepass_drop_allowed(now + 2, now, true, true, true));
  CHECK(!prepass_drop_allowed(0, now, true, true, true));
  CHECK(!prepass_drop_allowed(now, now, false, true, true));
  CHECK(!prepass_drop_allowed(now, now, true, false, true));
  CHECK(!prepass_drop_allowed(now, now, true, true, false));
  // hv7: equality with now rejected all 187 draws when apply was now + 1.
  unsigned drops = 0;
  for (unsigned i = 0; i < 187; ++i)
    drops += prepass_drop_allowed(now + 1, now, true, true, true);
  CHECK(drops == 187);
}
// 2026-11-07 hair-noswap: the G-buffer swaps only after this key's prepass dropped its card depth, the prepass drops
// only after a G-buffer swap; with every other gate passing, neither went first (0 prepass, 0 G-buffer strand draws).
// A G-buffer that passed every gate but the drop marks the key ready; the next prepass drops, then the G-buffer swaps.
static unsigned bootstrap_swaps(bool mark_ready, unsigned frames, unsigned* first_swap) {
  hair_primary::SwapGate gate;
  hair_primary::DepthLatch latch;
  const uint64_t key = 0x483b;
  unsigned swaps = 0;
  *first_swap = 0;
  for (uint64_t f = 100; f < 100 + frames; ++f) {
    // Prepass of frame f (the game records it before the build that publishes f).
    if (gate.allow_prepass(key) && hair_primary::prepass_drop_allowed(f, f, true, true, latch.allow_prepass(key, f)))
      latch.prepass(key, f);
    gate.publish(&key, 1, f);
    // G-buffer of frame f: every other gate (coverage, order, slot, variant) passes in this fixture.
    if (!gate.allow_gbuffer(key)) continue;
    if (latch.dropped(key, f)) {
      gate.gbuffer_drew(key);
      if (!swaps++) *first_swap = unsigned(f - 100);
    } else if (mark_ready) {
      gate.gbuffer_ready(key);
    }
    CHECK(!latch.needs_card_depth(key, f, latch.dropped(key, f)));   // a dropped latch always meets a strand draw here
  }
  return swaps;
}
static void test_prepass_gbuffer_bootstrap() {
  unsigned first = 0;
  CHECK(bootstrap_swaps(false, 300, &first) == 0);   // the deadlock as on main ce8ce8e7
  CHECK(bootstrap_swaps(true, 300, &first) >= 290);  // ready -> prepass drop -> strand G-buffer, then every frame
  CHECK(first <= 3);
  // Velocity still follows only a real G-buffer strand draw; a ready key alone never enables it.
  hair_primary::SwapGate g;
  const uint64_t k = 9;
  g.publish(&k, 1, 1); g.publish(&k, 1, 2);
  g.gbuffer_ready(k);
  g.publish(&k, 1, 3);
  CHECK(g.allow_prepass(k) && !g.allow_velocity(k));
  g.publish(&k, 1, 4);
  CHECK(!g.allow_prepass(k));                        // readiness lasts one publish
  g.gbuffer_ready(k);
  g.clear();
  g.publish(&k, 1, 5); g.publish(&k, 1, 6);
  CHECK(!g.allow_prepass(k));                        // a reset drops readiness
}
// rt_hair_primary_hold (2026-11-07 hair-flicker): draw 9861, about 430 card pixels, measured 0.833..0.925 per window
// around the 0.891 bound. The strict gate flips strands/cards with the count noise; the hold keeps a swapping key on
// strands while each measurement is inside the bounds widened by its own noise, and still drops it on a real mismatch.
static void test_coverage_hold() {
  using namespace hair_primary;
  CHECK(std::fabs(cover_sigma(8556, 9397) - 0.01923694227747121) < 2e-4);   // the offline footprint: the bound's sigma
  CHECK(cover_sigma(379, 430) > 0.08 && cover_sigma(379, 430) < 0.095);     // 9861's footprint
  CHECK(kHoldFrames == kFreshFrames + 16);
  const uint64_t key = 0x9861;
  const uint32_t strand[] = {398, 358, 396, 365, 379, 389, 360, 371, 402, 384};   // 0.926, 0.833, ... of 430
  for (bool hold : {false, true}) {
    CoverageGate c;
    c.hold = hold;
    unsigned swapped = 0, resumes = 0;
    bool was = false;
    uint64_t f = 100;
    c.observe(key, f, 430, 398);   // the first measurement in bounds: both start swapping
    for (int i = 0; i < 60; ++i, ++f) {
      if (i % 2 == 0) c.observe(key, f, 430, strand[(i / 2) % 10]);   // a measurement every other frame
      const bool on = c.allow(key, f + 1);
      if (on) c.swapped(key, f + 1), ++swapped;
      resumes += on && !was && i;
      was = on;
    }
    if (hold) CHECK(swapped == 60 && resumes == 0);
    else CHECK(swapped < 60 && resumes > 0);   // the strict gate flickers on the same measurements
  }
  {   // A real mismatch, an invalidation and an aged measurement still return a held key to cards.
    CoverageGate c;
    c.hold = true;
    c.observe(key, 100, 430, 398);
    CHECK(c.allow(key, 101));
    c.swapped(key, 101);
    c.observe(key, 102, 430, 260);                    // 0.60: outside the widened bound
    CHECK(c.why(key, 103) == CoverageGate::Why::Low);
    c.observe(key, 104, 430, 398);
    c.swapped(key, 105);
    c.invalidate(key, 104);
    CHECK(c.why(key, 105) == CoverageGate::Why::Invalidated);
    c.observe(key, 106, 430, 398);
    c.swapped(key, 105 + kHoldFrames);   // still swapping two frames before the age limit
    CHECK(c.allow(key, 106 + kHoldFrames));
    c.swapped(key, 106 + kHoldFrames);
    CHECK(c.why(key, 107 + kHoldFrames) == CoverageGate::Why::Stale);
    // A key not swapping: the strict bound and freshness, as without the hold.
    c.observe(7, 100, 430, 365);                      // 0.849
    CHECK(c.why(7, 101) == CoverageGate::Why::Low);
    c.observe(8, 100, 430, 398);
    CHECK(c.allow(8, 100 + kFreshFrames) && !c.allow(8, 101 + kFreshFrames));
    // An empty measurement (no card pixels) keeps the previous one with the hold, replaces it without.
    c.observe(8, 105, 0, 0);
    CHECK(c.latest.at(8).card == 430);
    CoverageGate strict;
    strict.observe(8, 100, 430, 398); strict.observe(8, 105, 0, 0);
    CHECK(strict.why(8, 105) == CoverageGate::Why::Stale);
  }
}
// rt_hair_coverage_footprint: every measurement, also a first swap, against the bound at its own footprint.
// 2026-11-07 live draws: 9861 0.842 of 443, 23a3 0.792 of 610, 5381 0.314 of 51 and 0.674 of 46 (refused strictly).
static void test_coverage_footprint() {
  using namespace hair_primary;
  CoverageGate strict, fp;
  fp.footprint = true;
  struct M { uint64_t key, card, strand; bool fp_ok; };
  const M ms[] = {{1, 443, 373, true}, {2, 610, 483, false}, {3, 51, 16, false}, {4, 46, 31, false},
                  {5, 9397, 8400, true}, {6, 9397, 8300, false}, {7, 430, 260, false}};
  for (const M& m : ms) {
    strict.observe(m.key, 100, m.card, m.strand);
    fp.observe(m.key, 100, m.card, m.strand);
    const bool s = strict.allow(m.key, 101), f = fp.allow(m.key, 101);
    CHECK(f == m.fp_ok);
    CHECK(!s || f);                                   // never stricter than the strict bound
  }
  // At the offline footprint the two bounds agree (sigma 0.0192): 8400/9397 = 0.894 passes both, 8300 = 0.883 neither.
  CHECK(strict.allow(5, 101) && !strict.allow(6, 101));
  CHECK(fp.why(2, 101) == CoverageGate::Why::Low && fp.why(7, 101) == CoverageGate::Why::Low);
  CHECK(fp.why(1, 101 + kFreshFrames + 1) == CoverageGate::Why::Stale);   // freshness unchanged
}
// rt_hair_groom_primary_current: the G-buffer is recorded before this frame's build (so only the previous publish is
// known then); at vkQueueSubmit this frame's publish is used only when its build is ordered first on the GPU.
// 5h strand dropouts: the classification of a swapped draw's counted frame, and the camera gate's header test. The
// sequence: rt_after_capture stores the build camera with frame_index F; a present on the queue thread advances
// frame_index to F + 1 before hair_primary_publish (inside the same build) asks rt_this_build_camera(F + 1): false, the
// header goes out without kFlagCamera, every strand fragment discards in hp_rebase, but the swap gates (coverage, depth
// latch, applied keys) never read the flag: the draw is swapped, its card depth already dropped -> no hair that frame.
static void test_dropout() {
  using namespace hair_primary;
  CHECK(dropout_reason(5000, 4800, 5000, 9000, 8000) == Dropout::None);
  CHECK(dropout_reason(5000, 600, 5000, 9000, 8000) == Dropout::None);            // 12%: low coverage, not a dropout
  CHECK(dropout_reason(5000, 400, 5000, 9000, 8000) == Dropout::Rejected);
  CHECK(dropout_reason(5000, 0, 5000, 0, 0) == Dropout::NoTrace);                // hp_rebase false: nothing traced
  CHECK(dropout_reason(5000, 0, 5000, 9000, 0) == Dropout::Missed);
  CHECK(dropout_reason(0, 0, 5000, 0, 0) == Dropout::NoMarker);                  // the frame's tag left no marker
  CHECK(dropout_reason(0, 0, 10, 0, 0) == Dropout::None);                        // never had evidence
  CHECK(dropout_reason(30, 0, 30, 0, 0) == Dropout::None);                       // too few card pixels to judge
  for (int d = 0; d < int(Dropout::Count); ++d) CHECK(std::strcmp(dropout_name(Dropout(d)), "?") != 0);
  Header h{};
  h.tag = 7; h.width = 4; h.height = 4; h.flags = kFlagTips;
  uint8_t bytes[kHeaderBytes] = {};
  const Row rows[1] = {};
  pack(h, rows, 0, bytes);
  CHECK(!header_usable(bytes));               // the raced publish: no build camera
  h.flags |= kFlagCamera;
  pack(h, rows, 0, bytes);
  CHECK(header_usable(bytes) && header_flags(bytes) == (kFlagTips | kFlagCamera));
  GbFrame f;
  CHECK(f.swapped_slots == 0);
}

static void test_current_pick() {
  using namespace hair_primary;
  // One submit [prepass + build, G-buffer]: the build runs first -> this frame's publish.
  CHECK(current_pick(true, false, 0, 1, true) == CurrentPick::Current);
  // The build submitted in an earlier vkQueueSubmit.
  CHECK(current_pick(true, true, -1, 0, true) == CurrentPick::Current);
  // [G-buffer, build] in one submit, the build in the G-buffer's own command buffer (recorded after it), or a later
  // submit: the record-time (previous) publish stays.
  CHECK(current_pick(true, false, 1, 0, true) == CurrentPick::AfterGbuffer);
  CHECK(current_pick(true, false, 0, 0, true) == CurrentPick::AfterGbuffer);
  CHECK(current_pick(true, false, -1, 0, true) == CurrentPick::AfterGbuffer);
  CHECK(current_pick(false, true, 0, 1, true) == CurrentPick::NotRecorded);
  CHECK(current_pick(true, false, 0, 1, false) == CurrentPick::Incompatible);
  // Same grooms in the same draw slots, rows in any order; a new, missing or moved groom is incompatible.
  uint64_t a[kMaxGrooms] = {11, 22, 33}, b[kMaxGrooms] = {33, 11, 22}, c[kMaxGrooms] = {11, 22}, d[kMaxGrooms] = {11, 22, 44};
  DrawSlots sa, sb;
  sa.publish(a, 3); sb = sa;
  CHECK(same_grooms(a, sa, b, sb));
  CHECK(!same_grooms(a, sa, c, sb) && !same_grooms(c, sa, a, sb) && !same_grooms(a, sa, d, sb));
  DrawSlots moved;
  moved.publish(b, 3);   // the same keys put into slots in another order
  CHECK(moved.find(33) != sa.find(33) && !same_grooms(a, sa, b, moved));
  CHECK(std::strcmp(current_pick_name(CurrentPick::AfterGbuffer), "build after the G-buffer") == 0);
}
// Swap refusal tally (period line) and the coverage gate's refusal reason.
static void test_refusal_tally() {
  using namespace hair_primary;
  CoverageGate c;
  CHECK(c.why(1, 10) == CoverageGate::Why::None);
  c.observe(1, 10, 100, 95);
  CHECK(c.why(1, 10) == CoverageGate::Why::Ok && c.allow(1, 10));
  CHECK(c.why(1, 11 + kFreshFrames) == CoverageGate::Why::Stale);
  c.observe(2, 10, 100, 80);
  CHECK(c.why(2, 10) == CoverageGate::Why::Low);
  c.observe(3, 10, 100, 120);
  CHECK(c.why(3, 10) == CoverageGate::Why::High);
  c.invalidate(1, 10);
  CHECK(c.why(1, 10) == CoverageGate::Why::Invalidated && !c.allow(1, 10));
  RefusalTally t;
  char out[256];
  t.format(out);
  CHECK(std::strcmp(out, " none") == 0);
  t.add(0, coverage_refusal(CoverageGate::Why::Low)); t.add(0, coverage_refusal(CoverageGate::Why::Low));
  t.add(0, Refusal::NotDropped); t.add(2, Refusal::Strike); t.add(3, Refusal::Strike); t.add(1, Refusal::Count);
  t.format(out);
  CHECK(std::strcmp(out, " G-buffer: coverage low 2, card depth not dropped 1; prepass: strike 1") == 0);
  t.reset(); t.format(out);
  CHECK(std::strcmp(out, " none") == 0);
}
static void test_card_accepted_totals() {
  uint32_t cards[256] = {}, accepted[256] = {}, ct[4], at[4];
  cards[0] = 10; cards[1] = 5; accepted[0] = 3;
  cards[64] = 8; accepted[64] = 7; accepted[255] = 2;
  hair_primary::count_marker_totals(cards, accepted, ct, at);
  CHECK(ct[0] == 15 && at[0] == 3);
  CHECK(ct[1] == 8 && at[1] == 7);
  CHECK(ct[2] == 0 && at[2] == 0 && ct[3] == 0 && at[3] == 2);
}
static void test_period_failures() {
  hair_primary::PeriodFailures failures;
  ++failures.refused; ++failures.feedback; ++failures.feedback;
  CHECK(failures.refused == 1 && failures.feedback == 2);
  failures.reset();
  CHECK(failures.refused == 0 && failures.feedback == 0);
  ++failures.feedback;
  CHECK(failures.refused == 0 && failures.feedback == 1);
}

// M5 participation: swapped draws only in the main view; other views and instanced hair are card-only groups.
static void test_participation() {
  hair_primary::Participation p;
  CHECK(p.card_only_groups() == 0);
  using P = hair_primary::Participation;
  p.draw(P::kGbuffer, true, false); p.swapped(P::kGbuffer);
  p.draw(P::kVelocity, true, false); p.swapped(P::kVelocity);
  p.draw(P::kPrepass, true, false); p.swapped(P::kPrepass);
  CHECK(p.card_only_groups() == 0);               // every main pass with hair has strand draws
  p.draw(P::kGbuffer, true, false);               // a kept card draw next to a swapped one: still strands in the pass
  CHECK(p.card_only_groups() == 0 && p.main_total[P::kGbuffer] == 2 && p.main_swapped[P::kGbuffer] == 1);
  p.draw(P::kGbuffer, false, false);              // a portrait G-buffer draw
  p.draw(P::kPrepass, false, false);
  CHECK(p.card_only_groups() == 2 && p.other_total[P::kGbuffer] == 1);
  p.draw(P::kGbuffer, true, true);                // instanced hair: counted apart, never in the main totals
  CHECK(p.card_only_groups() == 3 && p.instanced == 1 && p.main_total[P::kGbuffer] == 2);
  hair_primary::Participation q;
  q.draw(P::kVelocity, true, false);              // main velocity with no swap
  q.draw(7, true, false);                         // an unknown kind is ignored
  CHECK(q.card_only_groups() == 1);
}

// M2.2 cascade card drop: every condition is required; the publish must be this or the previous frame.
static void test_cascade_card_drop() {
  using hair_primary::cascade_card_drop;
  CHECK(cascade_card_drop(true, true, true, true));
  CHECK(!cascade_card_drop(false, true, true, true));   // key off: cards stay in the game's shadow map
  CHECK(!cascade_card_drop(true, false, true, true));   // not a sun cascade (local atlas, prepass): untouched
  CHECK(!cascade_card_drop(true, true, false, true));   // no published groom for the key: its cards cast
  CHECK(!cascade_card_drop(true, true, true, false));   // no clone (library/dynamic depth): native draw
  using hair_primary::publish_fresh;
  CHECK(!publish_fresh(0, 10));
  CHECK(publish_fresh(10, 10) && publish_fresh(9, 10) && publish_fresh(11, 10));
  CHECK(!publish_fresh(8, 10) && !publish_fresh(12, 10));
}

int main() {
  test_cascade_card_drop();
  test_participation();
  test_upload_submission(); test_compaction_submission(); test_uv_submission(); test_count_submission();
  test_gbuffer_depth_latch(); test_prepass_drop_decision(); test_card_accepted_totals(); test_period_failures();
  test_prepass_gbuffer_bootstrap(); test_refusal_tally(); test_coverage_hold(); test_dropout();
  test_current_pick(); test_coverage_footprint();
  {   // C2 shadow transmittance: the row word (10 bits per channel, bit 31 = measured) and the mean over samples.
    using namespace hair_primary;
    const float a[3] = {0.25f, 0.5f, 1.0f}, nan3[3] = {NAN, -1.0f, 2.0f};
    const uint32_t w = pack_transmit(a);
    CHECK((w >> 31) == 1u && (w & 1023u) == 256u && ((w >> 10) & 1023u) == 512u && ((w >> 20) & 1023u) == 1023u);
    CHECK(pack_transmit(nan3) == (1u << 31 | 1023u << 20));   // NaN and below 0 -> 0, above 1 -> 1
    TransmitMean m;
    CHECK(m.packed() == 0u);   // not measured: the strands block
    m.add(a);
    CHECK(m.packed() == w);    // the first sample is the mean
    const float b2[3] = {0.75f, 0.5f, 0.0f};
    for (int i = 0; i < 400; ++i) m.add(b2);
    CHECK(std::fabs(m.a[0] - 0.75f) < 1e-3f && std::fabs(m.a[2]) < 1e-3f);   // converges to a steady colour
  }
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
  {   // rt_hair_groom_primary_order: the header the G-buffer pass applies (newest publish ordered before it on the GPU)
    using hair_primary::Staged;
    const Staged st[3] = {{10, 100, 0xA}, {11, 101, 0xB}, {12, 102, 0xC}};
    auto none = [](uint64_t) { return false; };
    auto upto101 = [](uint64_t r) { return r <= 101; };
    CHECK(hair_primary::pick_publish(st, 3, 0xC, none) == 2);      // this frame's build recorded in the same command buffer
    CHECK(hair_primary::pick_publish(st, 3, 0xD, upto101) == 1);   // another buffer: the newest already submitted
    CHECK(hair_primary::pick_publish(st, 3, 0xD, none) == -1);     // nothing ordered before it: no strand swap
    const Staged empty[2] = {};
    CHECK(hair_primary::pick_publish(empty, 2, 0, upto101) == -1);
  }
  {   // rebase (hp_rebase): the strand passes of frame N trace frame N-1's camera-relative groom from frame N's camera
    CHECK(hair_primary::kFlagCamera != hair_primary::kFlagTips && hair_primary::kFlagPrevCamera != hair_primary::kFlagCamera &&
          hair_primary::kPrevCameraOffset == hair_primary::kCameraOffset + 16 && hair_primary::kCameraOffset + 32 == hair_primary::kHeaderTotal);
    CHECK(hair_primary::kCameraOffset >= hair_primary::kColourOffset + hair_primary::kMaxGrooms * hair_primary::kColourBytes);
    const double world[3] = {-640.25, 3.5, -190.75}, build[3] = {-642.4, 1.8, -195.3}, now[3] = {-642.1, 1.8, -194.9};
    double org[3], dir[3], len = 0;
    for (int c = 0; c < 3; ++c) { org[c] = now[c] - build[c]; dir[c] = world[c] - now[c]; len += dir[c] * dir[c]; }
    len = std::sqrt(len);
    bool same = true;
    for (int c = 0; c < 3; ++c) same = same && std::fabs(org[c] + dir[c] / len * len - (world[c] - build[c])) < 1e-9;
    CHECK(same);   // origin + t * direction (this camera's ray) lands on the point in the build camera's frame
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
    publish(0xA);   // the first image (startup latch: the first publish is inside the first level load)
    CHECK(load.quiet(frame) && load.quiet(frame + hair_primary::kLoadQuietFrames - 2));
    frame += hair_primary::kLoadQuietFrames;
    CHECK(!load.quiet(frame));
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
  { // Per-draw coverage evidence: neither publication nor an unavailable/newer pair proves coverage.
    using namespace hair_primary;
    CoverageGate g;
    const uint64_t a = 0xA, b = 0xB;
    CHECK(kFreshFrames == kCoverageLatency + kStrandLatency + 2);
    CHECK(!g.allow(a, 10));
    g.observe(a, 10, 1000, 900);
    CHECK(g.allow(a, 10, 0.9));
    CHECK(g.allow(a, 10 + kFreshFrames, 0.9));
    CHECK(!g.allow(a, 11 + kFreshFrames, 0.9));
    CHECK(!g.allow(a, 9, 0.9));
    CHECK(!g.allow(b, 10, 0.9));
    g.observe(a, 11, 1000, 899);
    CHECK(!g.allow(a, 11, 0.9)); // newest failure immediately brings cards back
    g.observe(a, 9, 1000, 1000);
    CHECK(!g.allow(a, 11, 0.9)); // late older success cannot replace newest failure
    g.observe(a, 12, 0, 1000);
    CHECK(!g.allow(a, 12, 0.9));
    g.observe(a, 13, 1000, 901);
    CHECK(g.allow(a, 13, 0.9)); // recovery has no strike/retry delay
    g.invalidate(a, 14);
    CHECK(!g.allow(a, 14, 0.9));
    CHECK(g.latest.at(a).card == 1000 && g.latest.at(a).strand == 901); // logs retain actual query counts
    g.observe(a, 14, 1000, 1000);
    CHECK(!g.allow(a, 14, 0.9)); // partial same-frame successes cannot qualify an invalidated key
    g.observe(a, 15, 1000, 901);
    CHECK(g.allow(a, 15, 0.9));
    g.clear();
    CHECK(!g.allow(a, 15, 0.9));
  }
  { // Derived bound: minimum-ratio batch view, minus one-frame count uncertainty.
    using namespace hair_primary;
    const double c = 9396.798707214493, s = 8556.228538087944;
    const double ratio = s / c;
    const double noise = (std::sqrt(s) + ratio * std::sqrt(c)) / c;
    CHECK(std::abs(ratio - 0.9105471772550376) < 1e-14);
    CHECK(std::abs(noise - 0.01923694227747121) < 1e-14);
    CHECK(std::abs(kCoverMin - (ratio - noise)) < 1e-14);
    CHECK(kCoverMin > 0.0 && kCoverMin < ratio);
    CoverageGate g;
    const uint64_t card = 1000000, below = uint64_t(kCoverMin * card);
    g.observe(0xA, 20, card, below);
    CHECK(!g.allow(0xA, 20));
    g.observe(0xA, 21, card, below + 1);
    CHECK(g.allow(0xA, 21));
    g.observe(0xA, 22, card, 0);
    CHECK(!g.allow(0xA, 22));
    g.observe(0xA, 23, card, below + 1);
    CHECK(g.allow(0xA, 23));
    g.observe(0, 23, card, card);
    CHECK(!g.allow(0, 23));
    // Upper side: the maximum offline ratio plus the same noise; strands covering clearly more than the cards (hv7
    // 2026-11-04: 2.55 from overdrawn samples) keep the cards.
    CHECK(std::abs(kCoverMax - (1.0752640637962045 + noise)) < 1e-14);
    const uint64_t above = uint64_t(kCoverMax * card) + 1;
    g.observe(0xA, 24, card, above);
    CHECK(!g.allow(0xA, 24));
    g.observe(0xA, 25, card, above - 2);
    CHECK(g.allow(0xA, 25));
    g.observe(0xA, 26, 93085, 237443);
    CHECK(!g.allow(0xA, 26));
  }
  {   // The count may run on the GPU before the last G-buffer passes: the record holds an older tag's entries (hv7
      // 2026-11-04: tag - 2). The evidence comes from the newest tag with entries, never from an empty newest tag.
    using namespace hair_primary;
    const uint32_t two_behind[kCountTags] = {0, 0, 166019, 0};
    CHECK(newest_counted_tag(two_behind) == 2);
    const uint32_t current[kCountTags] = {120, 90, 0, 0};
    CHECK(newest_counted_tag(current) == 0);
    const uint32_t none[kCountTags] = {0, 0, 0, 0};
    CHECK(newest_counted_tag(none) == -1);
    CHECK(kTagCountOffset == 1872 && kCardCountOffset == kTagCountOffset + kCountTags * kMaxGrooms * 4 && kCountBytes == kCardCountOffset + kCountTags * kMaxGrooms * 4);
  }
  { // Real ordering: prepass(N), G-buffer(N), readback three publishes later.
    using namespace hair_primary;
    constexpr uint64_t key = 0xABCD;
    GbFrame frames[8] = {};
    CoverageGate gate;
    DepthLatch latch;
    bool ambiguous = false;
    frames[0].frame = 100; frames[0].tag = 99; frames[0].keys[0] = key;
    CHECK(unique_frame(frames, 8, 99, ambiguous) == frames && !ambiguous);
    frames[1] = frames[0]; frames[1].frame = 101;
    CHECK(!unique_frame(frames, 8, 99, ambiguous) && ambiguous);
    CHECK(!unique_frame(frames, 8, 98, ambiguous) && !ambiguous);
    gate.observe(key, 100, 1000, 1000); // arrives at publish 103
    CHECK(gate.allow(key, 103));
    for (uint64_t n = 104; n < 108; ++n) {
      const bool prepass = gate.allow(key, n);
      if (prepass) latch.prepass(key, n);
      const bool strands = gate.allow(key, n) && n != 104;
      CHECK(!strands);
      const bool fallback = latch.needs_card_depth(key, n, strands);
      CHECK(!latch.orphan(key, n, strands, fallback));
      if (n == 104) gate.observe(key, 101, 1000, 1944);
      CHECK(!gate.allow(key, n));
    }
    gate.observe(key, 100, 1000, 1000);
    CHECK(!gate.allow(key, 108));
  }
  { // Frame latch: both recording orders, interleaving, hitch and sibling pipelines.
    using namespace hair_primary;
    constexpr uint64_t key = 0xABCD, F = 100;
    for (uint64_t present : {0ULL, 1ULL}) {
      DepthLatch latch;
      // G-buffer F can precede prepass F, consuming prepass F-1 instead.
      latch.prepass(key, F - 1);
      CHECK(latch.needs_card_depth(key, F, false));
      CHECK(!latch.orphan(key, F, false, true));
      latch.prepass(key, F);
      CHECK(latch.needs_card_depth(key, F + present, false));
      CHECK(!latch.orphan(key, F + present, false, true));
      CHECK(latch.needs_card_depth(key, F + present, false)); // sibling obligation retained
      CHECK(!latch.orphan(key, F + present, true, false));
      latch.prepass(key, F + 1); // next prepass before late G-buffer draws
      CHECK(latch.needs_card_depth(key, F + 1, false));
      CHECK(latch.needs_card_depth(key, F + 3, false));
      CHECK(!latch.needs_card_depth(key, F + 4, false));
      latch.prune(F + 900); // hitch: no synthetic orphan from missing draws
      CHECK(latch.keys.empty());
      CHECK(!latch.orphan(key, F + 900, false, false));
    }
    DepthLatch latch;
    latch.observe_pipeline(key, F, false, false); // uncovered sibling
    latch.observe_pipeline(key, F, true, true); // covered sibling cannot erase it
    CHECK(!latch.allow_prepass(key, F));
    CHECK(!latch.allow_prepass(key, F + 300));
    CHECK(latch.allow_prepass(key, F + 301));
    CHECK(latch.allow_prepass(key + 1, F));
    if (latch.allow_prepass(key, F)) latch.prepass(key, F);
    CHECK(!latch.orphan(key, F, false, false));
    latch.prepass(key + 1, F); // true native draw without fallback: exactly one
    unsigned orphans = latch.orphan(key + 1, F + 2, false, false);
    orphans += latch.orphan(key + 1, F + 2, false, true);
    orphans += latch.orphan(key + 1, F + 2, true, false);
    CHECK(orphans == 1);
    std::puts("depth latch: recording orders, interleaving, hitch, uncovered sibling, true orphan passed");
  }
  { // hv9 aggregate fixture: 3 keys x 60 frames at 30 fps; no draw-time orphan.
    using namespace hair_primary;
    DepthLatch latch;
    unsigned prepass = 0, gbuffer = 0, velocity = 0, orphans = 0;
    for (uint64_t f = 2600; f < 2660; ++f) {
      latch.prune(f);
      for (uint64_t key : {0xe338ULL, 0x833dULL, 0xdfc7ULL}) {
        CHECK(latch.allow_prepass(key, f));
        latch.prepass(key, f); ++prepass;
        orphans += latch.orphan(key, f + 1, true, false); ++gbuffer; ++velocity;
      }
    }
    CHECK(prepass == 180 && gbuffer == 180 && velocity == 180 && orphans == 0);
    std::puts("hv9 aggregate fixture: 180/180/180 draws, 0 orphans (not a per-draw log replay)");
  }
  { // Compact draw keys: duplicates share, live slots survive reorder/removal, overflow keeps cards.
    using namespace hair_primary;
    DrawSlots slots;
    uint64_t live[18];
    for (uint32_t k = 0; k < 17; ++k) live[k] = 100 + k;
    live[17] = live[0];
    slots.publish(live, 18);
    for (uint32_t k = 0; k < kDrawSlots; ++k) CHECK(slots.find(live[k]) == k);
    CHECK(slots.find(live[16]) == kDrawSlots && slots.find(0) == kDrawSlots);
    const auto snapshot = slots;
    std::swap(live[0], live[15]);
    slots.publish(live, 18);
    CHECK(slots.find(100) == 0 && slots.find(115) == 15);
    live[3] = 0;
    slots.publish(live, 18);
    CHECK(slots.find(116) == 3 && slots.find(103) == kDrawSlots);
    CHECK(slots.find(100) == 0 && slots.find(115) == 15);
    CHECK(snapshot.find(103) == 3 && snapshot.find(116) == kDrawSlots);
  }
  { // C++ model of the shader count: one packed pair per pixel, four newest masked tags.
    using namespace hair_primary;
    struct Marker { uint32_t card = 0, accepted = 0; };
    CHECK(coverage_bytes(2560, 1067) == uint64_t(2560) * 1067 * 24);
    Marker pixel[6] = {};
    auto fragment = [&](uint32_t slot, uint32_t x, uint32_t tag, bool accepted) {
      const uint32_t word = (tag << 8) | slot;
      pixel[x].card = word;
      if (accepted) pixel[x].accepted = word;
    };
    fragment(0, 0, 0x1000064, true);
    fragment(0, 0, 0x1000064, false); // same-owner rejection retains acceptance
    fragment(0, 1, 0x1000064, true);
    fragment(1, 1, 0x1000064, false); // overlapping key owns card, old acceptance cannot count
    fragment(0, 2, 0x1000063, true);
    fragment(1, 3, 0x1000062, true);
    fragment(1, 4, 0x1000061, false);
    fragment(1, 5, 0x1000060, true); // outside four-tag window
    uint32_t card[4][kDrawSlots] = {}, accepted[4][kDrawSlots] = {};
    auto count = [&](uint32_t tag) {
      for (const auto& m : pixel) for (uint32_t j = 0; j < 4; ++j) {
        const uint32_t t = tag - j, slot = m.card & 255u;
        if ((t & 0xFFFFFFu) && slot < kDrawSlots && (m.card >> 8) == (t & 0xFFFFFFu)) {
          ++card[j][slot];
          accepted[j][slot] += m.accepted == m.card;
        }
      }
    };
    count(0x1000064);
    CHECK(card[0][0] == 1 && accepted[0][0] == 1);
    CHECK(card[0][1] == 1 && accepted[0][1] == 0);
    CHECK(card[1][0] == 1 && accepted[1][0] == 1);
    CHECK(card[2][1] == 1 && accepted[2][1] == 1);
    CHECK(card[3][1] == 1 && accepted[3][1] == 0);
    fragment(1, 1, 0x1000064, true);
    CHECK(pixel[1].accepted == pixel[1].card); // both words follow last writer
    fragment(0, 0, 0x1000065, false);
    CHECK(pixel[0].accepted != pixel[0].card); // stale acceptance
    for (auto& m : pixel) m = {};
    fragment(15, 0, 0x1000000, true); // wrapped tags are excluded, including untouched zero markers
    fragment(16, 1, 0x1000000, true); // invalid slot ignored
    count(0x1000000);
    CHECK(card[0][15] == 0 && accepted[0][15] == 0);
    CHECK(card[0][0] == 1); // unchanged by the untouched zero markers
  }
  std::printf("hair_groom_asset_test: all passed\n");
  return 0;
}
