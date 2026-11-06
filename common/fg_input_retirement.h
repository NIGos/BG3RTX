#pragma once
#include <cstdint>

// Protected by the integration's fence mutex. A pending tag cannot retire
// against an older global value. A later valid present may conservatively
// publish its fence for an earlier tag whose state query failed.
struct FgInputUse {
  uint64_t frame = 0, fence = 0, value = 0;
  bool pending = false, idle = false, invalidated = false;
  void tag(uint64_t at) { frame=at; fence=value=0; pending=true; idle=false; }
  void publish(uint64_t through_frame, uint64_t semaphore, uint64_t completion) {
    if (!invalidated && pending && frame<=through_frame && semaphore && completion) {
      fence=semaphore; value=completion; pending=false;
    }
  }
  void boundary(bool completed) {
    const bool proven=completed || idle;
    fence=value=0; pending=!proven; idle=proven; invalidated=!proven;
  }
  bool known() const { return !invalidated && !pending && fence && value; }
  bool same(const FgInputUse& other) const {
    return frame==other.frame && fence==other.fence && value==other.value && pending==other.pending && idle==other.idle && invalidated==other.invalidated;
  }
};

enum class FgRetirePolicy { Global, Exact, Defer, Complete };
inline FgRetirePolicy fg_retire_policy(bool exact, const FgInputUse* use,
                                       bool global_available) {
  // A new SDK semaphore cannot certify consumption by an expired old one,
  // even if the user switches the pacing candidate back off.
  if (use && use->invalidated) return FgRetirePolicy::Defer;
  if (!exact) return FgRetirePolicy::Global;
  if (use && use->idle) return FgRetirePolicy::Complete;
  if (use && use->pending) return FgRetirePolicy::Defer;
  if (use && use->known()) return FgRetirePolicy::Exact;
  return global_available ? FgRetirePolicy::Global : FgRetirePolicy::Defer;
}
