// D-LS per-light data (docs/SE_BRIDGE.md, docs/TRACK_D_RT.md 9.8).
//
// The runtime lights near the camera (the Script Extender bridge merged with the asset
// database, Bg3AssetDbApi v2) are uploaded every frame into a host-visible ring.
// rt_local_shadows_match.hlsl joins them to the lighting's light array (b25) by absolute
// position and radius and writes one record per array slot into row 0 of a 4096 x 3 RGBA32UI
// image: the light's source radius, flags, owner and type. rt_local_shadows.hlsl reads
// it for each light it shadows.
#pragma once
#include "core.h"

constexpr uint32_t kLslSlots = 4096;      // b25 holds 4096 lights (TRACK_D 9.1)
constexpr uint32_t kLslMaxKnown = 1024;   // runtime lights uploaded per frame
constexpr uint32_t kLslRecord = 48;       // bytes per uploaded light (rt_local_shadows_match.hlsl)

struct LslFrame {
  VkDescriptorBufferInfo known;   // binding 45: this frame's slot of the upload ring
  VkImage slots_image;            // binding 46
  VkImageView slots_view;
  bool needs_init;                // the slot image is still UNDEFINED
  uint32_t count;                 // lights uploaded this frame
};

// Fills this frame's upload slot. False when there is nothing to join (no database and no
// bridge, API older than v2, ls_lights=0, or an allocation failed): the passes then use
// ls_source_radius for every light, as before.
bool lsl_begin(VkCommandBuffer cmd, LslFrame* f);
void lsl_initialized();   // the caller moved the slot image to GENERAL
void lsl_release();
