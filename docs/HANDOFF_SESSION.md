# Session handoff - 2026-10-31

Read PROGRESS_CODEX.md next. History is preserved verbatim in
archive/PROGRESS_CODEX_2026-10-31.md. Full rendering program is incomplete and PAUSED by the user.

## Consolidation done (2026-10-31, main fc34604) - read before resuming

Plan and decisions: docs/CONSOLIDATION.md. All steps merged through merge-green; none tested in game yet.
- ABI is now 24 (23 for shader defines, 24 for the emissive policy in core): rebuild both DLLs together. Shader variants are defines (`gi/x.hlsl?NAME=V;...`,
  list in shaders/variants.txt, checked by shader_variant_test); the per-flag wrapper folders are gone.
- 20 rejected keys removed (rt_scene_rebase, rt_scene_camera_match, rt_ao, ls_k, rf_so, ...): rebase
  open branches on main before merging; old jobs that set them are harmless no-ops.
- Hot-path settings use cfg_hot() (refreshed once per frame in cfg_poll); BG3RTX_DIAGNOSTICS CMake
  option compiles diagnostic host code out; sun packets, hit-material rules and emissive formulas
  are pure functions in common/ with tests (emissive move to core: TRACK_D_EMISSIVE.md 13.21).
- Pending live batch (needs the user's go): regression check with FG on and RT A/B, shader define
  hot reload, rt_scene_wait 0/1 fps, rt_cascade_casters and ls_box promotion gates, dialogue flicker.

## Latest steering and user evidence

User requested safe shutdown and documentation handoff. No more work or launches.
The completed GI batch used explicit background live permission. Queue only;
never start while USER_PLAYING or an unowned BG3 process exists. Preserve saves,
D:/BG3_Tools backups, snapshot tags and junctions. No Ponytail.
User now cannot reproduce camp rug/plank rectangles or Astarion hairline band.
No new fix was installed: personal hair-origin-live trial remains 047e105 ABI 19.
User tried OMM and Current Cascade Sun, reports encouraging visuals, no obvious
new defects. A slight environment-dependent shadow flicker remains in dialogues.
This is distinct from earlier static hairstyle shadows and resolved UI corruption.

Full personal-trial log 12:14-12:18 contains eight Current Sun coverage windows.
One reports 2413435 certified pixels and 368956 actual solar ray samples.
Four later windows report zero samples with scene-unready native fallback.
Three other windows have zero samples AND zero fallback; do not classify these
as fallback from those counters alone. Do not repeat the erroneous claim that
this entire session had no actual RT solar samples. Source hash and all windows:
docs/evidence/dialogue-flicker-user-feedback-20261031.json.
Views changed and no time-aligned flicker burst exists. No causal verdict.

## Current candidate and test preparation

Main d3e06e1 is pushed; merge-green passed both DLLs/launcher, 60 CTests,
1232 shader files and QA. ABI 21. New experimental features remain OFF.
Candidate codex/user-regression-ab in the matching isolated worktree contains
09fc3eb (core-only receiver guard A/B switch) and 82cf690 (progress trim).
Both DLLs/launcher build and all 60 CTests pass; no new code installed in trial.
Core switch rt_hair_receiver_guard=0 uses existing Push.ls_flags bit 21.
Keep physical origin ON in both phases; only switch competing-Scalp guard.
No ABI growth, shader coverage/bias or occluder changes.

Camp spill draft: captures/camp-regression-ab/job.json and motion.json,
outside pending. Campsite - 27h, real FG 2 presentation, visible unfocused,
RT-off reference then spill OFF/ON/OFF, three K16 bursts, about 10-13 minutes.
Do not equate transport validity with quality. No artifact or no rescued draws
means inconclusive. Do not launch an unrelated camp view merely to fill time.
GI R3 subsequently ran successfully; see completed batch below. No queued continuation.

## Completed GI batch and exact resume state

Candidate d19ecd7 in user-regression-ab, build identity
34bfc5b637045a25e7e784b2c1395fce4cc1dafd6569089ef416ca10e31fa190.
Job gi-descriptor-r3-static-off-on-off: VALID 580 seconds, no NVIDIA 153/dumps,
no build drift. FG active all 48 captured frames, actual presentation, no focus
activation. Runner handle 34268 terminal; game PID 19584 exited normally.
Queue pending/running empty; gi-descriptor-r3-20261031 receipt closed/consumed.
Three K16 bursts: pan-20261031-125128-72401, 125324-72402, 125519-72403.
Raw captures/logs remain in candidate captures/. Derived JSON/contact sheets and
exact job/motion/queue result copied to docs/evidence/gi-descriptor-r3-20261031/.
Full-frame analysis process 84198 terminal. 565595 stable pixels, zero negative
GI-envelope pixels, 21517 positive. Old failure ROI has 14/12812 final mean-low
pixels versus old R2 1144/19043. Different sessions and denominators; no direct
cross-run causal comparison. GI means 0.003004/0.002999/0.003048 OFF/ON/OFF;
mean delta is smaller than observed OFF-repeat drift. No blanket energy/gain pass.
Whole GI GPU medians 2.097/2.288/2.048 ms; no isolated bounce timing.
Captured rendered cadence 15.05/29.82/30.06 fps; first OFF overlaps CPU shader
compilation. No performance inference from those capture rates.
No final broad image review or motion gate completed; do not promote GI defaults.
Personal user's ABI 19 trial unchanged. Latest core guard switch is candidate-only.

## Prepared fixes and limits

Main includes rt_alpha_spill (105 budget checks), native STI material/ST depth,
TileBlend camera coverage and conservative competing-Scalp receiver guard.
Hair/Pubes/Scalp share raster class 5; class alone cannot prove receiver identity.
Guard rejects ambiguous/unreadable/capped cases, preserves virtual origin.
31 CPU checks, 16 ordinary shader identities/enabled validations and 22 windowless
GPU helper cases pass. These are not native pixel/image quality evidence.

Development codex/omm-bake has current-Sun exact shadow recipe 2e01c3c/92f9995
beyond main and TileBlend GPU fixture fbee8d7. Exact shadow reads 28 used bytes;
camera and legacy classification unchanged. Separate legacy bridge 9231954 is
reviewed but parked until attribution. No family expansion or deployment now.
Tileset/TileBlend/material GPU fixtures pass 32/53/14 scoped synthetic cases,
zero validation errors. Actual native graphics capture and game cost unqualified.
Sun half-angle 0.00459 radians is correct; no arbitrary radius/blur/bias changes.

## Next, in order

Resume ONLY after the user resumes the goal. Then:
1. Preserve user feedback; establish native versus added RT visibility for dialogue
   flicker with temporal evidence, one hypothesis at a time. Old F10 stills cannot
   certify flicker. Rug/hairline attribution stays unresolved, not declared fixed.
2. M3/M4: corrected GI descriptor image recheck, visible materials/world lights,
   offscreen reflections. Old R2 dark-stipple images rejected. Both new GI keys OFF.
   Increments and gates: PLAN_M4_REFLECTIONS_GI.md; first job tools/live_batches/m4_m5_first (mm1).
3. M5 water (89% port screen), then environment by measured screen impact.
   Increments and gates: PLAN_M5_WATER.md; M5.1 job mm2 needs its code first.
4. M2 persistence only when visible offscreen need requires it. Native board data
   lacks authored UV/NBT; do not infer them from positions or file order.
5. M6 shared opaque reference/PT research last. Never mark whole program complete.

Maximum two work streams with isolated owners. Existing helpers are finished;
legacy bridge is parked. New logic in core unless native ownership requires host.
Merge once per verified milestone, only merge-green, then push main and branch.
Keep progress and handoff about 150 lines with normal spacing.
