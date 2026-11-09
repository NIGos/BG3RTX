// RTX Hair live switch (docs/RTX_HAIR_PRIMARY.md 5m): the CPU state machine of layer/hair_primary.cpp's strand swaps.
//
// The startup part (the strand header and the strand pipeline variants) is made on every capable device; the live key
// (rt_hair, and the bundle keys it gates in layer/util.cpp) only decides, frame by frame, whether matched hair draws
// swap to strands. The game records frame F+1 while F is in flight, and the settings change at a present, so one
// frame may have recorded its depth prepass with strands (card depth dropped) and its G-buffer after the switch:
//  - Off at frame F: no swap from the first draw recorded after the switch; a G-buffer draw whose prepass dropped its
//    card depth (DepthLatch, through prepass frame + 2) still restores it with the fallback pipeline, so cards come back
//    whole in the same frame or the next. BLAS/TLAS: the next build leaves the grooms out (rt_hair_groom reads 0); a
//    frame already recorded keeps the TLAS it was recorded with.
//  - On at frame R: publishes staged before R (their TLAS may be refit or freed) are never applied, and the swap gate
//    restarts empty, so the first swaps need two fresh publishes (SwapGate), exactly as after a level load.
#pragma once
#include <cstdint>

namespace bg3::hair_live {

enum class Edge : uint8_t { None, On, Off, Unavailable };

struct Toggle {
  bool on = false;            // strands may swap (reserved and requested)
  bool refused = false;       // requested without the startup part: reported once
  uint64_t resume = 0;        // frame_index of the last On edge
  uint64_t off_at = 0;        // frame_index of the last Off edge
  uint32_t ons = 0, offs = 0;

  // Once per frame (any number of calls per frame is fine): the requested state and whether the startup part exists.
  Edge observe(bool requested, bool reserved, uint64_t frame) {
    const bool want = requested && reserved;
    if (requested && !reserved) {
      if (refused) return Edge::None;
      refused = true;
      return Edge::Unavailable;
    }
    if (!requested) refused = false;
    if (want == on) return Edge::None;
    on = want;
    if (on) { resume = frame; ++ons; return Edge::On; }
    off_at = frame; ++offs;
    return Edge::Off;
  }
  // A publish staged at `publish_frame` may be applied to a G-buffer: strands on and staged after the last On edge.
  bool publish_usable(uint64_t publish_frame) const { return on && publish_frame >= resume; }
  // A native hair draw must restore its card depth (fallback pipeline): strands off and its prepass inside the latch.
  bool restore_depth(uint64_t frame, uint64_t latch_until) const { return !on && frame <= latch_until; }
  // The draw hooks run (swap or restore) this frame.
  bool hooks(uint64_t frame, uint64_t latch_until) const { return on || frame <= latch_until; }
};

}  // namespace bg3::hair_live
