#pragma once
// rt_capture_wide_split (startup, off by default; docs/TRACK_D_SCENE_BUILD.md "RT capture"):
// per-module vertex stride for the RT capture. Native hair (rt_native_hair=1) widens the capture
// layout to 88 bytes for EVERY vertex shader, while only shaders that export the native strand
// frame (layer/spirv_xfb.h patch_vs, hair_flags) fill bytes 24..87. With the key on, those
// "wide" modules keep 88 bytes and every other module is patched at the stride the capture would
// have without native hair (64 with rt_hit_material, 80/40 with the native tileset, else 24).
//
// The XFB stride is a decoration of the vertex shader module, so the stride is per module, then per
// pipeline, then per draw. Every split-mode reader already takes the stride per instance (InstInfo
// meta bits 0..7), per BLAS geometry or per job push constant; only the in-place clip -> world
// conversion (shaders/rt/xfb_to_world.hlsl) walks a region with one stride. Wide draws therefore
// get their own region in the soup, at the top of the alpha region, growing down:
//   [0, opaque_end)                 opaque draws, base stride (unchanged)
//   [alpha_base, alpha_cursor)      alpha-tested draws, base stride, growing up (unchanged)
//   [wide_cursor, xfb.size)         every wide draw (alpha-tested or not), wide stride, growing down
// The alpha and wide cursors share the alpha region; a draw that does not fit is handled like a full
// alpha region (alpha: made opaque; wide: left out, it cannot go to the base-stride opaque region).
#include <cstdint>

namespace bg3::capture_stride {

// The split is active only when the startup layout is wider than the base layout.
inline bool active(bool key, bool alpha_mode, uint32_t full_stride, uint32_t base_stride) {
  return key && alpha_mode && base_stride >= 24u && full_stride > base_stride;
}

// The stride a vertex shader module is patched with: the full (wide) stride for a module that
// exports the native strand frame (hair_outputs != 0 after patching at the full stride), else the
// base stride. Inactive: always the full stride (key off: byte-identical patches).
inline uint32_t module_stride(bool split_active, uint32_t full_stride, uint32_t base_stride,
                              uint32_t hair_outputs_at_full) {
  return split_active && hair_outputs_at_full == 0u ? base_stride : full_stride;
}

// A draw is "wide" when its pipeline's stride differs from the base stride of an active split.
inline bool wide_draw(bool split_active, uint32_t draw_stride, uint32_t base_stride) {
  return split_active && draw_stride != base_stride;
}

// The conversion binds the wide region at its lowest byte as a storage buffer: that offset must be a multiple of
// minStorageBufferOffsetAlignment (16 on the 5090; up to 256 allowed), and the region must stay a whole number of
// triangles. Each wide draw therefore reserves a multiple of lcm(3 * stride, alignment) below the buffer end (a
// multiple of 1 MiB): at most one unit of padding per draw (264 bytes at 88 B and 16 B alignment), converted as
// unused triangles that no BLAS references.
inline uint64_t gcd64(uint64_t a, uint64_t b) { while (b) { const uint64_t t = a % b; a = b; b = t; } return a; }
inline uint64_t wide_unit(uint32_t stride, uint64_t storage_alignment) {
  const uint64_t tri = 3ull * stride, align = storage_alignment < 4 ? 4 : storage_alignment;
  return tri / gcd64(tri, align) * align;
}
inline uint64_t wide_reserve(uint64_t reserved, uint64_t unit) { return unit ? (reserved + unit - 1) / unit * unit : reserved; }

// Placement in the shared alpha region. `alpha_cursor` grows up from alpha_base, `wide_cursor` grows
// down from the buffer end; neither may cross the other.
inline bool alpha_fits(uint64_t alpha_cursor, uint64_t reserved, uint64_t wide_cursor) {
  return reserved <= wide_cursor && alpha_cursor <= wide_cursor - reserved;
}
// The wide draw's offset (written to *offset) and the new wide cursor; false when it does not fit.
// `reserved` is the draw's wide_reserve: the offset stays aligned (XFB offsets and R32G32B32 vertex data need
// 4 bytes, the conversion's storage binding the device alignment) because the buffer end is a multiple of 1 MiB.
inline bool place_wide(uint64_t alpha_cursor, uint64_t* wide_cursor, uint64_t reserved, uint64_t* offset) {
  if (!alpha_fits(alpha_cursor, reserved, *wide_cursor)) return false;
  *wide_cursor -= reserved;
  *offset = *wide_cursor;
  return true;
}

// A cached mesh (mesh pool, object space) is reused only by draws of the same stride: the mesh key
// is made of the vertex inputs, not of the vertex shader, so a wide and a base pipeline can draw
// the same mesh. 0 (unknown) never matches.
inline bool mesh_stride_matches(uint32_t mesh_stride, uint32_t draw_stride) {
  return mesh_stride != 0u && mesh_stride == draw_stride;
}

// A per-draw BLAS is refitted only over the same vertex stride (its geometry description).
inline bool slot_refit_ok(uint32_t slot_stride, uint32_t draw_stride) { return slot_stride == draw_stride; }

// Capture bytes of a view whose vertices were all captured at `full_stride` (the key-off layout),
// when a fraction wide_share (0..1, in bytes at the full stride) belongs to wide modules: the rest
// shrinks to the base stride. Used for the predicted bytes of a live check.
inline double split_bytes(double full_bytes, double wide_share, uint32_t full_stride, uint32_t base_stride) {
  if (full_stride == 0u) return full_bytes;
  return full_bytes * wide_share + full_bytes * (1.0 - wide_share) * double(base_stride) / double(full_stride);
}

}  // namespace bg3::capture_stride
