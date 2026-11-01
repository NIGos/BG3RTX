# S6 darkening diagnostic (live_batch m4-s6-diag), NOT authorized

Question: is the m4p3 S6 darkening (step-gi -1.05%, +sun -1.82%, +inst -3.10%; run 112657) caused by
`gi_hit_direct`, or by the forge's animated lava and the shot timing? Offline evidence (docs/TRACK_C_GI.md "S6
darkening"): on the S8 replay pack the hit variant keeps giRaw.a bit-equal and only adds (+0.0755%); at S6, 84% of
inst's darkening sits on lava pixels, which follow the game's glow buffer (OFF shots +8% / -8%, step shots down to
-25%), and every m4p3 step shot came 1-2 frames after a 4 s DXC compile. This job checks the same at S6, live.

One job, `m4p5-s6-gidiag` (Adamantine Forge, foreground, FG 2, 150 s plan, 17 PNG traces, one replay pack).
Startup: `preset=ultra`, `rt_hit_material=1`, `gi_rt=1`, `gi_rt_fallback=0`, `rt_cascade_casters=0` and
`rt_hit_material_instanced=0` (the replay pack contract is the base variant without defines), `rt_emissive=0`,
every other M4 key 0, `replay_pack_cap_mb=3300`. Needs a layer + core with the replay writer (ABI 25) and the
non-finite counters 417-418 (e6c30fb).

| Phase | s | Keys | Why |
|---|---|---|---|
| warmup-gi, warmup-off | 12, 8 | gi_hit_direct 1, 0 | compiles the hit variant before any shot (m4p3 compiled during its shots) |
| diag-pack | 15 | gi_hit_direct=1, rt_stats=1, replay_pack=1 | one S6 pack; the `GI hit direct` line with the non-finite counters |
| settle-off | 8 | 0 | |
| shot-off-1, shot-gi-1, ... shot-off-5 | 9 x 6 | alternating 0 / 1 | each ON shot between two OFF shots: a linear drift cancels |
| shot-vis-off/gi-1..2 | 4 x 6 | gi_debug=3 | the visibility alone (diffuse = 0.2 x giRaw.a): must not move |
| shot-add-off/gi-1..2 | 4 x 6 | gi_debug=2 | the GI's added diffuse alone (x 4): ON >= OFF |
| restore | 5 | 0 | |

Traces at 4 s of each 6 s shot. No `post` step: replay_capture's post step deletes every older pack, and the
S8 pack is still the current one.

## Predictions (committed before the run) and how to read them
1. Pack, offline: `py -3 -m tools.gi_hit_invariant --pack <captures\replay-...-1>`: GATE PASS (giRaw.a bit-equal
   on every pixel, mean hit >= base). A FAIL is a shader or binding cause: then the per-pixel lower list and
   `frame_replay` stats decide the fix.
2. `GI hit direct` (diag-pack): non-finite hit terms 0, bounce over the half-float max 0 px.
3. gi_debug=0: per ON shot k, d_k = ON_k / mean(OFF_k, OFF_k+1) - 1 from the float means (`dumps.json`,
   5c3fdabc slot 0). Noise: s from the 5 OFF shots' residuals around their linear fit (3 dof); band
   t(0.975, 3) x s x sqrt(3/2) per d_k, t(0.975, 3) x s x sqrt(3/8) for the mean of the four. Prediction: the
   mean of d_k is >= -band (no darkening). Localization of any d_k outside its band:
   `py -3 -m tools.preview_energy_split --root <captures> --off <OFF_k> <OFF_k+1> gi=<ON_k>` (lava vs the rest,
   and the glow buffer 48eedb24 as the lava's animation state).
4. gi_debug=3: ON = OFF within the same band (visibility identical by construction).
5. gi_debug=2: ON >= OFF (the added diffuse grows by the hidden-hit term).
If 1, 2 and 4 hold and 3 fails only on lava pixels with the glow buffer moving the same way, the m4p3 numbers
were the scene; the promotion gate at S6 then needs the lava excluded or more OFF repeats, not a shader fix.

Duration: 150 s plan + load, about 4.5 min. Disk: 17 traces (about 40 MB each measured in m4p3, the dry-run
counts 60) plus the pack (S8: 3.03 GB at this cap; S6 unknown): about 3.7 GB, over the 2 GB batch budget
because of the pack. The pack cap needs the user's approval for this job (rc1's approval was for rc1).
After the analysis: delete the 17 trace folders; keep the S6 pack only if a test reads it.

Offline check: `powershell -File tools\live_batches\m4_promotion\s6_diag\dryrun.ps1 [-Out <scratch dir>]`.
Last result 2026-11-01: all checks passed (estimate 4.5 min, 1030 MB without the pack).
