#include "../common/fg_stability.h"
#include <initializer_list>

int main() {
  // Healthy startup and the exact existing 30-frame warmup boundary.
  FgStability legacy, scoped;
  for (uint64_t f=1; f<60; ++f)
    if (legacy.update(f,true,30) || scoped.update(f,true,30)) return 1;
  if (!legacy.update(60,true,30) || !scoped.update(60,true,30)) return 2;

  // Retiring a rotated-out resource does not make a current input invalid.
  if (!legacy.destroyed(100,0,false) || scoped.destroyed(100,0,true)) return 3;
  if (legacy.update(100,true,30) || !scoped.update(100,true,30)) return 4;
  for (uint64_t f=101; f<160; ++f) {
    if (legacy.update(f,true,30) || !scoped.update(f,true,30)) return 5;
  }
  if (!legacy.update(160,true,30)) return 6;

  // Every real role (depth, MV, hudless, UI), including aliased roles, still
  // forces cooldown. A rediscovered replacement cannot bypass it.
  uint64_t at=200;
  for (uint32_t roles : {1u,2u,4u,8u,12u}) {
    if (!scoped.destroyed(at,roles,true)) return 7;
    if (scoped.update(at,false,30)) return 8;
    for (uint64_t f=at+1; f<at+60; ++f)
      if (scoped.update(f,true,30)) return 9;
    if (!scoped.update(at+60,true,30)) return 10;
    at+=100;
  }

  // Missing or stale inputs independently keep FG off, even without a destroy.
  if (scoped.update(700,false,30)) return 11;
  for (uint64_t f=701; f<731; ++f)
    if (scoped.update(f,true,30)) return 12;
  if (!scoped.update(731,true,30)) return 13;
  FgStability immediate;
  if (!immediate.update(1,true,0) || immediate.update(2,false,0)) return 14;
  return 0;
}
