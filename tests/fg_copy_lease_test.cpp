#include "../common/fg_copy_lease.h"
#include <cstdio>

int main() {
  FgCopyLease slots[2];
  auto check = [](bool ok) { if (!ok) std::puts("FG copy lifetime failed"); return ok; };
  if (!check(fg_copy_for_present(4809, 4810))) return 1;
  if (!check(!fg_copy_for_present(4808, 4810) && !fg_copy_for_present(4810, 4810))) return 1;
  if (!check(!fg_copy_for_present(UINT64_MAX, 0))) return 1;
  // Switching the key off between recording and present leaves a copy
  // untagged. The following present's fence still covers its earlier write.
  if (!check(fg_copy_precedes_present(4809, 4810) && fg_copy_precedes_present(4808, 4810))) return 1;
  if (!check(!fg_copy_precedes_present(4810, 4810) && !fg_copy_precedes_present(UINT64_MAX, 4810))) return 1;
  // Recording runs ahead of present. Neither an unsubmitted write nor a
  // submitted frame awaiting its SDK fence may be overwritten.
  slots[0].write(); slots[1].write();
  if (!check(!slots[0].reusable(true) && !slots[1].reusable(false))) return 1;
  slots[0].presented(41);
  if (!check(!slots[0].reusable(false) && slots[0].reusable(true))) return 1;
  // Recycling invalidates the previous present's completed fence.
  slots[0].write();
  if (!check(!slots[0].reusable(true))) return 1;
  slots[0].presented(0);
  if (!check(!slots[0].reusable(true))) return 1;
  slots[1].presented(42);
  if (!check(slots[1].reusable(true) && !slots[0].reusable(true))) return 1;
  slots[0].presented(43); // retirement of an earlier submitted, unselected copy
  if (!check(!slots[0].reusable(false) && slots[0].reusable(true))) return 1;
  return 0;
}
