// rr_hair_strand_aa (docs/RTX_HAIR_PRIMARY.md 5n): which frame's strand snapshot the RR-before-post resolve may read.
// layer/rrpost.cpp writes the snapshot at the end of the scene's G-buffer pass (rr_hair_strand_snap_site 3: later sites
// of the same frame merge into it) into one image; the resolve reads it only when, at vkQueueSubmit, the snapshot
// submitted last is its own frame's (Pairing), else the game's resolve runs and the frame is counted.
// CTest hair_groom_asset_test test_strand_snapshot_pairing.
#pragma once
#include <cstdint>

namespace bg3::strand_snapshot {

// The G-buffer's colour formats (VkFormat values: R16G16_SFLOAT 83, R8G8B8A8_UNORM 37 x 3) and D24_UNORM_S8_UINT (129):
// the pass ngx.cpp ngx_note_pass records the RR guides and the camera at (layer/ngx.cpp).
inline bool gbuffer_pass(uint32_t color_count, const int32_t* color_formats, int32_t depth_format) {
  return color_count >= 4 && color_formats[0] == 83 && color_formats[1] == 37 && color_formats[2] == 37 &&
         color_formats[3] == 37 && depth_format == 129;
}

// Which resolve may read the snapshot. Both sites are recorded on game worker threads, in different command buffers, and
// recording order is not frame order: in rv1-s8 (Water Queens House, FG 3, 2026-11-09) every 600-frame period had 600
// snapshots and 600 resolves, yet 113-140 resolves fell back. The pairing by recording order (the previous Ring) gave
// resolve N the image of frame N + 1 when snap N + 1 was recorded before resolve N (on the GPU that image still held
// frame N - 1: a wrong frame, silently), and resolve N + 1 then fell back. frame_index() counts presents (another thread)
// and cannot pair them either (145-395 of 600 fell back keyed on it). Submission order can: a frame's G-buffer goes down
// before its post chain and frames go down in order, so at its GPU time the one snapshot image holds the snapshot
// submitted last.
// So the k-th snapshot (at the anchor site) and the k-th resolve share the ordinal k (plus an offset learned at submit).
// At vkQueueSubmit, per command buffer in submission order, a resolve is valid only when the snapshot submitted last
// carries its ordinal and was written: its host-visible flag then holds its token, else 0 and the shader ignores the
// snapshot (the game's resolve, the same as a fallback). A mismatch resynchronises the offset. A frame without a snapshot
// leaves the last one at an older ordinal (refused), and a resolve recorded before its snapshot is decided at submit.
struct Pairing {
  static constexpr uint32_t kSlots = 64;     // resolve flags (host-visible words), one per resolve, reused after 64
  static constexpr uint32_t kPending = 16;   // recent snapshots and resolves, decided at their submit
  struct Snap { uint64_t rec = 0, ordinal = 0; bool ok = false; };
  struct Res { uint64_t rec = 0, raw = 0, expected = 0; bool ran = false, decided = false; };
  struct Take { uint64_t raw; uint32_t slot, token; };
  Snap snaps[kPending];
  Res res[kPending];
  uint64_t n_snap = 0, n_res = 0;
  int64_t offset = 0;
  uint64_t last = 0;     // ordinal of the snapshot submitted last (0: none yet)
  bool last_ok = false;  // it was written
  // Decided resolves that recorded the strand resolve: with their own frame's snapshot (own) or not (refused: another
  // frame's or none was submitted last; the game's resolve on the GPU); offset corrections; the most snapshots recorded
  // ahead of a resolve (2: snap N + 1 recorded before resolve N).
  uint64_t own = 0, refused = 0, resyncs = 0;
  uint32_t ahead_max = 0;

  static uint32_t token(uint64_t raw) { return uint32_t(raw) | 0x80000000u; }   // never 0
  // A snapshot at the anchor site, recorded into `rec` (CmdState::recording); wrote() once its dispatch is recorded.
  uint64_t snapshot(uint64_t rec) {
    ++n_snap;
    snaps[n_snap % kPending] = Snap{rec, n_snap, false};
    return n_snap;
  }
  void wrote(uint64_t ordinal) {
    Snap& s = snaps[ordinal % kPending];
    if (s.ordinal == ordinal) s.ok = true;
  }
  // A resolve (every 7401d785 while the key is on, run or not, so both counts follow the same frames), into `rec`.
  Take resolve(uint64_t rec) {
    ++n_res;
    const uint64_t expected = uint64_t(int64_t(n_res) + offset);
    res[n_res % kPending] = Res{rec, n_res, expected, false, false};
    if (n_snap > expected && n_snap - expected > ahead_max) ahead_max = uint32_t(n_snap - expected);
    return {n_res, uint32_t(n_res % kSlots), token(n_res)};
  }
  void ran(uint64_t raw) {   // this resolve recorded the strand resolve (it reads its flag)
    Res& r = res[raw % kPending];
    if (r.raw == raw) r.ran = true;
  }
  // At vkQueueSubmit, before it goes down, in submission order: per primary command buffer the snapshots of its
  // recordings (its own and its secondaries'), then their resolves (a frame's G-buffer runs before its post chain).
  void submit_snapshots(uint64_t rec) {
    if (!rec) return;
    uint64_t newest = 0;
    bool newest_ok = false;
    for (const Snap& s : snaps)
      if (s.rec == rec && s.ordinal > newest) newest = s.ordinal, newest_ok = s.ok;
    if (newest) last = newest, last_ok = newest_ok;   // a resubmission replays it: the image holds it again
  }
  // set(slot, value) writes a resolve's flag word.
  template <class F> void submit_resolves(uint64_t rec, F&& set) {
    if (!rec) return;
    for (Res& r : res) {
      if (r.rec != rec || !r.raw) continue;
      const bool valid = !r.decided && last && last_ok && last == r.expected;
      set(uint32_t(r.raw % kSlots), valid ? token(r.raw) : 0u);
      if (r.decided) continue;   // resubmitted: refused (its frame's snapshot may be overwritten since)
      r.decided = true;
      if (r.ran) ++(valid ? own : refused);
      if (last && last != r.expected) { offset = int64_t(last) - int64_t(r.raw); ++resyncs; }
    }
  }
  template <class F> void submit(uint64_t rec, F&& set) { submit_snapshots(rec); submit_resolves(rec, set); }
};

}  // namespace bg3::strand_snapshot
