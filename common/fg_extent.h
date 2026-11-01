#pragma once
// Frame generation inputs when the swapchain is not the game's output size (fg_letterbox=1).
// BG3 in borderless / fake fullscreen on a 3840x2160 desktop renders 3840x1600 and its final UI
// composite draws that output into the 3840x2160 swapchain (letterboxed or scaled). The
// composite's hudless and UI images are the game's output size, not the pass size, so matching
// them by the pass extent found nothing and DLSS-G never got its inputs (gf1 141602: missing 0xf).
// Pure CPU logic; tests/fg_extent_test.cpp.
#include <cstdint>
namespace bg3::fg {
struct Extent { uint32_t w{}, h{}; };
struct Rect { int32_t x{}, y{}; uint32_t w{}, h{}; };
// The game's output extent among the composite's sampled images: the pass extent when one of
// them has it (the ordinary case, unchanged), else with `letterbox` the largest one that fits
// inside the pass and covers more than a quarter of it (excludes half-resolution helpers such
// as bloom levels, a quarter of the area). {0,0}: none.
inline Extent output_extent(const Extent* sampled, uint32_t n, Extent pass, bool letterbox) {
  Extent best{};
  uint64_t best_area = 0;
  for (uint32_t i = 0; i < n; ++i) {
    const Extent e = sampled[i];
    if (e.w == pass.w && e.h == pass.h) return pass;
    if (!letterbox || !e.w || !e.h || e.w > pass.w || e.h > pass.h) continue;
    const uint64_t area = uint64_t(e.w) * e.h;
    if (area * 4 <= uint64_t(pass.w) * pass.h) continue;
    if (area > best_area) { best_area = area; best = e; }
  }
  return best;
}
// Where the game's output lands in the swapchain (DLSS-G's backbuffer subrect): the composite's
// viewport when it has one inside the pass, else the output centred in the pass. Equal extents:
// the whole image at 0,0 (as before).
inline Rect backbuffer_rect(Extent output, Extent pass, bool has_viewport, float vx, float vy, float vw, float vh) {
  if (output.w == pass.w && output.h == pass.h) return {0, 0, pass.w, pass.h};
  if (has_viewport && vw >= 1.f && vh >= 1.f && vx >= 0.f && vy >= 0.f && vx + vw <= float(pass.w) + 0.5f &&
      vy + vh <= float(pass.h) + 0.5f)
    return {int32_t(vx + 0.5f), int32_t(vy + 0.5f), uint32_t(vw + 0.5f), uint32_t(vh + 0.5f)};
  const uint32_t w = output.w < pass.w ? output.w : pass.w, h = output.h < pass.h ? output.h : pass.h;
  return {int32_t((pass.w - w) / 2), int32_t((pass.h - h) / 2), w, h};
}
}  // namespace bg3::fg
