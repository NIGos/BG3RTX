# S8 harbour water reflection flicker (live_batch water-flicker), NOT authorized

User report 2026-11-02 (Ultra on main): the water reflections at the port of Water Queens House flicker during camera
rotation. `[water] compose stability` reports 0 on/off flips, so the frame-level composition is stable; the candidate
is the temporal behaviour of the reflection signal through RR. Fix under test: `rt_water_rr_hitdist` (default 0,
live): water_reflect writes RR's specular hit distance on water pixels (its mirror hit distance where it resolves,
else no hit) instead of D-RF's value of the bed below (the water writes no depth).

| Job | Save | Plan | Mode |
|---|---|---|---|
| `wf1-s8-fg2` | Water Queens House | 163 s, 3 PNG, 3 K16 bursts | `visible_unfocused_fg` (the user is at the PC) |

Phases: `warmup` 20 s; `cost-off-a` / `cost-on` / `cost-off-b` 20 s, each followed by a 6 s `shot-*` (one trace,
exposure held); `rot-off-a` / `rot-on` / `rot-off-b` 20 s slow rotations (60 px/s, alternating direction) with one
K16 burst each at 8 s; `restore`.

## Gates
- Validity: every expect line, among them `[water] rt/water_reflect.hlsl?WATER_RR_HIT=1... water_reflect: built`.
- Energy: `py -3 -m tools.m4_promotion_gate <log>`: shot-on within the band of the OFF shots (predicted null).
- Cost: `gi: GPU cost` / `rf: reflection pass` ON against the OFF mean: no change expected (one store per pixel).
- Flicker: `py -3 -m scripts.qa.pan_artifacts <the 3 burst dirs> --out <dir>`: the final luma's temporal events of
  rot-on below both rot-off bursts; |rot-off-a - rot-off-b| is the noise. Judge GPU ms and energy only, not frame
  time (unfocused mode). Disk: about 7 GB of bursts (over the 2 GB lean budget: a rotation flicker needs them);
  delete them after the analysis.
- Look: the user's own check, Ultra with the key on, rotating over the harbour.

Offline check: `powershell -File tools\live_batches\water_flicker\dryrun.ps1 [-Out <scratch dir>]`.
