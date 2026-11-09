// Background runs (BG3RTX_BACKGROUND=1): which window messages may reach the game.
//
// BG3 reads the mouse only through SDL2 (2.30.6; no raw input, no relative mode, no
// GetCursorPos in bg3.exe). SDL turns WM_MOUSEMOVE / button / wheel messages into events,
// and its WM_MOUSELEAVE handler reads the REAL cursor (GetCursorPos) and clears the mouse
// focus. While the user works in another window, every posted WM_MOUSEMOVE re-arms
// TrackMouseEvent, Windows answers with an immediate WM_MOUSELEAVE (the real cursor is
// elsewhere), and SDL moves the game's pointer to the user's cursor: the runner's drag and
// wheel lose their position and focus. Real mouse messages also reach the game whenever
// the user's cursor crosses a visible part of its window (hover, inactive-window wheel).
//
// The runner marks every mouse message it posts with kPostedMark in the low word of
// wParam (unused by the MK_* flags, 0x01-0x40). While the game is not the real foreground
// window, unmarked mouse messages and WM_MOUSELEAVE are dropped; marked ones pass with the
// mark stripped. Keyboard messages are untouched: real keys only reach the focused window,
// which is not the game, so every key the game sees is the runner's.
#pragma once
#include <cstdint>

namespace bg3::bginput {

constexpr uint64_t kPostedMark = 0x0800;

constexpr uint32_t kMouseFirst = 0x0200;   // WM_MOUSEMOVE
constexpr uint32_t kMouseLast = 0x020E;    // WM_MOUSEHWHEEL
constexpr uint32_t kMouseLeave = 0x02A3;   // WM_MOUSELEAVE
constexpr uint32_t kMouseHover = 0x02A1;   // WM_MOUSEHOVER

enum class Verdict { Pass, PassStrip, Drop };

// filter_active: background run and the real foreground window is not the game.
constexpr Verdict classify(uint32_t message, uint64_t wparam, bool filter_active) {
  bool mouse = message >= kMouseFirst && message <= kMouseLast;
  if (mouse && (wparam & kPostedMark)) return Verdict::PassStrip;
  if (!filter_active) return Verdict::Pass;
  if (mouse || message == kMouseLeave || message == kMouseHover) return Verdict::Drop;
  return Verdict::Pass;
}

// Mouse buttons of an MK_* wParam as an SDL button mask (L=1, M=2, R=4, X1=8, X2=16).
constexpr uint32_t sdl_buttons(uint64_t wparam) {
  return ((wparam & 0x01) ? 1u : 0u) | ((wparam & 0x10) ? 2u : 0u) | ((wparam & 0x02) ? 4u : 0u) |
         ((wparam & 0x20) ? 8u : 0u) | ((wparam & 0x40) ? 16u : 0u);
}

}  // namespace bg3::bginput
