#pragma once
#include <cstdint>

// A held capture is usable only after submission, in the same static-camera
// epoch, and for at most `hold` absent frames. Never refresh age on a hold.
inline bool dyn_hold_alive(uint64_t now, uint64_t seen, uint32_t hold,
                           bool moving, bool submitted, bool same_epoch) {
  return hold && !moving && submitted && same_epoch && now > seen && now - seen <= hold;
}

// Snapshot ownership must follow the hold horizon, not the much longer BLAS
// cache lifetime. Returning true only queues retirement; GPU fences and old
// scene slots must still retire before the allocator can reuse the bytes.
inline bool dyn_hold_expired(uint64_t now, uint64_t seen, uint32_t hold) {
  return now > seen && now - seen > hold;
}

inline bool dyn_slot_expired(uint64_t now, uint64_t seen, uint32_t hold,
                             bool trim, bool hair) {
  return dyn_hold_expired(now, seen, trim && hair ? hold : 120u);
}

// Only ordinary position/UV recipes can discard native strand/frame/color
// payload. Held opaque instances have no hit-material metadata by contract.
inline uint32_t dyn_hold_stride(uint32_t captured, bool trim, bool alpha, uint32_t kind) {
  return trim && captured >= 88u && (!alpha || (kind >= 1u && kind <= 3u)) ? 24u : captured;
}
struct DynHoldPackJob { uint32_t source, destination, vertices, stride; };
static_assert(sizeof(DynHoldPackJob) == 16);
inline bool dyn_hold_pack_range(uint64_t offset, uint64_t vertices, uint32_t stride, uint64_t bytes) {
  return stride >= 16u && offset <= bytes && vertices <= (bytes - offset) / stride;
}
