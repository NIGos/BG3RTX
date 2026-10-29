#include "../common/dyn_hold.h"
int main() {
  for (uint64_t age = 1; age <= 4; ++age)
    if (!dyn_hold_alive(100 + age, 100, 4, false, true, true)) return 1;
  if (dyn_hold_alive(105, 100, 4, false, true, true)) return 2;
  if (dyn_hold_alive(101, 100, 0, false, true, true)) return 3;
  if (dyn_hold_alive(101, 100, 4, true, true, true)) return 4;
  if (dyn_hold_alive(101, 100, 4, false, false, true)) return 5;
  if (dyn_hold_alive(101, 100, 4, false, true, false)) return 6;
  if (dyn_hold_alive(100, 100, 4, false, true, true) ||
      dyn_hold_alive(99, 100, 4, false, true, true)) return 7;
  // Reappearing draws refresh age; holds themselves do not.
  if (!dyn_hold_alive(107, 103, 4, false, true, true)) return 8;
  return 0;
}
