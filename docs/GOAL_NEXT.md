# Current goal (proposed 2026-11-03, night session)

Replaces the goal of 2026-11-01 (its stages 1-3 are done; stages 4-6 continue below). Stages in order; each ends with
a consolidation. State at the start: main c17d27d; the spec-constant migration (2986 -> 150 shader variants) is being
merged with main on branch `variant-migration`; live tests were paused by the user and resume now.

1. Shader migration: finish the merge of `variant-migration` with main (all spec equivalence checks N/N, pp-diff 0
   on unchanged requests), one merge-green, then live `scc1` extended to the converted keys (no DXC compile on
   toggles, energy and GPU ms unchanged).
2. Release-preset verification: smoke_ultra with every default (VRAM guard, compact and elastic capture on; the
   elastic-drop fix), 0 RT capture drops, VRAM within budget. If it fails, fix or turn the failing key off.
3. Pending lives, one launch per scene, unfocused if the user is at the PC, foreground otherwise:
   - dialogue saves with the occluder diagnostic (branch worktree-agent-a0c9be1ab336eae1a): numeric cause of the
     character shadows (AO, hair cards, sun, foliage), then the fix;
   - native hair qualification (branch worktree-agent-a78f48005e3b350bd: async builds, water hair): into Ultra if it
     passes;
   - OMM bake om4 (branch worktree-agent-a53f0b584fa39317b): 0 violations, measured ms saved;
   - A/B of the CPU perf keys (perf_hitmat_lists, perf_hook_state, perf_submit_batch): keep what saves time;
   - gi reprojection guards as spec constants (already in the migration);
   - m4_generated rerun (bush pairing, two-sided rows).
4. M4 rest: material coverage (meshes the camera never drew: match mesh to material from the paks), lava flowmap,
   hit-AO gap with a replay pack that contains instanced rows; ls_box=2 after the user's visual check.
5. M6: SHaRC only if it beats the PT reference offline; no global ambient correction unless consistent on 3 scenes.
6. Release: GPU optimisations of PERF_AUDIT_GPU (rt_trace_scale for lower presets, compact hit state, SER where
   available); launcher QA (keyboard, high contrast, DPI); installer compiled and tested in a disposable VM; licence
   texts for DXC and Vulkan-Headers; assetdb and VT masks generated on the user's PC; long sessions and acts 2-3.

Consolidation after each stage or about every 6 hours: non-regression batch + smoke_ultra; gate audit; fix bugs;
remove superseded keys (the null water flicker keys, rejected keys); HANDOFF <= 80 lines, PROGRESS ~150 (fold
docs/progress/); clean merged worktrees with scripts/remove-worktree.ps1 and dead captures.

How: AGENTS.md is binding. Codex (codex exec, workspace-write sandbox, BelowNormal, never resume) is the default
executor for verifiable offline work; Claude agents for live-log diagnosis; the coordinator briefs, verifies,
merges and never trusts a report without re-running its checks. Heavy DXC work one at a time; finish build
improvements before spending compile time they would save. Offline before live; predicted.json before each run;
gates on every window. Never: saves, backups, history rewrite, force-push, snapshot tags.

Report: 10 lines at the top of docs/PROGRESS_CODEX.md after each stage and consolidation (done, merged, open, what
to try in game and where). Questions for the user there; meanwhile continue with other work.
