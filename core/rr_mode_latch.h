#pragma once
// The GI's Ray Reconstruction mode (full resolution, gi_rr_rays, no accumulation of its own) follows
// Bg3HostApi::rr_active, which is true while RR evaluated within the last 3 presents. A loading screen is
// longer than that: the first lit frame after it saw "inactive", the GI images were reallocated at half
// resolution and again at full resolution on the next frame (two `gi: resources` lines per level load in the
// play logs of 2026-11-14), with the history cleared twice. RR does denoise that first frame (its evaluate
// follows in the same frame), so the mode is held while RR is inactive for less than kHoldFrames presents,
// counted from the first frame the GI itself sees it inactive. A real fallback (SR, FSR, TAA) is permanent:
// it switches after the hold. kHoldFrames: ten times the host's 3-present window.
#include <cstdint>

namespace bg3 {

struct RrModeLatch {
  static constexpr uint64_t kHoldFrames = 30;
  bool mode = false;
  bool inactive = false;         // RR was seen inactive while in RR mode, first at frame inactive_since
  uint64_t inactive_since = 0;

  // forced: the gi_rr key (-1 follows RR, 0 off, 1 on). active: rr_active(). frame: frame_index().
  bool update(int forced, bool active, uint64_t frame) {
    if (forced >= 0 || active || !mode) {
      mode = forced >= 0 ? forced != 0 : active;
      inactive = false;
      return mode;
    }
    if (!inactive) { inactive = true; inactive_since = frame; }
    if (frame >= inactive_since && frame - inactive_since < kHoldFrames) return true;
    mode = inactive = false;
    return false;
  }
};

}  // namespace bg3
