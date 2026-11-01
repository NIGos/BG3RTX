## Box lights `ls_box=2` (branch worktree-agent-ab66efcab091c48ec, offline, READY_FOR_LIVE, 2026-11-01)

- Causes (lr3 S6 08:49:39): 0.009 traced box lights/px because `ls_box=1` traces only the shadow-mapped
  ones (evaluated 1.141/px, listed 2.44/px: not culling); those ran past their shadow view's near plane
  (847,547 of 847,557 occluded). `projector validation px 0`: it counts only under `ls_debug=4`.
  `ls_debug=4` "a quarter": AO 0 x mix(1, AO, m0.y) leaves half the direct light at S7 (m0.y 0.5) in both;
  the residual is additive (the game's in-scatter, not zeroed). Details: TRACK_D_LOCAL_LIGHTS 9.10.
- `ls_box=2` (GUI "All box lights", off): unshadowed boxes traced to the end of their light-side fade-in
  (visibility only, can only darken); shadow-mapped ones only to the near plane. New in-play accuracy
  check `ls box bound` (recompute vs the stored B10G11R11 value's upper neighbour) and `ls box trace`,
  device-local counters reduced per group, copied into rtStats 385-416 (after gi_far 372-382 and the
  rf box part 383-384). `box_light_test` checks the math against f469db78; `bl_reaches` is shared by
  D-LS and the rf_hit_shade box early-out.
- Key-off SPIR-V: before the merge, 992 of 1244 byte-identical vs 904c41e; the rest are the LS_BOX
  variants and the rf_hit_shade ones (same box math, shared). Expected +0.15 to +0.3 ms on the D-LS
  pass. Live: `tools/live_batches/box_lights` (S6 + S7, ~10 min, 0.65 GB; dry run passes). Not run.
- User check: S6 forge and S7 inn with "All box lights": no light leaking into the next room or
  through floors; the main-menu skull keeps its beam.
