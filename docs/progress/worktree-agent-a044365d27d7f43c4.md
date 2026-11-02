## Stage 6 first pass: release package, installer, README, licences (branch worktree-agent-a044365d27d7f43c4, offline, 2026-11-02)
- Release defaults audit (565 `cfg_*` call sites against the package profile): no new diagnostic leak on.
  Pinned `rt_hit_reference=0` in data/release.ini (already 0). Kept on by design and doubtful items: docs/release/RELEASE_BETA.md.
- scripts/package.ps1 fixed: it shipped no common/ headers (21 shader files include them: the RT path could not
  compile in a player package) and no asset DB. Now an explicit list, versioned dist\BG3RTX-<ver>.zip (69 MB) + symbols zip.
- No implicit layer or registry (launcher sets VK_ADD_LAYER_PATH per process): scripts/release/install.ps1 /
  uninstall.ps1 (+ .cmd) copy to %LOCALAPPDATA%\Programs\BG3RTX and fully remove it (shader cache included),
  never follow links, refuse game and Larian folders. Tested in scratch folders (install, update, guard, uninstall).
- docs/release/README.md (players), THIRD_PARTY.md (shipped notice), RELEASE_BETA.md (audit, licences, what remains,
  diagnostic-package design).

## Long sessions: log cap, VRAM line, trend gates, long_session job (same branch, offline, 2026-11-02)
- bg3rtx.log capped at 64 MB (common/log_rotate.h, layer/util.cpp): the full file and the previous session's
  log move to bg3rtx.prev.log; at most two files. CTest log_rotate_test (221 checks).
- New layer line `[mem] device-local VRAM of this process: used / budget / heaps` next to each `[pace]` line
  (VK_EXT_memory_budget, physical-device query only; restart to get it).
- queue_gates.py `"trend": true` gates: tail windows' median <= baseline max + baseline spread + print precision.
  6 new checks in queue_gates_test.
- tools/live_batches/long_session (READY_FOR_LIVE, not run): camp, Ultra + FG 2, 37 min, 22 cycles of 5 cost
  windows + a 10 s rt_stats pool window; scene wait / capacity in all 110 cost windows; trend gates on VRAM, mesh
  pool, AS, frame time median/p99, pool records (cycles 20-22 vs 2-4); predicted.json all flat. Dryrun: all checks passed.
- docs/USER_SETTINGS.md: Ultra levels (RT water + Refraction, + Bounce, Hidden hits, Far-field 6 m), pool alpha
  and fg_letterbox automatic, log cap.
- Build + 96 CTests passed.

## User decisions needed
1. **SHaRC headers:** shipping them is source distribution under the NVIDIA RTX SDKs licence (object code only).
   Not shipped now (sharc=0 everywhere). Before SHaRC ships: precompiled SPIR-V, or ask NVIDIA?
2. **NVIDIA notification:** the RTX licence asks for one before a commercial release. Notify for the free beta anyway?
3. **Larian data:** the asset DB holds numbers extracted from the game's .pak files. Ship it (as now), or check
   Larian's mod terms first?
4. **BG3RTX's own licence:** the repository has none. Which one (it must not put the NVIDIA parts under an
   open-source licence)?
5. **Licence texts:** DXC (NCSA) and Vulkan-Headers texts are not on this PC; OK to fetch them into external\licences\?
- Open: long-session live run (needs steering), Act 2 / most of Act 3, non-5090 GPUs, fresh-PC install test.
