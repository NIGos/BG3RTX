#pragma once
#include <cstdint>

// A recorded upload is usable by its own recording. Publish it to other
// recordings only after a successful queue submission.
struct RecordingInit {
  bool ready = false;
  uint64_t pending = 0;
  bool available(uint64_t recording) const { return ready || !pending || pending == recording; }
  bool needed(uint64_t recording) const { return !ready && pending != recording; }
  void record(uint64_t recording) { pending = recording; }
  void submitted(uint64_t recording) { if (pending == recording && recording) { ready = true; pending = 0; } }
  void discard(uint64_t recording) { if (pending == recording) pending = 0; }
};

// Cancellation restores the prior image layout/completion lease. A submitted
// copy remains owned by the GPU and cannot be cancelled by a later reset.
template<class Lease>
bool cancel_recorded_copy(bool unsubmitted, Lease& current, const Lease& previous) {
  if (!unsubmitted) return false;
  current = previous;
  return true;
}
