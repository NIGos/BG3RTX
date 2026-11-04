> Superseded. This is historical evidence. See the [2026-11-04 release report](PROGRESS_CODEX.md) for current decisions.

# Current goal (user, 2026-11-03 night, v3)

Short form of the /goal text. AGENTS.md is binding. Work in this order; each stage ends with a short consolidation.

1. DONE 01:24: smoke_ultra with every default valid (0 RT capture drops, VRAM in budget).
2. DONE: shader specialization migration merged; scc1 valid, 0 DXC on converted toggles. Variants 2,946 -> 147
   before null-key cleanup; full merge-green passed.
3. DONE: visible-defect fixes implemented. Dialogue moving sun shadow fixed by default (cascade identity);
   user accepts AO self-occlusion. Water camera correction looks right to the user; water flicker fix pending
   corrected wf9 qualification (static delta and smooth Q/E windows), not yet a quality pass.
4. Qualification partly done: OMM and box lights (honour shadow flag) qualified in Ultra, integrated smoke valid.
   Converted RTX Hair grooms work in game at about 40% less cost than native cards; native card path stays off
   (+2.3-6.3 ms RT). CPU perf controls and rr_hair_resolve retired 2026-11-04 after rejection. GI reprojection guards and generated
   rows still await valid gates (genrows-gate). Hair/DoF cause found: replayed DoF/bloom weights flip per frame;
   rr_post_temporal on post-temporal is READY_FOR_LIVE, off pending energy/quality/cost checks.
5. Effects IN PROGRESS: water hidden/off-screen hit shading is on main, experimental/off; water-hit-cost adds
   equivalent sparse-light/culling optimisation. M4 cov-paks2 adds COLOR_0/Tileset publication and UV joins;
   incremental catalogue generation is blocked on LSLib DLLs. Lava/HeightGradient/HLOD and hit-AO gaps remain.
   M6 SHaRC stays off until it beats the PT reference without losing energy. gpu-opt1 has compact hit state,
   alpha metadata cache and empty water-tile skip, all off pending qualification. rt-continuity and gpu-opt2
   are open lanes at main, with no implementation/result recorded yet.
7. Backlog to close (in dead time, smallest first): null water MV/glint keys removed; rejected CPU perf/hair resolve/static-cascade controls retired 2026-11-04; audit remaining keys (IP grass if still useless, generated rows if superseded), delete the dropped Codex refs (user approved), clean merged
   worktrees and dead captures (test-main captures ~39 GB), fold merged progress fragments (59 folded on 11-03), S8 ambient ratio 1.44
   question, inn material records, lava, rf_hit_gi AO gap (needs a pack with instanced rows), dynamic/cascade BLAS
   relocation, ls_box=2 (qualified in Ultra), M5.5 steps 2-4, every READY_FOR_LIVE branch run.
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
- No main commits during merge-green. Camera turns use held Q/E; mouse turns are never smooth here.
- Launcher categories are Lighting/Surfaces/Performance/Memory/Advanced; experimental controls use Advanced.
- Never: saves, backups, history rewrite, force-push, snapshot tags.
