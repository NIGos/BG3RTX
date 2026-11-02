# Current goal (user, 2026-11-01)

Verbatim Italian text set with /goal. Stages in order; each ends with a consolidation.

1. Live check of the bugfix round: smoke_ultra (Ultra, all qualified keys on, rt_hit_material_recycle=1), scene_wait,
   water stability (0 uncomposed frames), in-game shader cache (second launch without compiles, Ctrl+Shift+F9
   immediate), FG letterbox with a desktop of a different resolution. What passes: default on or automatic.
2. GUI rationalised as in the approved GUI_AUDIT (28 -> 8 experimental entries; water and materials as levels;
   emission turns on rt_emissive; current-sun and exact-k developer only).
3. M5 water: m5_promotion batch (S8, Flymm, camp) -> water in Ultra if it passes. Then M5.5 steps 2-4 only if measurable.
4. M4: S6 diagnostic (job m4p5), promotion of the bundle without rf_hit_shade; rf_hit_shade stays experimental until
   its bias is within the spread (user decision: threshold unchanged); rf_hit_gi + direct/box correction at S6;
   material coverage (blocker ranking: meshes without an S8 row, camp grass, S6 lava/terrain); box light ls_box=2
   after a visual check.
5. M6 step 1: camp/inn packs (replay_capture/ambient), native/reference ambient ratio on 3 scenes; gi_far gap
   (-0.3% live against -2% predicted); then SHaRC. Systematic correction only if consistent on every scene.
6. Release hardening for a Nexus beta: release defaults, long sessions, acts 2-3, installer, README, licences;
   later a diagnostic mode that builds a package (logs, settings, GPU info, crash dump) with a save the user picks,
   copied read-only.

M2 only if a visible defect needs it.

CONSOLIDATION (after each stage or ~6 hours): non-regression batch + smoke_ultra; meta-analysis and gate audit; fix
bugs; remove superseded keys; HANDOFF <= 80 lines, PROGRESS ~150 (fold docs/progress/ fragments); clean worktrees with
scripts/remove-worktree.ps1, merged branches and dead captures.

HOW: 2-3 streams per area, one agent fixes in sequence per stream (resume it); briefs with evidence and file:line,
minimal fix, no new frameworks, report <= 15 lines; the coordinator merges, reads logs, does not implement. Tools:
always main's version (agents merge main before gates or long tools); progress every ~30 s and cache; machine-wide
DXC slot cap; builds/tests BelowNormal, game Normal. Grouped merges in one merge-green. Offline before live; live =
one precise observable with its predicted delta in predicted.json, exposure held, energy on scene colour (after the
water for water), gates on every window including capacity and scene timeouts. Every feature also in smoke_ultra.
Live tests: foreground if the PC is free; unfocused mode if the user is at the PC; no live if the user asks for a
pause. 2-5 min per scene. Disk: keep packs and reference baselines, delete the rest.
Never: touch saves and backups, rewrite history, force-push, move snapshot tags.

REPORT: at the end of each stage and consolidation, 10 lines at the top of docs/PROGRESS_CODEX.md (done, merged,
open, what to try in game and where). Questions there; meanwhile continue with other work.
