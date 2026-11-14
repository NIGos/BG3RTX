#include "../core/rr_mode_latch.h"

int main() {
  using bg3::RrModeLatch;
  const uint64_t hold = RrModeLatch::kHoldFrames;
  {  // Never active: off from the first frame (no hold before RR ran once).
    RrModeLatch l;
    for (uint64_t f = 1; f < 200; ++f)
      if (l.update(-1, false, f)) return 1;
  }
  {  // A loading screen: many presents without a GI frame, then one inactive frame, then active again.
    RrModeLatch l;
    if (!l.update(-1, true, 100)) return 2;
    if (!l.update(-1, false, 5000)) return 3;   // first lit frame after the load: held
    if (!l.update(-1, false, 5000)) return 4;   // several reads in one frame agree
    if (!l.update(-1, true, 5001) || l.inactive) return 5;
    // The next gap gets its own full hold.
    if (!l.update(-1, false, 5002) || !l.update(-1, false, 5002 + hold - 1)) return 6;
  }
  {  // A permanent fallback switches exactly after the hold, and stays off.
    RrModeLatch l;
    l.update(-1, true, 10);
    for (uint64_t f = 11; f < 11 + hold; ++f)
      if (!l.update(-1, false, f)) return 7;
    if (l.update(-1, false, 11 + hold)) return 8;
    if (l.update(-1, false, 12 + hold)) return 9;
    if (!l.update(-1, true, 13 + hold)) return 10;   // RR back: on at once
  }
  {  // The key overrides both ways, with no hold.
    RrModeLatch l;
    if (!l.update(1, false, 1) || l.update(0, true, 2)) return 11;
    l.update(-1, true, 3);
    if (l.update(0, false, 4)) return 12;
    if (l.update(-1, false, 5)) return 13;   // forced off cleared the mode: no hold afterwards
  }
  {  // Frame 0 and a frame counter that went back do not hold for ever.
    RrModeLatch l;
    l.update(-1, true, 0);
    if (!l.update(-1, false, 0)) return 14;
    if (!l.update(-1, false, hold - 1) || l.update(-1, false, hold)) return 15;
    l.update(-1, true, 500);
    l.update(-1, false, 501);
    if (l.update(-1, false, 400)) return 16;
  }
  return 0;
}
