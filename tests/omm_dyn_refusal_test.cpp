// common/omm_dyn_refusal.h (rt_omm_dyn_retry_frames) and common/log_budget.h (fault-range log budget).
#include "../common/log_budget.h"
#include "../common/omm_dyn_refusal.h"
#include "../common/omm_dyn_evict.h"
#include <cstdio>
#include <string>
#define CHECK(x) do { if (!(x)) { std::printf("FAIL line %d: %s\n", __LINE__, #x); return 1; } } while (0)
int main() {
  using bg3::omm_dyn::Refusal;
  {
    Refusal r;
    CHECK(r.allow(1) && r.allow(1u << 20));                 // nothing remembered: every bake is tried
    r.refused(500, 10, 1000, 64);
    CHECK(!r.allow(500) && !r.allow(501) && r.allow(499));  // larger or equal: proven refused; smaller: tried
    r.refresh(11, 1000, 64);
    CHECK(!r.allow(500));                                    // same live bytes: still proven
    r.refresh(12, 2000, 64);
    CHECK(!r.allow(500));                                    // more live bytes: still proven
    r.refused(200, 12, 2000, 64);
    CHECK(!r.allow(200) && r.allow(199));                    // a smaller refusal lowers the bound
    r.refresh(12 + 63, 2000, 64);
    CHECK(!r.allow(200));
    r.refresh(12 + 64, 2000, 64);
    CHECK(r.allow(200) && r.allow(1u << 20));                // retry period over: everything tried again
    r.refused(300, 100, 1000, 64);
    r.refresh(101, 999, 64);
    CHECK(r.allow(300));                                     // the backend freed bytes: the proof is gone
  }
  {
    Refusal r;                                               // retry_frames 0: the old behaviour (no memory)
    r.refused(500, 10, 1000, 0);
    CHECK(r.allow(500));
    r.refused(500, 10, 1000, 64);
    r.refresh(11, 1000, 0);
    CHECK(r.allow(500));
  }
  {
    bg3::log_budget::Budget b(2, 600);
    std::string s;
    CHECK(b.admit("A", 1, &s) && s.empty());
    CHECK(b.admit("B", 2, &s) && s.empty());
    std::string tag = "A";                                   // tags are copied, compared by value
    CHECK(!b.admit(tag.c_str(), 3, &s) && s.empty());
    CHECK(!b.admit("A", 4, &s));
    CHECK(!b.admit("C", 5, &s));
    CHECK(b.admit("A", 600, &s));                            // new period: summary of the old one, lines again
    CHECK(s == "3 more in frames 0-600: A x2, C x1");
    CHECK(b.admit("A", 601, &s) && s.empty());
    CHECK(!b.admit("A", 602, &s));
    CHECK(b.admit("A", 1300, &s) && s == "1 more in frames 600-1300: A x1");
    CHECK(b.admit("A", 1301, &s) && s.empty());              // a period without suppressed lines: no summary
    CHECK(b.admit("A", 1, &s));                              // frame counter reset: a new period
  }
  {
    using bg3::omm_dyn::EvictCandidate;
    using bg3::omm_dyn::pick_evictions;
    const std::vector<EvictCandidate> all = {
        {1, 100, true, false},   // unused 900 frames: evictable
        {2, 50, true, false},    // unused 950 frames: evictable, oldest
        {3, 950, true, false},   // used 50 frames ago: kept
        {4, 10, true, true},     // a per-draw BLAS was built with it: kept
        {5, 10, false, false},   // bake not run yet: kept (its own path discards it)
        {6, 100, true, false}};  // same age as 1: key order
    auto v = pick_evictions(all, 1000, 600, 64);
    CHECK(v.size() == 3 && v[0] == 2 && v[1] == 1 && v[2] == 6);
    v = pick_evictions(all, 1000, 600, 2);
    CHECK(v.size() == 2 && v[0] == 2 && v[1] == 1);          // at most `max`, least recently used first
    CHECK(pick_evictions(all, 1000, 0, 64).empty());          // key off: never
    CHECK(pick_evictions(all, 1000, 600, 0).empty());
    CHECK(pick_evictions(all, 650, 600, 64).size() == 1);     // exactly N frames unused: 2 only (650 - 50 = 600)
  CHECK(pick_evictions(all, 649, 600, 64).empty());
    CHECK(pick_evictions({{7, 2000, true, false}}, 1000, 600, 64).empty());   // frame counter reset: kept
  }
  std::printf("omm_dyn_refusal_test ok\n");
  return 0;
}
