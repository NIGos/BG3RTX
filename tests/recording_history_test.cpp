#include "../common/recording_history.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <initializer_list>
int main() {
  bg3::RecordingHistory h;
  // A discarded first recording cannot publish UNDEFINED->GENERAL or parity.
  assert(h.begin(1)); h.ready = true; h.parity ^= 1;
  assert(h.begin(1)); assert(!h.begin(2));
  h.resolve(false, true);
  assert(!h.ready && h.parity == 0 && !h.owner);
  assert(h.begin(2)); h.ready = true; h.parity ^= 1;
  h.resolve(true, false);
  assert(h.ready && h.parity == 1 && !h.owner);
  // Submission, not GPU completion, publishes GPU-only ping-pong state.
  for (uint64_t frame = 3; frame < 40; ++frame) {
    assert(h.begin(frame));
    const auto old = h.parity;
    h.parity ^= 1;
    h.resolve(true, false);
    assert(h.ready && h.parity == (old ^ 1));
  }
  // A stalled/discarded later recording restores previously submitted history.
  const auto old = h.parity;
  assert(h.begin(100)); h.parity ^= 1;
  for (unsigned i = 0; i < 20; ++i) { h.resolve(false, false); assert(!h.begin(101)); }
  h.resolve(false, true); assert(h.ready && h.parity == old);
}
