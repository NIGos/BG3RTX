# PAUSED: two concurrent BG3 test instances

Paused at the user's request on 2026-10-26 at about 21:10. No game, lock holder or
watcher of this task is running. `graphicSettings.lsx` has the player's values
(Fullscreen=1, FakeFullscreenEnabled=0); all 14 entries match the copy taken before the
first test. No save was moved, and `_bg3rtx_parked` does not exist.

## Feasibility: yes

BG3 runs two instances side by side with no workaround. There is no single-instance
check: both start, and both keep `SteamAppId`.

- **Feasibility run (20:42-20:46).** Same bin, tags `fa` and `fb`, headless, background,
  borderless. S1 (Foundry, `QuickSave_3784`) and S3 (`Campsite - 27h`) were both in
  gameplay for 150 s and both kept rendering (draw counts are in `bg3rtx.fa.log` and
  `bg3rtx.fb.log`). There was no crash and no input interaction.
- **GPU memory:** 26.1 GB used in total, 98% GPU utilisation. The RR evaluate time was
  3.6-5.3 ms, against 2.6 ms alone, so timings from shared runs are contended.
- **Script Extender:** BG3ScriptExtender.dll loads in both processes; the modules were
  checked during the sweep. The per-instance bridge works for fa:
  `bg3rtx\fa\lights.txt` and `world.txt` were written, and the layer read them.
  fb's folder only received the heartbeat, with no lights or world files. This is not
  yet explained; it may be specific to Campsite, or the Lua failed there. Check it.
- **First feasibility attempt:** my test script died because `launch-bg3.ps1` did not
  return the PID (since fixed). Its mutex was abandoned, another agent's old-style
  auto-run took it and killed every bg3.exe, and display values were left changed. I
  restored them afterwards. This shows why the old "kill every bg3.exe" behaviour must
  go, and why a script must never die while holding the lock and leave games running.

## Done (uncommitted work is in the WIP commit)

- **Layer:**
  - `BG3RTX_RUN_TAG`: when set, the log, auto.done, trace.request and capture folders
    carry the tag (`util.cpp`, `tracer.cpp`, `streamline.cpp`).
  - `BG3RTX_SAVE_LOG`: records the save folders this game creates. It hooks
    CreateDirectoryW and MoveFileExW through the IAT (`savefilter.cpp`).
  - The SE bridge token `bg3rtxse=<tok>` is read from the command line, and the bridge
    folder becomes `bg3rtx\<tok>\` (`sebridge.cpp`).
  - The hot-reload cleanup no longer deletes the core copies of another live process.
- **Lua bridge:** reads the token through `Ext.Utils.GetCommandLineParams()` and falls
  back to `bg3rtx/`. It is installed into the game folder, and is
  backward-compatible.
- **`scripts/gamelock.ps1` (locks):**
  - Two slots, `Global\BG3RTX_game_slot0` and `slot1`.
  - `Global\BG3RTX_game_run` is the exclusive lock.
  - `excl_wait` gives exclusive runs fairness.
  - Shared runs need a holder process: an older script takes only the global mutex, so
    something must hold that mutex while any shared game runs. Two processes cannot
    both own it, so a hidden holder process owns it for the shared runs and releases it
    about 6 s after the last slot is free.
  - Simulated with `Local\` test names: two shared runs ran concurrently; an old-style
    run and an exclusive run waited; a later shared run waited for the pending
    exclusive run.
- **`scripts/gamelock.ps1` (display reference count):**
  - `%LOCALAPPDATA%\BG3RTX\display_prev.json` plus `display_users\<pid>.json`. An entry
    counts as alive while its script or its game is alive.
  - Crash recovery restores the values when no user is alive. It also takes over a
    legacy `captures\display_prev.json`.
  - Simulated with a fake ensure-settings, including a crash: correct.
  - In the real sweep, the second run kept borderless while the first was alive, and the
    last run restored.
- **`auto-run.ps1`:**
  - New switches: `-Exclusive` and `-Tag`.
  - A per-checkout and per-tag mutex.
  - Shared vs exclusive: shared means `-Background` and not RealPresent and not
    Exclusive.
  - It tracks only its own PID (`captures\game[.tag].pid`) and kills only a stale game
    of this checkout.
  - try/finally cleanup.
  - Saves are parked only from the layer's save log, and only those not present before.
  - `run.json`, with mode and exclusive flag, goes into each capture.
  - Shared runs pass `bg3rtxse=slotN`.
  - Prints "Run mode: shared (slot N): timings are not exclusive".
- **Other scripts:**
  - `launch-bg3.ps1` returns the process (`-PassThru`).
  - `scene-sweep.ps1`: two lanes (`-Tag a/b`), `-Parallel 1|2`, `-Exclusive`, and a
    Seconds/Mode column.
  - `scripts/qa`: `--parallel` (default 2) and `--exclusive`, with run_mode and
    exclusive in run.json. The report marks shared timings. `live_phases.py` takes
    "tag" from phases.json.
- **Docs:** TEST_SCENES (saves paragraph), QA (concurrency paragraph), QUALITY R7
  (performance only from exclusive runs), SE_BRIDGE ("Two games at once").

## In progress / observed

- **Sweep S1+S3 (`scene-sweep.ps1 -Only S1,S3`):** both games ran together (slot0 and
  slot1). S3 completed (`captures\20261026-210628-b`, run 89 s).
  - S1 timed out: no auto trace, although it was in gameplay at ~1130 draws for 4 min
    (`captures\sweep_S1.out`, `bg3rtx.a.log`).
  - Not yet known whether this is caused by concurrency or is the existing S1 `-Save`
    behaviour with `auto_after_load=1`, waiting for a loading screen the Foundry load
    may not show. Check with a single S1 run, and look at the `auto_trigger` logic in
    `layer/tracer.cpp`.
  - The sweep's "Seconds" include the ~18 min the lanes waited for other agents' runs,
    so they are no throughput figure.
- **Save files:** at every game launch, the 10 newest save files get a new mtime with an
  identical size. This happened at 11:26, 20:42 and 21:05, so it predates this work;
  Steam Cloud or the game is the likely cause. Nothing is moved.
- **The game rewrites `graphicSettings.lsx` without the Fullscreen and
  FakeFullscreenEnabled entries:** seen after other agents' runs.
  - `ensure-settings --restore` re-adds them; it reports "added".
  - Every background run re-applies borderless before its launch, so a later game never
    reads the default.

## Next steps

1. Find out why S1 gave no trace in the shared sweep: run
   `auto-run.ps1 -Background -Save QuickSave_3784 -Tag a` alone. If it also fails
   alone, the cause is pre-existing.
2. Find out why fb (Campsite) wrote no `lights.txt` and `world.txt`. Read
   `bg3rtx\slot1\error_*.txt`, and see whether the Lua token parse took the token from a
   neighbouring argument.
3. Verify with 4 scenes, `scene-sweep.ps1 -Only S2,S3,S5,S8` (shared), against the same
   with `-Exclusive`. Measure the wall-clock time from the first launch, not counting
   the lock wait; record start and end in the lane outputs. Compare the captures.
4. After each sweep: run `scratchpad\lsxdiff.py` against the original copy (it is also
   checked in nowhere, so take a fresh copy before starting), check that
   `_bg3rtx_parked` and the save list are unchanged, and check that
   `%LOCALAPPDATA%\BG3RTX\display_prev.json` is gone.
5. Add the measured speed-up to `docs/TEST_SCENES.md` in a "Concurrent runs" section:
   the lock scheme, tags, holder, display reference count, bridge tokens and limits. QA.md
   links to the `#concurrent-runs` anchor, which does not exist yet.
6. Final commit, with messages ending in the Co-Authored-By line. Do not merge into
   main.

## Risks known so far

- If the holder process is killed while shared games run, the global mutex is
  abandoned, and an old-style script could start and kill those games.
- A `-KeepRunning` background game keeps its display entry alive; the values are
  restored by the next run after it exits.
- Shared timings are contended (RR 2.6 to 5.3 ms) and must never be used for budgets.
