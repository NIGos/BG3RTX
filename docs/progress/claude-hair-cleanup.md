## RTX Hair: coverage keep in the On bundle, entering head 5 -> 3 card frames, six obsolete keys removed, hitch frame line (branch claude/hair-cleanup, READY_FOR_LIVE, 2026-11-14)

Design and evidence: docs/RTX_HAIR_PRIMARY.md 6k. Offline only; evidence is hx1 194658 (6j). No live.

- Bundle: RTX Hair On writes `rt_hair_coverage_keep=1` (26 keys). Old files get it on load. hx1 194658: "coverage
  stale" refusals 16-35 -> 0, entering head 10 -> 5 card frames, 0 switches of any cause, no cost outside the spread.
  The Advanced knob stays as the A/B. `rt_hair_card_fill` stays an Advanced option outside the bundle,
  `rt_hair_dense_sample` a diagnostic, `rt_hair_header_scene_pass` and `rt_hair_header_cam_gpu` stay switchable.
- Entering head, with keep on (`SwapGate::early`): the 5 frames were 2 without the groom in any publish (its build
  latency: kept), 1 with it in one publish only (kept: the frame may still read the publish before) and 2 of the
  prepass and G-buffer handshake. Both handshake frames go: a ready mark counts in the same publish, and the frame
  the groom is first published arms the prepass. Offline 5 -> 3 (4 when that frame's header lacks the groom).
  A prepass that went first for a refusing G-buffer meets the existing native-depth fallback. CTest
  `test_entering_head`. Not exercised in game.
- Removed with their code, knobs, ini lines and tests: `rt_hair_stale_publish` (the submit-time camera check and its
  UBO lookup at every hair draw), `rt_hair_row_stable` (GroomRows), `rt_hair_watchdog_draw` (DrawWatchdog),
  `rt_hair_watchdog_one_key`, `rt_hair_swap_latch`, `rt_hair_coverage_min_pixels`. 394 lines of layer and header code
  net (48 added, 442 removed), 223 of tests, 5 Advanced knobs, 25 ini lines. An old user file with the keys loads
  (carried, read by nothing: settings_test). All six read 0 in the qualifying run: no behaviour change by the removal.
- Log text: the switches line has seven causes (no "key watchdog", "coverage latched", "key watchdog trips", "latched
  keys"); the header safety line lost the per-draw and one-key fields; the "stale publish check" line is gone.
- Hitch frames (two scene G-buffer passes in one present interval, never seen live): the first one of a session now
  writes one line also with `detailed_logging=0`: "[hair primary] hitch frame: ...". Cost: two sums per pass.
- No shader, no ABI. `shaders/variants.txt` delta 0. GPU cost 0; the per-draw UBO binding query of 6a is gone (CPU).
- hx1 regenerated (predicted.json first): 16 phases, about 98 s, the bundle as the launcher writes it (no card fill,
  no dense sampler). Gated at 0 in 4 dollies and 2 orbits of 180 frames: label taken by the decal pass, by another
  scene pass, strands->cards of any cause, frames with every draw on cards, frame watchdog trips, rebase origin.
  A/B inside the run: `rt_hair_coverage_keep=0` (2 dollies). Reported: card runs (3 or 4 predicted, 10 with keep
  off), refusals, dropouts, GPU ms, a hitch frame. The job expects the seven-cause line: an older layer is INVALID.
  hc1, hk1, hl1, hf2 marked historical, pl1 not current (generators' docstrings); they still lint.
- Tests: `build.ps1 -Test` 226 of 226, lint live and frozen 0 failing for the staged hair jobs, snapshot appended
  only (3 readiness queries, 1 pattern), `merge_scope --run tooling` 0.
- User check after a go: S8, RTX Hair On alone. Zoom fast with the wheel and orbit: heads in view keep strands; a
  head that enters the view shows cards for about 3 frames.
- Next live (hx1, one launch): 0 switches of any cause in every bundle window; card runs 3 or 4 (5 = the handshake
  change had no effect; then say so); "prepass card depth dropped without its G-buffer swap" 0; keep off: card runs 10.
- Left: styles without a groom file (S8: index counts 3399, 7086); the key-off path of the header pass key (after a
  release); the 2 build-latency frames of an entering head; docs/HANDOFF_SESSION.md still names the removed keys
  (coordinator's file).
