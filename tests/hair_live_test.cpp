// common/hair_live.h: the RTX Hair live switch with the strand swap's depth latch (common/hair_primary.h DepthLatch),
// as layer/hair_primary.cpp drives them: on, off, and on again while frames recorded under the old state are in flight.
#include "../common/hair_live.h"
#include "../common/hair_primary.h"

#include <cstdio>

namespace {
int g_fail = 0;
#define CHECK(x) do { if (!(x)) { std::printf("FAIL line %d: %s\n", __LINE__, #x); ++g_fail; } } while (0)
using bg3::hair_live::Edge;
using bg3::hair_live::Toggle;

// One recorded frame of one hair draw K: the prepass (recorded before the switch may flip) and the G-buffer (after).
struct Frame { bool prepass_dropped = false, gbuffer_strands = false, gbuffer_restored = false; };
}  // namespace

int main() {
  constexpr uint64_t K = 0xabcdef0123456789ull;
  Toggle t;
  hair_primary::DepthLatch latch;
  uint64_t latch_until = 0;
  auto prepass = [&](uint64_t f) {   // hair_primary_swap's prepass swap: card depth dropped, latch through f + 2
    if (!t.on) return false;
    latch.prepass(K, f);
    if (latch_until < f + 2) latch_until = f + 2;
    return true;
  };
  auto gbuffer = [&](uint64_t f) {   // the G-buffer draw of the same key: strands, or the card with its depth restored
    Frame r;
    if (t.on) { r.gbuffer_strands = true; return r; }
    r.gbuffer_restored = t.restore_depth(f, latch_until) && latch.needs_card_depth(K, f, false);
    return r;
  };

  // Off at startup: nothing runs, no edge.
  CHECK(t.observe(false, true, 1) == Edge::None);
  CHECK(!t.hooks(1, latch_until));

  // On at frame 10. A publish staged before the switch (frame 9) is never applied; frame 10's is.
  CHECK(t.observe(true, true, 10) == Edge::On);
  CHECK(t.observe(true, true, 10) == Edge::None);   // any number of calls per frame
  CHECK(t.resume == 10 && t.ons == 1);
  CHECK(!t.publish_usable(9));
  CHECK(t.publish_usable(10) && t.publish_usable(11));
  CHECK(t.hooks(10, latch_until));

  // Frames 11 and 12 with strands: each prepass drops the card depth, the G-buffer draws strands.
  for (uint64_t f = 11; f <= 12; ++f) {
    CHECK(t.observe(true, true, f) == Edge::None);
    CHECK(prepass(f));
    CHECK(gbuffer(f).gbuffer_strands);
  }
  CHECK(latch_until == 14);

  // Off between frame 12's prepass and its G-buffer (the settings change at a present while 12 is still recording its
  // later passes, so the G-buffer is recorded as frame 13's switch state): the card comes back with its depth.
  CHECK(t.observe(false, true, 13) == Edge::Off);
  CHECK(t.off_at == 13 && t.offs == 1);
  CHECK(!t.publish_usable(12));                       // no strand header after the switch
  {
    const Frame g = gbuffer(13);
    CHECK(!g.gbuffer_strands && g.gbuffer_restored);
  }
  CHECK(!prepass(13));                                // no new drop while off
  CHECK(t.hooks(13, latch_until) && t.hooks(14, latch_until));   // the draw hook stays on through the latch

  // On again at frame 14, while frame 12's card-depth drop is still inside the latch (frames in flight): the publish of
  // frame 12 (made before the off switch, its TLAS may be refit or freed) is never applied, even though it is only two
  // frames old; strands resume with frame 14's publish.
  CHECK(t.observe(true, true, 14) == Edge::On);
  CHECK(t.resume == 14 && t.ons == 2);
  CHECK(!t.publish_usable(12) && !t.publish_usable(13));
  CHECK(t.publish_usable(14));
  CHECK(!t.restore_depth(14, latch_until));           // on: the swap path decides (a refused swap restores itself)
  CHECK(prepass(14));
  CHECK(gbuffer(14).gbuffer_strands);
  CHECK(latch_until == 16);

  // Off at 20: past the latch nothing runs any more.
  for (uint64_t f = 15; f < 20; ++f) { CHECK(t.observe(true, true, f) == Edge::None); prepass(f); }
  CHECK(t.observe(false, true, 20) == Edge::Off);
  CHECK(gbuffer(20).gbuffer_restored);                // frame 19's drop reaches frame 20's G-buffer
  CHECK(gbuffer(21).gbuffer_restored);
  latch.prune(22);
  CHECK(!gbuffer(22).gbuffer_restored);
  CHECK(!t.hooks(22, latch_until));
  CHECK(t.offs == 2);

  // Requested without the startup part (no strand variants this session): reported once, never on.
  Toggle u;
  CHECK(u.observe(true, false, 5) == Edge::Unavailable);
  CHECK(u.observe(true, false, 6) == Edge::None);
  CHECK(!u.on && !u.publish_usable(6));
  CHECK(u.observe(false, false, 7) == Edge::None);
  CHECK(u.observe(true, false, 8) == Edge::Unavailable);   // asked again after an off: reported again

  if (g_fail) std::printf("%d failures\n", g_fail);
  else std::printf("hair_live_test: all checks passed\n");
  return g_fail ? 1 : 0;
}
