// rr_hair_strand_aa (docs/RTX_HAIR_PRIMARY.md 5n): which frame's strand snapshot the RR-before-post resolve may read.
// layer/rrpost.cpp writes the snapshot at the end of the scene's G-buffer pass (and again at the decal pass with the
// same formats, same frame) into the image of the frame's parity; the resolve of the same frame reads it only when that
// image was written in this frame, else it runs the game's resolve and counts the frame. CTest hair_groom_asset_test
// test_strand_snapshot_ring.
#pragma once
#include <cstdint>

namespace bg3::strand_snapshot {

// The G-buffer's colour formats (VkFormat values: R16G16_SFLOAT 83, R8G8B8A8_UNORM 37 x 3) and D24_UNORM_S8_UINT (129):
// the pass ngx.cpp ngx_note_pass records the RR guides and the camera at (layer/ngx.cpp).
inline bool gbuffer_pass(uint32_t color_count, const int32_t* color_formats, int32_t depth_format) {
  return color_count >= 4 && color_formats[0] == 83 && color_formats[1] == 37 && color_formats[2] == 37 &&
         color_formats[3] == 37 && depth_format == 129;
}

struct Ring {
  uint64_t written[2] = {UINT64_MAX, UINT64_MAX};
  void wrote(uint64_t frame) { written[frame & 1] = frame; }
  bool usable(uint64_t frame) const { return written[frame & 1] == frame; }
};

}  // namespace bg3::strand_snapshot
