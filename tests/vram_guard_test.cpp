// rt_vram_guard CPU policy (common/vram_guard.h): headroom math, hysteresis, per-card caps, yield order.
#include "../common/vram_guard.h"
#include "../common/native_hit_recycle.h"
#include <cstdio>
#include <cstdlib>
#include <vector>
using namespace bg3::vram_guard;
static unsigned checks;
static void check(bool ok, int line) {
  ++checks;
  if (!ok) { std::fprintf(stderr, "vram guard check at line %d failed\n", line); std::exit(1); }
}
#define CHECK(x) check((x), __LINE__)

int main() {
  RefusedRequests refused;
  for(int i=0;i<25;++i)refused.note(960*mib);
  CHECK(refused.count==25 && refused.peak==960*mib);
  refused.note(976*mib);refused.note(944*mib);
  CHECK(refused.count==27 && refused.peak==976*mib);
  CHECK(kRtCapture!=kCascade);
  // Required opaque capture uses real headroom, never simulated shares or LOW
  // hysteresis. A replacement reserves its full size, not just the growth delta.
  {
    Guard g;
    g.sample(17000 * mib, 15640 * mib);
    CHECK(g.low && !g.allow(64 * mib));
    CHECK(g.allow_capture(1024 * mib, 31419 * mib));
    CHECK(g.granted == 1024 * mib);
    CHECK(!g.allow_capture(13400 * mib, 31419 * mib));
    CHECK(g.granted == 1024 * mib);  // refusals reserve nothing
    CHECK(g.allow_capture((31419 - 17000 - 1024) * mib, 31419 * mib));
    CHECK(!g.allow_capture(1, 31419 * mib));
    CHECK(cap(kRtCapture, 11730 * mib) == kCapRef[kRtCapture].ref);
    g.sample(11900 * mib, 12000 * mib);
    CHECK(g.low); // within real budget, but below the cache-trimming slack
    CHECK(g.allow_capture(100 * mib, 12000 * mib));
    CHECK(!g.allow_capture(1, 12000 * mib));
    g.sample(12001 * mib, 12000 * mib);
    CHECK(!g.allow_capture(1, 12000 * mib));
  }
  // Normal and required consumers share outstanding reservations in both orders.
  {
    Guard g;
    g.sample(1000 * mib, 12000 * mib);
    CHECK(g.allow(2000 * mib));
    CHECK(g.allow_capture(8000 * mib, 12000 * mib));
    CHECK(!g.allow(701 * mib)); // 300 MB slack after all shared grants
    CHECK(g.allow(700 * mib));
    CHECK(!g.allow_capture(301 * mib, 12000 * mib));
    CHECK(g.allow_capture(300 * mib, 12000 * mib));
    CHECK(!g.allow(1));
    CHECK(!g.allow_capture(1, 12000 * mib));
    g.sample(1000 * mib, 12000 * mib);
    CHECK(g.granted == 0 && g.allow_capture(1000 * mib, 12000 * mib));
    CHECK(g.allow(1000 * mib));
  }
  {
    Guard g;
    CHECK(g.allow_capture(UINT64_MAX, 0)); // no driver extension/sample: unchanged
    g.sample(1, UINT64_MAX);
    CHECK(g.allow_capture(UINT64_MAX - 1, UINT64_MAX));
    CHECK(!g.allow_capture(1, UINT64_MAX));
    CHECK(!g.allow(1)); // ordinary arithmetic must not wrap either
    g.sample(0, UINT64_MAX);
    CHECK(!g.allow(UINT64_MAX));
  }
  // ---- headroom math ----
  // Slack: 2.5% of the budget (the evidence's 789 MB drop of 31419 MB), or a larger observed drop.
  CHECK(slack(31419 * mib, 0) == 31419 * mib / 1000 * 25);
  CHECK(slack(31419 * mib, 0) >= 785 * mib && slack(31419 * mib, 0) <= 789 * mib);
  CHECK(slack(31419 * mib, 1200 * mib) == 1200 * mib);
  // Capped at what the yielders can give back: 4224 + 2047 + 512 MB on the reference card.
  CHECK(slack_cap(31419 * mib) == 6783 * mib);
  CHECK(slack(31419 * mib, 15942 * mib) == 6783 * mib);
  CHECK(slack_cap(15477 * mib) == (2080 + 1008 + 252) * mib);
  // Margin: never below one BLAS chunk + slack; a 2047 MB pool step dominates.
  CHECK(margin(31419 * mib, 0, 0) == kMinStep + slack(31419 * mib, 0));
  CHECK(margin(31419 * mib, 2047 * mib, 0) == 2047 * mib + slack(31419 * mib, 0));

  // ---- the evidence run, replayed: 21992 of 31419 MB, then 31668 of 30630 MB ----
  {
    Guard g;
    CHECK(!g.sample(21992 * mib, 31419 * mib) && !g.low);
    CHECK(g.allow(544 * mib * 3));                        // the cascade grow fits then
    CHECK(g.allow(64 * mib));                             // a chunk
    CHECK(g.granted == 544 * mib * 3 + 64 * mib);
    // Over budget: low, the budget drop (789 MB) becomes the slack, every grow is refused.
    CHECK(g.sample(31668 * mib, 30630 * mib) && g.low);
    CHECK(g.recent_drop() == 789 * mib);
    CHECK(!g.allow(64 * mib));
    // Free down to used + 2 margins = budget.
    const uint64_t m = g.margin_now();
    CHECK(m == 544 * mib * 3 + 789 * mib);                // largest step this window + observed drop
    CHECK(g.trim_need() == 31668 * mib + 2 * m - 30630 * mib);
  }

  // ---- hysteresis: enter at used + margin > budget, leave at used + 2 margins <= budget ----
  {
    Guard g;
    const uint64_t B = 16000 * mib;
    g.sample(1000 * mib, B);
    const uint64_t m = g.margin_now();                    // 64 MB + 400 MB
    CHECK(m == 64 * mib + B / 1000 * 25);
    CHECK(!g.sample(B - m, B) && !g.low);                 // exactly one margin left: still normal
    CHECK(g.sample(B - m + mib, B) && g.low);             // less than one margin: low
    CHECK(!g.sample(B - m - 100 * mib, B) && g.low);      // a bit freed: stays low (no flapping)
    CHECK(g.trim_need() == (B - m - 100 * mib) + 2 * m - B);
    CHECK(!g.sample(B - 2 * m + mib, B) && g.low);        // just short of two margins
    CHECK(g.sample(B - 2 * m, B) && !g.low);              // far below: growth allowed again
    CHECK(g.trim_need() == 0);
    CHECK(g.allow(64 * mib));
  }

  // ---- per-request headroom in normal state: used + granted + bytes + slack <= budget ----
  {
    Guard g;
    const uint64_t B = 12000 * mib, s = slack(B, 0);
    g.sample(B - s - 3000 * mib, B);                      // normal (3 GB above slack)
    CHECK(!g.low);
    CHECK(g.allow(2000 * mib));
    CHECK(!g.allow(1001 * mib));                          // would cross budget - slack with the 2000 granted
    CHECK(g.allow(1000 * mib));                           // exactly fits
    CHECK(!g.allow(1));
    g.sample(B - s - 3000 * mib, B);                      // a new sample resets the grants
    CHECK(g.granted == 0);
  }

  // ---- the largest step decays over two windows, so one big pool grow does not keep the margin high ----
  {
    Guard g;
    g.sample(1000 * mib, 31419 * mib);
    g.allow(2047 * mib);
    CHECK(g.largest_step() == 2047 * mib);
    g.new_window();
    CHECK(g.largest_step() == 2047 * mib);                // the previous window still counts
    g.new_window();
    CHECK(g.largest_step() == kMinStep);
  }

  // ---- a budget drop is slack for two stats windows, not for the session ----
  {
    Guard g;
    g.sample(20000 * mib, 31419 * mib);
    g.sample(20000 * mib, 30630 * mib);
    CHECK(g.recent_drop() == 789 * mib);
    g.new_window();
    CHECK(g.recent_drop() == 789 * mib);                  // the previous window still counts
    g.new_window();
    CHECK(g.recent_drop() == 0 && g.drop_from == 0);
    CHECK(g.margin_now() == kMinStep + slack(30630 * mib, 0));
    // Two drops in a row, then the budget back at the level before the first: void at once.
    g.sample(20000 * mib, 30000 * mib);
    g.sample(20000 * mib, 29000 * mib);
    CHECK(g.recent_drop() == 1000 * mib && g.drop_from == 30630 * mib);
    g.sample(20000 * mib, 30000 * mib);
    CHECK(g.recent_drop() == 1000 * mib);                 // only part of it came back
    g.sample(20000 * mib, 30630 * mib);
    CHECK(g.recent_drop() == 0);
  }

  // ---- the user's run of 2026-11-13 13:41, replayed: 31419 -> 15477 -> 13522 MB and back ----
  // (bg3rtx.log: used 13153 of 31419, 13256 of 15477 "margin 16454 MB", 14747 of 13599, 13704 of 13522 MB.)
  {
    Guard g;
    auto trims = [&g] { return g.trim_ask() != 0; };
    CHECK(!g.sample(13153 * mib, 31419 * mib) && !g.low);
    CHECK(g.allow(512 * mib) && !trims());                // the largest step in the log's first margin
    g.sample(13256 * mib, 15477 * mib);                   // the halving: slack is capped, not 15942 MB
    CHECK(g.recent_drop() == 15942 * mib);
    CHECK(g.margin_now() == (512 + 3340) * mib);          // was 16454 MB
    CHECK(g.low && !g.starved && trims());                // 13256 + 3852 > 15477: yield once
    g.sample(14747 * mib, 13599 * mib);
    CHECK(g.low && g.starved);                            // really over the OS budget: refuse, say so
    CHECK(!g.allow(64 * mib));
    CHECK(trims());                                       // worse than what was acted on: one more ask
    g.sample(13704 * mib, 13522 * mib);
    CHECK(g.low && g.starved && !g.allow(64 * mib) && !trims());
    // The budget returns: growth allowed at that very sample.
    CHECK(g.sample(13704 * mib, 31419 * mib));
    CHECK(!g.low && !g.starved && g.recent_drop() == 0 && g.trim_need() == 0 && !trims());
    CHECK(g.allow(64 * mib) && g.allow(1728 * mib));      // a chunk and the 3 x 576 MB cascade grow
    // At the full budget growth is not refused while used + margin fits, whatever happened before:
    // the old guard refused everything above 31419 - 15942 = 15477 MB.
    for (uint64_t used = 14000; used <= 28000; used += 1000) {
      CHECK(!g.sample(used * mib, 31419 * mib) && !g.low);
      CHECK(g.allow(64 * mib));
    }
    // Real pressure at the full budget is still refused: used + margin above the budget.
    const uint64_t s = slack(31419 * mib, 0), m = g.margin_now();
    CHECK(m == 1728 * mib + s);
    CHECK(g.sample(31419 * mib - m + mib, 31419 * mib) && g.low && !g.starved && !g.allow(64 * mib));
    CHECK(g.sample(31419 * mib - 2 * m, 31419 * mib) && !g.low);
    CHECK(!g.allow(2 * m - s + mib));                     // would cross budget - slack
    CHECK(g.allow(2 * m - s));
  }
  // The budget stays down (another program holds the VRAM): the drop leaves the margin after two stats
  // windows, and Low is left as soon as the use fits the budget that is really there.
  {
    Guard g;
    g.sample(9000 * mib, 31419 * mib);
    g.sample(9000 * mib, 13522 * mib);
    CHECK(g.margin_now() == (64 + 2917) * mib);           // capped slack: 1817 + 880 + 220 MB
    CHECK(!g.low);                                        // 9000 + 2981 <= 13522
    g.sample(11000 * mib, 13522 * mib);
    CHECK(g.low);                                         // 11000 + 2981 > 13522
    g.new_window();
    g.sample(11000 * mib, 13522 * mib);
    CHECK(g.low);
    g.new_window();                                       // two windows: floor slack 338 MB, margin 402 MB
    CHECK(g.sample(11000 * mib, 13522 * mib) && !g.low && g.allow(64 * mib));
    CHECK(g.sample(13200 * mib, 13522 * mib) && g.low && !g.starved);   // 13200 + 402 > 13522: still protected
  }

  // ---- no thrash at a steady low budget: one ask per episode, again only when it got worse by a chunk ----
  {
    Guard g;
    g.sample(13000 * mib, 13522 * mib);
    CHECK(!g.low && !g.starved);
    unsigned asks = 0, starved_edges = 0;
    bool was = g.starved;
    for (int i = 0; i < 4000; ++i) {                      // ~18 minutes of samples at 60 fps
      g.sample((14700 + (i * 37) % 60) * mib, 13522 * mib);   // 14.7 GB wanted, up to 59 MB of game streaming
      if (i % 19 == 18) g.new_window();
      CHECK(g.low && !g.allow(64 * mib));
      asks += g.trim_ask() != 0;
      starved_edges += g.starved != was;
      was = g.starved;
    }
    CHECK(asks == 1 && starved_edges == 1 && g.starved);
    g.sample(14900 * mib, 13522 * mib);                   // a real step up: one more ask, for the whole need
    CHECK(g.trim_ask() == g.trim_need() && g.trim_ask() == 0);
    g.sample(14000 * mib, 13522 * mib);                   // what was freed does not re-arm the ask
    CHECK(g.trim_ask() == 0);
    g.sample(14900 * mib, 13522 * mib);
    CHECK(g.trim_ask() == 0 && g.starved);
    // Leaving Low ends the episode: the next episode asks again.
    CHECK(g.sample(9000 * mib, 13522 * mib) && !g.low && !g.starved && g.trim_asked == 0);
    CHECK(g.sample(13400 * mib, 13522 * mib) && g.low && !g.starved && g.trim_ask() != 0);
  }

  // ---- no budget known: unchanged behaviour ----
  {
    Guard g;
    CHECK(g.allow(uint64_t(8) << 30));
    CHECK(!g.sample(5000 * mib, 0) && !g.low && g.trim_need() == 0);
  }

  // ---- per-consumer caps per card (budget ~ 95.5% of the heap, as the 32 GB card: 31419 of 32187 MB) ----
  {
    struct Card { uint64_t gb, budget; };
    const Card cards[] = {{12, 11730 * mib}, {16, 15640 * mib}, {24, 23460 * mib}, {32, 31419 * mib}};
    uint64_t prev[kConsumerCount] = {};
    for (const Card& c : cards) {
      for (int i = 0; i < kConsumerCount; ++i) {
        const uint64_t v = cap(Consumer(i), c.budget);
        CHECK(v >= kCapRef[i].floor && v <= kCapRef[i].ref);   // never below the start size, never above today
        CHECK(v % mib == 0);
        CHECK(v >= prev[i]);                                    // monotonic in the budget
        prev[i] = v;
      }
      // Our growable consumers together stay within a fixed share of the card: BLAS + pool + 3 cascades + recycled.
      const uint64_t ours = cap(kAsChunks, c.budget) + cap(kMeshPool, c.budget) + 3 * cap(kCascade, c.budget) +
                            cap(kHitRecycle, c.budget);
      CHECK(ours * 100 <= c.budget * 42);   // 12 GB: 1576 + 764 + 3 x 764 + 191 = 4823 MB = 41.1%, the same share as the 32 GB caps (12924 MB)
    }
    // The reference card keeps today's caps exactly.
    CHECK(cap(kAsChunks, 31419 * mib) == 66 * 64 * mib);
    CHECK(cap(kMeshPool, 31419 * mib) == 2047 * mib);
    CHECK(cap(kCascade, 31419 * mib) == 2047 * mib);
    CHECK(cap(kHitRecycle, 31419 * mib) == 512 * mib);
    // 12 GB: the pool stops at 764 MB (2047 x 11730 / 31419), BLAS chunks at 1576 MB (24 full chunks), not 4.2 GB.
    CHECK(cap(kMeshPool, 11730 * mib) == 764 * mib);
    CHECK(cap(kAsChunks, 11730 * mib) == 1576 * mib);
    CHECK(cap(kCascade, 11730 * mib) == 764 * mib);
    CHECK(cap(kHitRecycle, 11730 * mib) == 191 * mib);
    // 16 GB.
    CHECK(cap(kMeshPool, 15640 * mib) == 1018 * mib);
    CHECK(cap(kAsChunks, 15640 * mib) == 2102 * mib);
    // A tiny budget: floors (the default sizes every card ran before the guard).
    CHECK(cap(kMeshPool, 2000 * mib) == 256 * mib && cap(kCascade, 2000 * mib) == 427 * mib);
    CHECK(cap(kAsChunks, 2000 * mib) == 22 * 64 * mib && cap(kHitRecycle, 2000 * mib) == 32 * mib);
    // Unknown budget: the reference caps (unchanged behaviour).
    CHECK(cap(kMeshPool, 0) == 2047 * mib);
  }

  // ---- who yields first: empty chunks, then recycled buffers, then LRU meshes; never more than asked ----
  {
    uint64_t ask[kYieldCount];
    const uint64_t have[kYieldCount] = {640 * mib, 384 * mib, 900 * mib};
    plan_trim(500 * mib, have, ask);
    CHECK(ask[kYieldEmptyChunks] == 500 * mib && ask[kYieldRecycled] == 0 && ask[kYieldMeshLru] == 0);
    plan_trim(800 * mib, have, ask);
    CHECK(ask[kYieldEmptyChunks] == 640 * mib && ask[kYieldRecycled] == 160 * mib && ask[kYieldMeshLru] == 0);
    plan_trim(1500 * mib, have, ask);
    CHECK(ask[kYieldEmptyChunks] == 640 * mib && ask[kYieldRecycled] == 384 * mib && ask[kYieldMeshLru] == 476 * mib);
    // More than everything: each gives all it has (live need is not in `have`), the rest is refused growth.
    plan_trim(9000 * mib, have, ask);
    CHECK(ask[0] == have[0] && ask[1] == have[1] && ask[2] == have[2]);
    plan_trim(0, have, ask);
    CHECK(ask[0] == 0 && ask[1] == 0 && ask[2] == 0);
  }

  // ---- the recycled pool's cap follows the budget (native_hit::recycle::Pool::set_cap) ----
  {
    using namespace bg3::native_hit::recycle;
    Pool<int> pool;
    std::vector<int> evict;
    for (int i = 0; i < 6; ++i) pool.give(Key{64ull << 20, 1, false}, i, uint64_t(i), evict);
    CHECK(evict.empty() && pool.retained() == 384ull << 20);
    pool.set_cap(191ull << 20, evict);                    // the 12 GB card's share
    CHECK(pool.retained() <= 191ull << 20 && evict.size() == 4 && evict[0] == 0);   // the oldest go first
    pool.set_cap(0, evict);
    CHECK(pool.retained() == 0 && pool.size() == 0);
  }

  std::printf("vram_guard_test: %u checks passed\n", checks);
  return 0;
}
