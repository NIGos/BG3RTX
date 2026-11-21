## Water compose "no scene" on handout frames (branch claude/water-noscene, fix, READY_FOR_LIVE, 2026-11-21)

- Symptom: S8 Ultra, one uncomposed water frame (an on/off flip of reflections and shadows) in 4-8 % of the 60-frame
  windows (final-s8-ultra-high .014936 5/23, .033318 4/32, .040347 8/118, .050126 6/25).
- Cause (`core/water.cpp` compose, the handout branch): with rt_scene_handout=2 the lighting stash holds the dummy TLAS
  of frame F. A later pass accepted only the committed scene of F (`water_handout_pick` kCommitted). When frame F+1's
  build had already committed (b >= now), the pass was refused. An ordinary stash takes that newer pair
  (pick_scene kNewer). Log proof: in every clean window with "no scene 1" (or 2), "newer build before this frame" is 59
  (or 58) of 60 passes. Every other pass saw a committed build older than this frame, so the refused one saw b >= now.
- Fix: `WaterPick::kNewer` (`common/scene_handout.h` water_handout_pick + water_handout_newer). The pass takes the newer
  pair with take_newer (rt_scene_set does not wait when b >= now). The shift is off for kNewer, as for an ordinary stash.
  The stash's own committed scene still wins. A stash whose build has not committed yet is still refused (no wait).
  Pure fix, no key, no shader edit, no ABI change. Unit cases in tests/scene_handout_test.cpp; build -Test 239/239.
- New counters at the end of "[water] compose stability": newer scene taken, refused with the stash's build not
  committed yet, other. Rerun: final-s8-ultra-high at S8 Ultra; "no scene 0" and "uncomposed 0" in the static and pan
  windows, "newer scene taken" > 0 where the old run had refusals, no rise in compose GPU ms.
