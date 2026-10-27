// RT sun replace: where the traced sun visibility is trusted (docs/TRACK_D_RT.md 12).
//
// With rt_sun_replace=1, gi/rt_shadow.hlsl writes the traced visibility into the game's
// sun mask instead of min(game, traced) wherever the geometry that could shadow the pixel
// is in the TLAS. Two things make a pixel untrusted, and it then keeps min(game, traced):
//   - its sun ray passes through a roof volume of the asset database (a roofed room, a
//     covered floor, a roof floor; Bg3AssetDbApi v3): the camera may have cut that roof
//     away, so it is missing from the TLAS but still in the game's cascades;
//   - its sun ray leaves the view frustum (the captured region) before it has risen
//     above the captured scene (the highest visible point, read back from the GPU, plus
//     a margin), or rt_sun_rise metres above the pixel: off-screen casters.
//
// The core writes, per frame, a slot of a host-visible ring: a header with the parameters
// and the roof volumes near the camera (camera-relative on the GPU through set 0 b2,
// binding 48). It is staged into a device-local R32_UINT image (binding 47, one texel per
// 4 bytes), which the shaders read and count into; the counters, the scene top and the
// camera are copied back into the slot and read kRing frames later.
#pragma once
#include "core.h"

constexpr uint32_t kSunMaxVolumes = 256;
constexpr uint32_t kSunHeader = 128;   // bytes before the 48-byte volume records

struct SunTrustFrame {
  VkImageView view;   // binding 47 (GENERAL)
  bool replace;       // rt_sun_replace is on
  uint32_t volumes;
};

// Fills this frame's slot and records its upload into `cmd` (always succeeds unless the
// allocation failed; without the database there are simply no roof volumes and, with
// rt_sun_roofs=1, nothing is trusted). `have_cam2`: the lighting dispatch has set 0 b2
// (the absolute camera); without it nothing is trusted either.
bool sunt_begin(VkCommandBuffer cmd, SunTrustFrame* f, bool have_cam2);
// After the sun passes: records the copy of what they wrote back into the slot.
void sunt_end(VkCommandBuffer cmd);
void sunt_release();
