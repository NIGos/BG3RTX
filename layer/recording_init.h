#pragma once
#include <cstdint>
#include <vector>
#include <algorithm>

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

// Executable recordings may be resubmitted even after GPU completion.
struct DescriptorRecordingLease {
  uint64_t recording = 0;
  bool reusable(bool complete) const { return !recording && complete; }
  void record(uint64_t owner) { recording = owner; }
  void discard(uint64_t owner) { if (recording == owner) recording = 0; }
};

// Keep each ping-pong producer independently. Recording the next frame must
// neither erase the older pending submission nor block the current dispatch.
struct PostHistoryProducer {
  uint64_t recording = 0;
  bool submitted = false;
  std::vector<uint64_t> writers;
  void record(uint64_t owner) {
    recording = owner; submitted = false;
    if (owner && std::find(writers.begin(), writers.end(), owner) == writers.end()) writers.push_back(owner);
  }
  void submit(uint64_t owner) {
    // An older executable recording can be resubmitted after a newer writer.
    // Its pixels no longer match the latest metadata: invalidate that slot.
    if (owner && std::find(writers.begin(), writers.end(), owner) != writers.end()) submitted = owner == recording;
  }
  void discard(uint64_t owner) {
    std::erase(writers, owner);
    if (owner == recording && !submitted) recording = 0;
  }
  bool readable() const { return recording && submitted; }
};
