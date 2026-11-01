## S6 m4p3 darkening: the animated lava and the shot timing, not gi_hit_direct (branch worktree-agent-a128d3e3a3182b5e8, offline, READY_FOR_LIVE, 2026-11-01)

- Shader invariant, offline on the S8 replay pack, same rays: base pass vs hit variant give giRaw.a bit-equal on
  100% of 2.73 M px, mean luminance +0.0755%, 6 px lower beyond the half rounding (depth-test classification
  flips between the two compiled shaders, both ways; lost 3.0e-8 vs added 6.8e-6 per px). New tool
  `py -3 -m tools.gi_hit_invariant --pack <dir>` (gate: alpha bit-equal, mean hit >= base), CTest
  `gi_hit_invariant_test`. The material set clones the scene's alpha bindings, so the occlusion is the key-off one.
- The steps cannot carry their share: diag-on sun added 0.75 vs 11208 for the local lights; instanced rows 0 at
  S6. +inst also contains rf_hit_shade (its tier (a) is brighter, 1.09).
- Localized (`tools/preview_energy_split.py`, decoded PNG previews, within 0.07% of the float means): lava is
  54.5% of the frame's luminance. inst -3.10% = lava -2.56% + rest -0.49%; the rest is -1.07% relative vs
  -0.87% / +0.87% for the two OFF shots. The OFF pair's 0.07% was a cancellation (lava +0.32%, rest -0.40%).
  The lava follows the game's glow buffer 48eedb24: OFF +8% / -8%, steps -5 / -16 / -25%.
- Timing: each m4p3 step shot was traced 1-2 frames after a 4 s DXC compile of its variant (warmup compiled
  only the all-on one). Noted in the m4_promotion README; the plans are unchanged.
- No fix in code (nothing to fix found). Live check prepared, not run: `tools/live_batches/m4_promotion/s6_diag`
  (`m4p5-s6-gidiag`, 4.5 min: S6 replay pack for the invariant, 5 OFF / 4 ON interleaved shots precompiled,
  gi_debug=3 and 2 pairs; predictions in its README). Needs approval of replay_pack_cap_mb=3300 (pack ~3 GB).
