#pragma once
#include <cstdint>

// A held capture is usable only after submission, in the same static-camera
// epoch, and for at most `hold` absent frames. Never refresh age on a hold.
inline bool dyn_hold_alive(uint64_t now, uint64_t seen, uint32_t hold,
                           bool moving, bool submitted, bool same_epoch) {
  return hold && !moving && submitted && same_epoch && now > seen && now - seen <= hold;
}
