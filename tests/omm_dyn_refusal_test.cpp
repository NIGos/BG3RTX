// common/omm_dyn_refusal.h (rt_omm_dyn_retry_frames) and common/log_budget.h (fault-range log budget).
#include "../common/log_budget.h"
#include "../common/omm_dyn_refusal.h"
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
  std::printf("omm_dyn_refusal_test ok\n");
  return 0;
}
