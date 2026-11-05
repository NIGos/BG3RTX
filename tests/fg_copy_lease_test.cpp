#include "../common/fg_copy_lease.h"
#include "../layer/recording_init.h"
#include <cstdio>

int main() {
  FgCopyLease slots[2];
  auto check = [](bool ok) { if (!ok) std::puts("FG copy lifetime failed"); return ok; };
  if (!check(fg_copy_for_present(4809, 4810))) return 1;
  if (!check(!fg_copy_for_present(4808, 4810) && !fg_copy_for_present(4810, 4810))) return 1;
  if (!check(!fg_copy_for_present(UINT64_MAX, 0))) return 1;
  if (!check(fg_copy_pair_for_present(4809, 4809, 4809, 4810))) return 1;
  // A current private copy must never be combined with past/future camera or color.
  if (!check(!fg_copy_pair_for_present(4809, 4808, 4809, 4810))) return 1;
  if (!check(!fg_copy_pair_for_present(4809, 4809, 4810, 4810))) return 1;
  if (!check(!fg_copy_pair_for_present(4808, 4808, 4808, 4810))) return 1;
  if (!check(!fg_copy_pair_for_present(UINT64_MAX, UINT64_MAX, UINT64_MAX, 0))) return 1;
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
  // A reset/free before submit restores an unused slot or its previous completed layout.
  FgCopyLease lease, previous;
  lease.write();
  if (!check(cancel_recorded_copy(true, lease, previous) && lease.reusable(false))) return 1;
  previous.write(); previous.presented(12); lease = previous; lease.write();
  if (!check(cancel_recorded_copy(true, lease, previous) && lease.value == 12 && lease.written)) return 1;
  lease.write();
  if (!check(!cancel_recorded_copy(false, lease, previous) && !lease.reusable(true))) return 1;
  RecordingInit init;
  if (!check(init.needed(1) && init.available(1))) return 1;
  init.record(1);
  if (!check(!init.needed(1) && !init.available(2))) return 1;
  init.discard(2);
  if (!check(!init.available(2))) return 1;
  init.discard(1);
  if (!check(init.needed(2) && init.available(2) && !init.ready)) return 1;
  init.record(2); init.submitted(1);
  if (!check(!init.ready)) return 1;
  init.submitted(2); init.discard(2);
  if (!check(init.ready && init.available(3) && !init.needed(3))) return 1;
  init = {};
  if (!check(!init.ready && init.needed(3))) return 1;
  // Ring wrap cannot overwrite an executable recording, even after completion.
  DescriptorRecordingLease descriptors[256];
  for (auto& descriptor : descriptors) descriptor.record(100);
  for (auto& descriptor : descriptors)
    if (!check(!descriptor.reusable(true))) return 1;
  descriptors[0].discard(99); // wrong generation/handle reuse
  if (!check(!descriptors[0].reusable(true))) return 1;
  descriptors[0].discard(100);
  // Reset releases the CPU recording, not the pending GPU consumer.
  if (!check(!descriptors[0].reusable(false) && descriptors[0].reusable(true))) return 1;
  descriptors[0].record(101);
  descriptors[0].discard(100);
  if (!check(!descriptors[0].reusable(true))) return 1;
  descriptors[0].discard(101); // discarded before submit, no GPU use
  if (!check(descriptors[0].reusable(true))) return 1;
  // Guide/history publication: a failed submit never calls submitted().
  init = {}; init.record(200);
  if (!check(!init.ready && !init.available(201))) return 1;
  init.discard(200); init.submitted(200); // reset then stale submit notification
  if (!check(!init.ready)) return 1;
  init.record(201); init.submitted(200);
  if (!check(!init.ready)) return 1;
  init.submitted(201); init.discard(201);
  if (!check(init.ready)) return 1;
  return 0;
}
