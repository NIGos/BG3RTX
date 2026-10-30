# Session handoff — 2026-10-30 (FG copy quality not qualified)

## Latest: minimal descriptor reset built for user trial (2026-10-30)

User: "proviamola". Applied only allocation-lifetime reset; layer DLL rebuilt.
Successful allocations clear tracked descriptor contents/fallback dynamic counts
before assigning layout; unrelated sets/partial writes are preserved. No ini key,
ABI, HLSL, settings or GPU work changes. Build/CTest 16/16; fixed real-function
nine-case reproduction 0 failures (old 4). New helper common/descriptor_epoch.h
and descriptor_epoch_test. No game launched or main merge. User should restart
through normal launcher and test screenshot foliage/flags with world UI, RR on/off.
Full visual resolution remains unproved. Material-owner selection and RR helper
changes are deliberately absent. Top PROGRESS_CODEX and trial evidence JSON apply.
The earlier UNAPPLIED note below describes the preceding offline-analysis state.


## Latest: user-requested offline UI contamination analysis (2026-10-30)

UI includes overhead text/labels/selection, not only hover. No new live job authorized
by this task; no game actions or production code/binary/ini changes were made.
Read docs/UI_CONTAMINATION_OFFLINE.md and top PROGRESS_CODEX.md. Proven CPU bug:
recycled descriptor-set handles retain old contents/dynamic counts; optional RT3/SSS
reads can accept stale-but-live UI data. Nine-case native source harness: production
4 failures -> candidate 0. Minimal patch is UNAPPLIED; fix also needs material-owner
provenance. Exact blue-frame attribution remains unproved (buffer epoch/data missing).
Separate RR fallback rejects normal exposure helper 4681b95e. No quality pass/merge.
Proofs in docs/evidence/ui-descriptor-20261030; artifacts captures/offline-ui-20261030.


Read AGENTS.md, then the top of docs/PROGRESS_CODEX.md, then D:/BG3_Tools/USER_STEERING.md
(newest entries first). The user decides by FG on (fg=2), real presentation, visible window.

## Permission

Live jobs only through scripts/test-queue.ps1; visible unfocused FG jobs are allowed
(14:29 authorization). Skip while USER_PLAYING exists or a game the runner did not launch
runs. Never hold results.md open. No saves, backups, snapshot tags, force-push or junction
cleanup. The launcher BG3RTX.exe, if left open, locks bin/BG3RTX.exe and breaks the build.

## Base frame rate (`sup/perf`, unmerged; 2026-10-30)

Default-off `rt_alpha_stream` speeds alpha mapped-memory reads: capture CPU
4.224 -> 2.973 ms/frame, pre-draw 2.989 -> 1.730. Build included ~0.97 ms;
submit copies ~0.48 ms. GPU chain / scene / RR ~2.54 / 1.67 / 2.36 ms.
All stats-off windows cap at 30 fps, even with restored-after-test driver/game
cap controls and immediate presentation. WDDM GPU busy ~10.6 vs frame 33.3 ms.
FPS promotion gate NOT QUALIFIED; foreground permission asked, not received.
Primary median 98.969 -> 98.968%, exact-copy CTest passes; build 15/15, no VK
errors, driver events or dumps. No bursts. Do not merge/promote until uncapped
speed repeats pass. User check/decision at top of PROGRESS_CODEX; evidence
`docs/evidence/perf-base-20261030.json`, job `sup-perf-game-uncapped`.

## Hidden floor squares (2026-10-30, sup/hidden-quads)

Fixed: `rt_capture_discard=1` (restart layer; live A/B key). An always-OpKill fragment
shader's helper boxes were captured by XFB as opaque casters. Gate 0/1 twice:
29.4k opaque-front px -> 132 / 127, both square ROIs -> 0 / 1; primary match improves
96.09% -> 96.95-97.19%. BVH build 1.387 -> 1.368 ms, no VK errors or driver faults.
Check Blushing Mermaid - 96h 39m near crates and stage edge. Refits and instance
indexing are not the cause. Split-off hid the boxes by capture-buffer overflow.
See PROGRESS_CODEX and TRACK_D_RT section 11 for the deciding run and diagnostics.

## FG pairing / UI follow-up (merged main 5e78b88; default follow-up)

Read the top of PROGRESS_CODEX. Code `066b38b`: submit-order color/UI and a locked
camera snapshot behind fg_input_copy; disabled UI tags clear their SDK cache entry.
200/200 same-NGX depth/MV comparisons valid; 245 selected submit stamps align.
No causal input lag established. MV scale agrees with camera motion (0.9994).
Final copy 0/1 twice: world off 7.21 / 8.38, on 3.87 / 8.38; UI off 7.24 / 7.31,
on 3.97 / 8.35. Second UI repeat is worse. Steady 2.00, capture 1.70-2.30 with
16 timer resets. No new VUID IDs; SDK latency-marker threading warning remains.
Five UI combinations repeated; baseline near-UI foliage support is zero. No winner.
Default follow-up: copy 1 for input correctness. Four-window means +/- SE:
world 7.039 +/- 0.352 -> 7.940 +/- 0.100; UI 7.458 +/- 0.509 -> 7.835 +/- 0.447.
Observed worsening is below simultaneous 95% noise bounds (1.075 / 1.663).
This does not qualify full flicker resolution. UI tag 1, PQ 0, hudless 1 unchanged.
RR pool idle/reset raced the FG pacer and stalled; fenced pool retirement survived
64 rollovers. PNG encoding is asynchronous, but GPU readbacks still stall capture.
Temporary virtual mouse changes were removed. Build 14/14, shaders 107/107, QA pass.
All 44 new completed raw bursts deleted. Evidence: fg-followup-20261030.json.
Next: normal-cadence GPU readback with native endpoint IDs and actual hover/foliage overlap.

## UI inputs (sup/ui-inputs; optional correction stays off)

Late FG depth is zero in 64/128 samples; MV gains 3 non-finite components after RR/post.
Missing event-wait tracking also declared layouts 3/2 instead of observed 5/5.
fg_input_copy=1 preserves the NGX depth/MV pair, guarded by the SDK consumption fence,
and corrects event-wait tracking. Both inputs match in 68/68 private copies; pair-copy
GPU median 0.026 ms. No new validation errors in the deciding capture. Keep default 0:
continuous rotation and matched in-world UI coverage are not qualified; full flicker
resolution is not claimed. See top of PROGRESS_CODEX, TRACK_B_FG and the evidence JSON.

## State (before UI input integration: main f257d00)

Defaults on since 2026-10-29: rt_scene_wait (now 12 ms), rt_dyn_hold=4,
fg_current_destroy_only=1, rt_sun_roof_up 50 / pad 3, rt_prepass_by_ordinal=0,
rt_cascade_alpha=0, rt_cascade_roofs_trust=0 (cascade casters themselves stay off by
default; the user runs them on as "Roof sun shadows"). ls_box=0 (ls_box_unshadowed=0).
Layer fixes of the night (in-world UI): RT scene prepass found per frame, not by ordinal;
held alpha views checked for handle reuse; UI portraits no longer replace the G-buffer;
rt_scene_wait resumes at once after a gap. Pan bursts keep all 16 'final' frames.

## Measurement tools (D:/BG3_Tools/sup_scripts)

- sunmap.py out.png <burst dirs>: sun buffer after resolve (r RT, g game mask, b applied):
  leak = applied lit where the game is dark, dark = applied dark where the game is lit.
- flicker_final.py <burst dirs>: reprojected frame-to-frame change of the final image per
  material class (4 = translucent/foliage, 8 = standard).
- tflk.py (static) and buffers.py (rotation vs static) for the pre-RR buffers.
- Fixtures tests/fixtures/sup-*-inn-fg2.json: save "The Blushing Mermaid - 96h 39m".
  Never leave ls_stats=1 / rt_stats=1 in a cost job: their counters cost ~6.6 ms.

## Open (ranked)

1. User re-check of the hidden floor squares fix and the night's fixes (PROGRESS_CODEX.md).
2. Red square blocks on foliage with RR off: not reproduced since the scene-stall fixes.
3. FG temporal artefact around in-world UI while the camera moves (also with RT off): the
   game recreates its UI and hudless images often (~160 times in 4 minutes in one user
   session); our tags follow. Not yet measured on generated frames.
4. Face/skin flicker (P4 of the supervisor list): no repro data.
5. Phase plan: OMM foliage (P4), EN2/D-EM gates and SHaRC v2 (P5), water (P6).

## Cost (inn upper floor, Ultra, FG 2, stats off)

Local-light shadows 0.93 ms (Fast 0.75); rt passes 1.2 ms; scene build 2.6-2.9 ms;
RR 2.4 ms. rt_fast_trace=1 gives no trace gain and +0.3 ms build.
