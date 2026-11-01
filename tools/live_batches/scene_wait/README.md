# Scene-wait fix A/B (live_batch scene-wait), NOT authorized

Decides whether `rt_hit_material_recycle=1` (docs/TRACK_D_SCENE_BUILD.md "Hit-material CPU on the scene commit path")
removes the stale-scene timeouts the native hit materials cause (shadows detach while turning). Lean format of
[../lean_regression/README.md](../lean_regression/README.md): one foreground launch per save, FG 2, every A/B by
toggling the key inside the run, `rt_stats` only in `diag-*`, no traces in cost windows. Independent jobs. Receipt
`scene-wait.json`; the plan goes to the test tree's `captures\live_batches\scene_wait\plans`. Needs a layer build with
the key (a game restart; the key itself is live).

Startup (both): the m4p4 Ultra bundle (`rt_hit_material=1`, `rt_hit_material_instanced=1`, `rt_emissive=1`, GI hit
direct/sun, rf hit shade, hit emission, `rt_xfb_mb=1024`), `cpu_timing=1` (the `[cpu]` splits), `rt_scene_wait_log_frames=120`,
`rt_hit_material_recycle=0`.

| Job | Save | Why |
|---|---|---|
| `sw1-s8-fg2` | Water Queens House | m4p4 S8: 3-232 timeouts per 600 frames with the bundle |
| `sw2-camp-fg2` | Campsite - 27h | camp: lr1 had 0 timeouts without hit materials; the user's run had 5-456 |

Plan `sw_pan.json` (176 s): `warmup-on` 10 s; `diag-rec0` / `diag-rec1` 25 s each (rt_stats, recycle 0 / 1: the
materials still qualify); `shot-off-a` / `shot-on` / `shot-off-b` 8 s static, one PNG each, recycle 0/1/0, exposure held
(no energy change); `cost-on-static` 15 s (recycle 1); pans `base-off-a-q/e`, `cost-on-q/e`, `base-off-b-q/e` 12 s each
(Q then E back, recycle 0, 1, 0; exposure held); `restore` 5 s.

## Deciding numbers
- Valid run: every `expect` line and `expect_every` gate. Gate: `[rt] scene wait: 120 frames ... 0 timeouts` in every
  complete line of `cost-on-static`, `cost-on-q`, `cost-on-e` (queue_gates.SCENE_WAIT_GATE); every other window
  >= 9 s logs complete scene-wait lines (the recycle-0 baseline, expected to show timeouts); one trace per shot.
- Report per scene: timeouts and wait mean per window, recycle 0 vs 1 (expected 1: 0 timeouts, mean 2-5 ms as without
  hit materials); `[cpu] hit material` allocations/frees per frame and MB/frame (expected 1: ~0 allocations), query and
  G-buffer material hook ms; `[cpu] scene build recording` "capture end to commit" and its lock wait.
- No regression: `hit material (N frames)` qualified / attempted in `diag-rec1` within the `diag-rec0` noise; shot-on
  energy equal to the OFF shots (`py -3 -m tools.m4_promotion_gate` energy triple); rt passes GPU ms unchanged.
- Keep: promote `rt_hit_material_recycle=1` into the hit-material bundle (exp_hit_material) and the M4 jobs' sets.
