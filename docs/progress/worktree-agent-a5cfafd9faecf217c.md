## Dialogue character shadows: code attribution + live batch prepared (branch worktree-agent-a5cfafd9faecf217c, READY_FOR_LIVE, 2026-11-02)
- User report: intrusive shadows on the characters in dialogues. The user's session (bg3rtx.log 06:24-06:32):
  Ultra + ls_box=2 + hit materials + rt_alpha_spill=1, stride 64 (native hair off: hair cards opaque), alpha made
  opaque 0.00, scene wait 599/599. Code reading, most plausible first (file:line in DIALOGUE_SHADOWS.md
  "Attribution batch 2026-11-02"): H2 D-LS traces the cinematic rig lights the art leaves unshadowed
  (ls_owners=31, ls_noshadow=1, ls_box=2; unmatched and box lights always traced); H1 opaque hair/scalp cards;
  H5 cascade casters put a 1-2 frame old copy of animated characters into the sun TLAS (opaque dedupe needs an
  equal per-draw UBO offset; 22 of 264 cascade draws deduped); H3 dyn hold keeps up to 4 older poses with a
  static camera; H4 AO soft only; contact shadows ruled out in Ultra (run only with rt_shadows=0); H6 excluded
  in the user session (0.00), gated per window.
- Batch tools/live_batches/dialogue_shadows (make_job.py, README, predicted.json written before any run):
  ds1/ds2/ds3 on `Test Dialogue1-3` (sizes in the bullet below).
  One launch per save, all toggles inside the run: base / RT sun off / local off / box off / character lights
  off / art-unshadowed off / cascade off / dyn hold off / AO off / native hair+scalp / + physical origin, views
  ls_debug 2 and 9, ls_stats, rt_stats hair off/on/off, cost OFF/hair/OFF. Gates per window: scene wait 0
  timeouts, opaque dropped 0.00, alpha made opaque 0.00, cascade full 0.0, static camera (no `camera moving`).
- Hair validation in the same runs (NATIVE_HAIR.md "Dialogue validation plan"): startup latches on, keys 0 in
  the base phases; participation, invalid 0, origin counters, primary match, ROI energy, cost.
- Dryrun (lean_dryrun: static checks, scene-wait and capacity lints, runs A and B) and the exposure-hold lint pass.
- User answers: saves load in dialogue; shadow on all characters, sometimes moving (trees?), not moving in the
  isometric view. Added H7 (wind foliage captured live from the camera prepass into the RT sun scene; no
  foliage-only key exists): PNG pairs 0.5 s apart with RT on and the game's map only, rt_debug=2 view.
  Jobs now visible_unfocused_fg (user at the PC), stats every 60 frames, 1024 px: 260 s of plan, 22 traces,
  about 19 min and 1.9 GB. lean_dryrun.ps1 gained `-Unfocused` (foreground path unchanged, box_lights passes).
- ds1 run 1 (dsh-20261102b, 10:03, unfocused) "stalled" before the plan: first-time native-hair shader compiles on
  the render thread (up to 17 s each), 53 s without a present; the 30 s stall rule closed the game as frames
  resumed. Not focus or dialogue. Fix: auto-run.ps1 does not call it a stall while `[shaders] ... compiled in`
  lines appear; plan gains diag-warm-a/b (toggle-only variants first); jobs foreground again (user: PC free), 300 s.
- Separate gap: `[water] reflections skipped: no variant for rt_native_scalp, rt_native_hair,
  rt_hair_physical_origin` (water reflections are off whenever native hair is on).
