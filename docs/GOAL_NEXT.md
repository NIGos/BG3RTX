# Current goal (user, 2026-11-03 night, v3)

Short form of the /goal text. AGENTS.md is binding. Work in this order; each stage ends with a short consolidation.

1. DONE 01:24: smoke_ultra with every default valid (0 RT capture drops, VRAM in budget).
2. Shader migration: merge `variant-migration` (150 variants) with main, all spec checks N/N, one merge-green,
   live scc1 extended to the converted keys.
3. Visible defects first:
   - moving sun shadow in dialogues (port and camp): cause is the cascade casters (GUI "Roof sun shadows" =
     rt_cascade_casters; user: off = static); fix branch fix-cascade-dialogue (static casters only), check camp
     canopies too; dialogue batch with the occluder diagnostic
     confirms and checks AO / hair darkening;
   - water reflections switching on/off while turning: wf8 reflection-only metric decides ours vs game SSR,
     then the fix (temporal hysteresis of the screen-hit decision, then hit shading for hidden/off-screen hits).
4. Qualify what is ready: native hair (hair_qual), OMM om4, CPU perf A/Bs, gi reprojection guards, m4_generated.
   Pass = default on or in Ultra; fail = remove or keep off with the reason written down.
5. Effects: true off-screen reflections (REFLECTIONS_PLAN: water hidden/off-screen hits shaded, sky in the exact
   direction, replace or blend the game's SSR); M4 material coverage (mesh-to-material from the paks, vertex
   colour in the capture for lava/Tileset, HLOD two-sided), hit-AO gap; M6 SHaRC only if it beats
   the PT reference; GPU optimisations from PERF_AUDIT_GPU (lower-preset trace scale, compact hit state).
7. Backlog to close (in dead time, smallest first): remove null/rejected keys (water flicker keys, glint, IP grass
   if still useless, generated rows if superseded), delete the dropped Codex refs (user approved), clean merged
   worktrees and dead captures (test-main captures ~39 GB), fold progress fragments, S8 ambient ratio 1.44
   question, inn material records, lava, rf_hit_gi AO gap (needs a pack with instanced rows), dynamic/cascade BLAS
   relocation, ls_box=2 (waits for the user's visual check), M5.5 steps 2-4, every READY_FOR_LIVE branch run.
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
