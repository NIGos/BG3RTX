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
  // BG3 records separate command buffers before submitting them. Publication
  // of N must survive the recording of N+1, including secondary producer IDs.
  PostHistoryProducer history[2];
  history[0].record(100);                         // secondary, frame N
  history[1].record(101);                         // frame N+1 before submit N
  if (!check(!history[0].readable() && !history[1].readable())) return 1;
  history[0].submit(90);                          // primary ID is not the producer
  if (!check(!history[0].readable())) return 1;
  history[0].submit(100);                         // executed secondary snapshot
  if (!check(history[0].readable() && !history[1].readable())) return 1;
  history[1].submit(101);
  if (!check(history[0].readable() && history[1].readable())) return 1;
  for (uint64_t n = 102; n < 300; ++n) {
    auto& output = history[n % 2];
    auto& previous = history[1 - n % 2];
    if (!check(previous.readable())) return 1;    // steady-state history remains valid
    output.record(n);
    if (!check(previous.readable() && !output.readable())) return 1;
    previous.discard(n - 1);                     // re-begin submitted recording
    if (!check(previous.readable())) return 1;
    output.submit(n);
    if (!check(output.readable())) return 1;
  }
  history[0].record(400); history[1].record(401);
  history[1].discard(401);                        // discarded newer producer
  history[0].submit(400);
  history[1].submit(401);                         // cannot revive a discarded recording
  if (!check(history[0].readable() && !history[1].readable())) return 1;
  history[1].record(402); history[1].discard(401); // old reset cannot cancel new begin
  history[1].submit(402);
  if (!check(history[1].readable())) return 1;
  PostHistoryProducer reused;
  reused.record(1); reused.submit(1);
  reused.record(2); reused.submit(2);
  if (!check(reused.readable())) return 1;
  reused.submit(1); // an older executable writer overwrites the slot
  if (!check(!reused.readable())) return 1;
  DescriptorRecordingLease descriptors[256];
  for (auto& descriptor : descriptors) {
    descriptor.record(500);
    if (!check(!descriptor.reusable(true))) return 1;
    descriptor.discard(499);
    if (!check(!descriptor.reusable(true))) return 1;
    descriptor.discard(500);
    if (!check(!descriptor.reusable(false) && descriptor.reusable(true))) return 1;
  }
  return 0;
}
