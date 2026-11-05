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

// A descriptor remains immutable while its executable recording exists, even
// after GPU completion (the application may resubmit it). Reset releases the
// recording, but never substitutes for the lifetime tracker's completion proof.
struct DescriptorRecordingLease {
  uint64_t recording = 0;
  bool reusable(bool gpu_complete) const { return !recording && gpu_complete; }
  void record(uint64_t owner) { recording = owner; }
  void discard(uint64_t owner) { if (recording == owner) recording = 0; }
};
