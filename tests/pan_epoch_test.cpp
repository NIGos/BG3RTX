#include "../common/pan_epoch.h"
#include <cstdio>

int main() {
  PanEpoch e;
  auto check = [](bool ok) { if (!ok) std::puts("pan epoch check failed"); return ok; };
  if (!check(!e.matches(7, 11))) return 1;
  e.bind(7, 1768, 11);
  // The present counter can advance to 1769 while this recording continues.
  if (!check(e.matches(7, 11) && e.frame == 1768)) return 1;
  if (!check(!e.matches(8, 11) && !e.matches(7, 12))) return 1;
  e.clear();
  if (!check(!e.matches(7, 11))) return 1;
  e.bind(8, 1769, 12);
  return check(e.matches(8, 12) && e.frame == 1769) ? 0 : 1;
}
