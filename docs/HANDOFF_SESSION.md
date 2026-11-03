# Session handoff - 2026-11-03 afternoon

Main dc3ef899, ABI 26. Read [PROGRESS_CODEX.md](PROGRESS_CODEX.md) next for questions and evidence,
then [GOAL_NEXT.md](GOAL_NEXT.md) for stage order. Verbatim history: [11-03 archive](archive/PROGRESS_20261103.md).

## Current state

- Stages 2 and 3 done; corrected water flicker fix still awaits qualification. Stage 4 partly qualified, stage 5 active.
- Ultra: water + Refraction, shading + Bounce, emission Hidden hits, gi_far 6 m, qualified OMM and box lights.
- OMM: rt_omm/dyn, VT masks and fine LOD on; box ls_box=2 honours the game's shadow flag. Smoke valid.
- Dialogue moving shadow fixed by default. User accepts AO self-occlusion; rt_cascade_static_only stays off.
- Water camera correction looks right to the user; corrected wf9 gates and cost remain pending.
- Converted RTX Hair grooms work in game, about 40% cheaper than native cards. Native cards stay off (+2.3-6.3 ms RT).
- Hair/DoF shimmer cause found: replayed DoF/bloom weights flip each frame; rr_post_temporal is the pending fix.
- User rejected rr_hair_resolve and CPU perf keys. Do not promote or queue them for qualification.

## On main

- Shader specialization migration, null water-key removal, cascade identity fix, OMM/box promotion and water integration fix.
- Groom conversion/integration, native-hair pool/cost fixes, corrected compose camera shift and user trial controls.
- Mesh content identity, generated material rows, VT alpha hit materials, capture/VRAM/AS policies and GI NaN fix.
- Launcher category fix: experimental controls now appear in Advanced. Settings test validates visible categories.
- New experimental renderer keys remain off unless explicitly qualified. User approval of appearance is not a gate pass.

## Open branches

- post-temporal: rr_post_temporal; offline checks pass, READY_FOR_LIVE. About 250 MiB history at 2560 x 1067; GPU ms pending.
- genrows-gate: fixes parallel depth-source publication and generated two-sided row qualification jobs; rerun pending.
- cov-paks2: COLOR_0/Tileset publication, device-local UV join, incremental catalogue; generation blocked on LSLib DLLs.
- water-hit-cost: sparse light membership/cone/zero culling for hidden-hit shading; equivalent offline, live cost pending.
- gpu-opt1: compact GI hit state, alpha metadata cache, empty water-tile skip; 36 byte-exact replay comparisons, all off.
- rt-continuity: lane at main dc3ef899; no implementation/result recorded yet.
- gpu-opt2: lane at main dc3ef899; no implementation/result recorded yet.

## Next live queue (prepare only; explicit batch steering required)

1. post_temporal: Dialogue3 white hair/DoF and static S8, Ultra 0/1/0/1; both ON windows, linear energy and GPU ms.
2. Corrected wf9: S8 static zero-delta gate and held Q/E turns; reflection-only metric, stable energy and compose cost.
3. genrows-gate: generated rows after offline checks; compare coverage/energy in every window, then real Ultra smoke.
4. Water off-screen hit shading and water-hit-cost: retained F-1 tuple, nonzero attempts, equivalent energy and clean timing.
5. gpu-opt1 after dependent offline checks: isolate each key, pass cost/energy/coverage gates, then integrated Ultra smoke.
6. Remaining GI guards/one-ray/SHaRC only with their valid null/reference gates; keep unqualified keys off.

Do not stage unauthorized jobs in any watched pending inbox. Group by save, toggle within one run, 2-5 minutes/scene.
Finish each candidate's dependent offline checks before requesting that concrete batch; unrelated lanes need not finish.
Groom style conversion and cov-paks2 data work can continue offline without live permission.

## Rules learned today

- No commits on main during merge-green; no lives while merge-green updates the test tree.
- Mouse turns never become smooth here: use held Q/E, not mouse input, for camera-motion comparisons.
- Launcher shows only Lighting/Surfaces/Performance/Memory/Advanced. Experimental is not a visible category.
- Never judge reflection quality from whole-water glint metrics; never accept lower residual caused by lost energy.
- Compare pre-post float energy and GPU pass ms; expensive diagnostics belong outside cost windows.
- Give jobs patterns/expect and per-window expect_every. Snapshot lines are events with 1 s settle.
- Heavy DXC one at a time; specialization constants for new on/off keys; cached preprocessed identity checks.

## Safety and working pattern

- Follow [AGENTS.md](../AGENTS.md): never touch saves, backups or snapshot tags; never hold queue results open.
- Game launches only through the serial queue after fresh explicit user steering; no launch is authorized by this handoff.
- Remove worktrees only with scripts/remove-worktree.ps1; shared NTFS junctions make recursive cleanup unsafe.
- One hypothesis, smallest fix behind an off key, same-run A/B, keep or reject; core logic is hot-reloadable.
- Branches write progress fragments; fold only heading branches verified as ancestors of main.
- This consolidation folded 59 fragments, retained six unmerged fragments and archived the full folded text verbatim.
- Open questions remain at the top of PROGRESS: GUI audit, licence, NVIDIA notification, water shadow edge, emission mode.
- Keep HANDOFF <= 80 lines and PROGRESS about 150. No game, code changes or git metadata writes in this docs task.
