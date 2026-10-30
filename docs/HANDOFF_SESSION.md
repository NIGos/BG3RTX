# Session handoff — 2026-10-30 09:00 (hidden floor squares fixed)

Read AGENTS.md, then the top of docs/PROGRESS_CODEX.md, then D:/BG3_Tools/USER_STEERING.md
(newest entries first). The user decides by FG on (fg=2), real presentation, visible window.

## Permission

Live jobs only through scripts/test-queue.ps1; visible unfocused FG jobs are allowed
(14:29 authorization). Skip while USER_PLAYING exists or a game the runner did not launch
runs. Never hold results.md open. No saves, backups, snapshot tags, force-push or junction
cleanup. The launcher BG3RTX.exe, if left open, locks bin/BG3RTX.exe and breaks the build.

## Hidden floor squares (2026-10-30, sup/hidden-quads)

Fixed: `rt_capture_discard=1` (restart layer; live A/B key). An always-OpKill fragment
shader's helper boxes were captured by XFB as opaque casters. Gate 0/1 twice:
29.4k opaque-front px -> 132 / 127, both square ROIs -> 0 / 1; primary match improves
96.09% -> 96.95-97.19%. BVH build 1.387 -> 1.368 ms, no VK errors or driver faults.
Check Blushing Mermaid - 96h 39m near crates and stage edge. Refits and instance
indexing are not the cause. Split-off hid the boxes by capture-buffer overflow.
See PROGRESS_CODEX and TRACK_D_RT section 11 for the deciding run and diagnostics.

## State (before this integration: main 6e3b997)

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
