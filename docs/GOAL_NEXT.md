# Current goal (user, 2026-11-03 night)

Short form of the /goal text. AGENTS.md is binding. Work in this order; each stage ends with a short consolidation.

1. Release preset holds: smoke_ultra with every default (VRAM guard, compact, elastic capture with the drop fix):
   0 RT capture drops, VRAM within budget, no frame >100 ms. Fail: fix or turn that key off.
2. Shader migration: merge `variant-migration` (150 variants) with main, all spec checks N/N, one merge-green,
   live scc1 extended to the converted keys.
3. Visible defects first:
   - moving sun shadow in dialogues (port and camp): cause is the roof-shadow logic (user: off = static);
     fix branch fix-roof-moving-shadow, check camp canopies too; dialogue batch with the occluder diagnostic
     confirms and checks AO / hair darkening;
   - water reflections switching on/off while turning: wf8 reflection-only metric decides ours vs game SSR,
     then the fix (temporal hysteresis of the screen-hit decision, then hit shading for hidden/off-screen hits).
4. Qualify what is ready: native hair (hair_qual), OMM om4, CPU perf A/Bs, gi reprojection guards, m4_generated.
   Pass = default on or in Ultra; fail = remove or keep off with the reason written down.
5. Effects: M4 material coverage (mesh-to-material from the paks), lava, hit-AO gap; M6 SHaRC only if it beats
   the PT reference; GPU optimisations from PERF_AUDIT_GPU (lower-preset trace scale, compact hit state).
6. Release: launcher QA (apply-mode badges, keyboard, high contrast, DPI), installer in a disposable VM, missing
   licence texts (DXC, Vulkan-Headers), data generated on the user's PC, long sessions and acts 2-3.

Efficiency rules:
- Lives in the foreground when the PC is free, unfocused when the user is at the PC, none when paused. Group jobs
  by save; 2-5 min per scene; predicted.json first; gates on every window.
- Dead time is never idle: while a live or merge-green runs, Codex works offline on the next fix, an audit, a
  doc or a test; while Codex compiles, run lives that don't depend on it. Keep up to 3 Codex tasks on disjoint
  files, BelowNormal priority. Heavy DXC one at a time.
- Codex executes verifiable offline work; Claude agents diagnose live logs; the coordinator briefs, re-runs the
  checks, merges with merge-green and reads the deciding numbers. Finish build improvements before spending the
  compile time they save.
- Consolidate about every 6 hours: smoke_ultra, gate audit, remove superseded keys, HANDOFF <= 80 lines,
  PROGRESS ~150, clean merged worktrees and dead captures.
- Report: 10 lines at the top of PROGRESS_CODEX after each stage (done, merged, open, what to try in game and
  where); questions there, keep working meanwhile.
- Never: saves, backups, history rewrite, force-push, snapshot tags.
