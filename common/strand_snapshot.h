// rr_hair_strand_aa (docs/RTX_HAIR_PRIMARY.md 5n): which frame's strand snapshot the RR-before-post resolve may read.
// layer/rrpost.cpp writes the snapshot at the end of the scene's G-buffer pass (and again at the decal pass with the
// same formats, same frame) into one of two images; the resolve of the same frame reads it only when that frame wrote
// it, else it runs the game's resolve and counts the frame. CTest hair_groom_asset_test test_strand_snapshot_ring.
#pragma once
#include <cstdint>

namespace bg3::strand_snapshot {

// The G-buffer's colour formats (VkFormat values: R16G16_SFLOAT 83, R8G8B8A8_UNORM 37 x 3) and D24_UNORM_S8_UINT (129):
// the pass ngx.cpp ngx_note_pass records the RR guides and the camera at (layer/ngx.cpp).
inline bool gbuffer_pass(uint32_t color_count, const int32_t* color_formats, int32_t depth_format) {
  return color_count >= 4 && color_formats[0] == 83 && color_formats[1] == 37 && color_formats[2] == 37 &&
         color_formats[3] == 37 && depth_format == 129;
}

// Frames in recording order, not frame_index(): frame_index() counts presents, and a present (another thread; fps cap,
// Reflex and frame generation move it) may land between a frame's G-buffer end and its post chain. Keyed on it, the
// resolve looked for the snapshot of the next frame and fell back in 145-395 of 600 frames (user session 2026-11-09).
// A frame begins at its first snapshot after the previous frame's resolve (or at a site that cannot follow the last one:
// the G-buffer end again, or an earlier site), and ends at its resolve: each resolve reads only the snapshot recorded
// since the resolve before it.
struct Ring {
  uint64_t frame = 0;       // the recorded frame of the newest snapshot (0: none yet)
  int last_site = -1;       // the latest site of that frame
  bool resolved = true;     // its resolve has run
  uint64_t written[2] = {UINT64_MAX, UINT64_MAX};   // the frame each image holds

  struct Begin { bool skip; bool merge; uint32_t slot; };
  // A snapshot at `site` (0 G-buffer end, 1 decal end, 2 lighting). skip: this frame has that site already (the decal
  // pass can match more than once); merge: write over an earlier site of the same frame; slot: the image to write.
  Begin begin(uint32_t site) {
    const int s = int(site);
    if (!resolved && s == last_site && s != 0) return {true, false, uint32_t(frame & 1)};
    const bool fresh = resolved || s <= last_site;
    if (fresh) { ++frame; resolved = false; }
    last_site = s;
    return {false, !fresh && written[frame & 1] == frame, uint32_t(frame & 1)};
  }
  void wrote() { written[frame & 1] = frame; }
  // The resolve of the frame being recorded: true when that frame wrote its snapshot (into *slot). Ends the frame, so
  // a resolve without a snapshot since the last one falls back instead of reading an older frame's.
  bool take(uint32_t* slot) {
    const bool ok = !resolved && written[frame & 1] == frame;
    resolved = true;
    *slot = uint32_t(frame & 1);
    return ok;
  }
};

}  // namespace bg3::strand_snapshot
