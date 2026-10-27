# Test scenes

The player's saves cover every act, so they are the multi-scene test suite for
the review checkpoints in `QUALITY.md`. `scripts/save_index.py` lists all saves
with their level (from `SaveInfo.json` inside each `.lsv`); the save thumbnails
confirm the view and the time of day.

Load one with `scripts/auto-run.ps1 -Save "<folder substring>"`.

`bg3.exe` has an undocumented `-loadSaveGame <save folder name>` argument
(found next to `-continueGame` in the executable's strings; "GameStateInit:
These commands should be mutually exclusive"). `auto-run.ps1 -Save` passes the
matching folder name. Neither the full `.lsv` path nor file timestamps work:
`-continueGame` picks the newest save by the save time stored inside each
package.

`layer/savefilter.cpp` (env `BG3RTX_SAVE_ONLY`) can also hide every other save
from the game's listing of `Savegames\Story`. It works, but `-continueGame`
then stays in the main menu, so it is kept only as a tool. Nothing on disk is
ever moved for a test. That matters because Steam Cloud syncs that folder
(`remotecache.vdf` lists every save), and moving saves out, even temporarily,
could look like a deletion to it.

Runs with `-Save` set `auto_after_load=1`: the trace waits for a loading screen
after the 3D main menu, since other scenes draw about as much as the menu.
`scripts/scene-sweep.ps1` runs every scene in turn and writes
`captures/scene-sweep.json` (`-Parallel 2`: two at a time, see "Concurrent runs").

Saves this run's game writes (the layer records the folders it creates,
`BG3RTX_SAVE_LOG`, `layer/savefilter.cpp`) are parked in `Savegames\_bg3rtx_parked`
instead of joining the player's list. Saves of any other game, a concurrent test
run's or the player's, are never moved; new folders not written by this run's game
are only listed in its output. Save content is never modified. A full copy of the player's profile and saves, taken
2026-10-26 before the first `-Save` run, is in
`D:\BG3_Save_Backup\20261026_PlayerProfiles`. It has 227 files and 2.12 GB, and
the newest save's hash matches the original. To restore, copy it back over
`%LOCALAPPDATA%\Larian Studios\Baldur's Gate 3\PlayerProfiles` with the game
closed.

| Id | Save (substring for `-Save`) | Level | Lighting case | What it stresses |
|---|---|---|---|---|
| S1 | `QuickSave_3784` (Steel Watch Foundry) | CTY_Main_A | interior, many point/spot lights | local-light shadows (D-LS), the default `-continueGame` scene |
| S2 | `Rivington - 74h` | BGO_Main_A | outdoor day, hard sun | sun shadows and penumbra, cobblestone GI, crowds (skinned characters) |
| S3 | `Campsite - 27h` | WLD_Main_A | outdoor day, trees and grass | alpha-tested foliage, back-lit translucency, alpha-buffer capacity |
| S4 | `Goblin Camp - 18h` | WLD_Main_A | outdoor overcast, dense vegetation, fog | foliage shadows, fog over GI |
| S5 | `Grymforge - 32h 59m` | WLD_Main_A | underground, lava and torches | emissive lava (D-EM), local lights, GI colour bleed |
| S6 | `Adamantine Forge` | WLD_Main_A | huge lava pool, metal machinery | emissive bounce, metal reflections (D-RF) |
| S7 | `The Blushing Mermaid - 96h` | CTY_Main_A | tavern interior, candles, cut-away roof | local shadows, casters hidden by the roof cut-away |
| S8 | `Water Queens House` | CTY_Main_A | marble temple, water, columns | reflections on water and polished stone (D-RF) |
| S9 | `Crèche Yllek` | CRE_Main_A | gilded interior | glossy metal reflections, warm GI |
| S10 | `Shadow-Cursed Lands - 52h` | SCL_Main_A | darkness, magic light shafts, fog | low light, local and magic lights, noise in the dark |
| S11 | `Gauntlet of Shar - 57h` | SCL_Main_A | dark, purple lights, polished stone | reflections in the dark, local lights |
| S12 | `Moonrise Towers Main Floor - 65h` | SCL_Main_A | dark interior, torches | local shadows, GI at low light |
| S13 | `Mind Flayer Colony` | SCL_Main_A | organic, emissive pods | emissive surfaces, wet-looking materials |
| S14 | `Lower City - 91h` | CTY_Main_A | city day, stairs and walls | sun shadows on architecture, large view distance |

Checkpoint routine: for each scene, a feature-off and a feature-on capture of
the same frame (`scripts/live_phases.py` toggles live inside one run), the A/B
sheet from `scripts/ab_compare.py`, GPU costs, and the non-finite and
validation checks. `python -m scripts.qa run --scenes ...` automates the
captures and the objective checks ([QA.md](QA.md)). The art-intent review looks at each A/B sheet. It asks
whether the change keeps the look the artists lit for (key lights, character
readability, colour grade) as well as being physically better.

## Test queue (how in-game tests are run)

Only one process starts the game: `scripts/test-queue.ps1`, run by the coordinating
session. Agents and sessions never launch the game themselves. They add a job, a JSON
file in `captures/queue/pending/`, and the runner executes the jobs oldest first, one game
at a time. The file header lists the fields (`tree`, `save`, `set`, `traces`, `repeat`,
`args`, `phases`, `patterns`, `stop_on_dump`, `post`, `command`, and for sessions
`session`, `warmup`, `trace_gap`, `hold`).

Each run gets a standard record in `captures/queue/results.jsonl` and `results.md`:
- the outcome and its duration;
- the NVIDIA device-lost events (System log, nvlddmkm id 153);
- new Aftermath dumps;
- the last log line matching each of the job's `patterns`;
- the build stamp, flagged when the build changed during the run.

A run's log is kept when it faulted.

### Sessions: one game for many jobs

A launch and load costs about 55-60 s of a 90 s run (core loaded at +4 s, 31k pipelines
created, the level ready at about +55 s). Most jobs need none of it: A/B toggles,
emissive calibration, QA effect toggles and guard or counter checks only need other
settings, which the layer reads live, a rebuilt core, which hot-reloads, and traces.

A job with `"session": true` can share a running game. Consecutive session jobs with the
same `tree`, `save` and `args` use one game, started by `auto-run.ps1 -Session`. Each run
of such a job:
1. waits for a rebuilt `bin\bg3rtx_core.dll` to load (`[hot] core generation N loaded`),
   and records the generation (`core_gen`);
2. writes its `set` keys into `captures\auto.ini` and waits for `[cfg] settings read`;
3. starts its `phases` (live_phases `--offset`: only lines of this run count);
4. waits `warmup` seconds (default 10);
5. takes `traces` traces on demand, `trace_gap` seconds apart (default 8), by creating
   `trace.request`, which the layer polls once per present by mtime. `traces: 0` is valid;
6. waits `hold` seconds (default 0), for counter or guard lines;
7. restores the session's `auto.ini`;
8. matches `patterns` in its own slice of `bg3rtx.log` (byte offsets), not the whole log.

The game is closed when:
- the next job cannot use it;
- the queue is empty;
- a fault occurs: nv153, a new Aftermath dump, a stall or exit, a trace not written
  within 60 s, or settings not re-read. The faulted run's log slice is kept, and later
  jobs relaunch;
- the tree's `VkLayer_bg3rtx.dll` changes, because a layer needs a new process.

The session starts once the scene is stable: with `auto_traces=0` the layer takes no
trace and writes `auto.done` with `[auto] scene ready`. A tree whose layer predates this
runs its jobs alone, as before.

Records gain `session` (`s<hhmmss>#<run>`), `core_gen` and `saved_sec`, which is the
session's launch-to-ready time for every run but the first. `results.md` notes each
session's start and close, with the total saved.

The game is not reloaded between runs, so the state carries over: time of day, camera,
streaming and temporal history. Keys that act only at startup (`rt`, `headless`,
`fg`/DLSS setup, `aftermath`) and fault-rate tests belong in non-session jobs. A pass with
a fresh game is still the reference before a result is final.

Offline test (no game): `.\scripts\test-queue-dryrun.ps1` runs ten jobs through
`test-queue.ps1 -DryRun`. `test-queue-fake.ps1` plays the game and the layer: a fake log,
a hot reload, traces, and nv153, stall and layer-rebuild faults.

Example: three toggles on one save, about 2 minutes instead of about 4.5:

```json
{"name": "gi-off",   "save": "Campsite - 27h", "session": true, "set": "gi=0", "traces": 1, "patterns": ["\\[core\\] guard:"]}
{"name": "gi-on",    "save": "Campsite - 27h", "session": true, "set": "gi=1", "traces": 1, "patterns": ["\\[core\\] guard:"]}
{"name": "refl-off", "save": "Campsite - 27h", "session": true, "set": "rt_reflections=0", "traces": 1, "patterns": ["\\[core\\] guard:"]}
```

Rules that keep the queue fast and its results trustworthy:
- **One load, many measurements.** Put A/B toggles in a `live_phases` spec (`phases`),
  or in consecutive session jobs, not in separate fresh runs. A load costs 40-60 s, a
  toggle about 10 s.
- **Restarts only when the load is the subject.** Fault-rate tests (`repeat`) restart the
  game because the faults cluster in the first minute after a load. Don't mark them `session`.
- **Don't rebuild a `tree` while its job runs.** The core DLL hot-reloads into a running
  game, so a rebuild mid-job mixes two builds; the record flags it (`BUILD CHANGED`). Agents
  build, commit, then enqueue. Between session jobs a core rebuild is fine: the next run
  waits for the new generation.
- **Wait for results; never poll the game.** The runner writes them, and the coordinator
  reads `results.md`.

## Concurrent runs

**Status: experimental, off by default.** Every run is exclusive unless it asks
for `-Shared` (`auto-run.ps1 -Shared`, `scene-sweep.ps1 -Parallel 2`,
`python -m scripts.qa run --parallel 2`). Two instances do run side by side: RTX 5090,
32 GB, about 17-26 GB used with two headless games, no crash, no input interaction.
But scenes did not load reliably in pairs (see "Findings" below).

**Shared and exclusive runs.** A `-Shared -Background -Save` run is *shared*. Every
other run is *exclusive*, including `-RealPresent` (frame generation needs the
foreground) and `-Exclusive` (timing and performance runs). An exclusive run has
the GPU to itself. Shared runs' timings are contended:
RR cost 3.6-5.3 ms against 2.6 ms alone. They print `Run mode: shared (slot N):
timings are not exclusive`, and each capture gets a `run.json` with
`"exclusive": false`. Performance numbers come only from exclusive runs
([QUALITY.md](QUALITY.md) R7).

**Locks** (`scripts/gamelock.ps1`; named mutexes, so a crashed run releases them):

| Name | Role |
|---|---|
| `Global\BG3RTX_game_run` | The exclusive lock. Older copies of `auto-run.ps1` take only this one, and close *every* `bg3.exe` when they start, so it is held whenever any game of ours runs. |
| `Global\BG3RTX_game_slot0` / `slot1` | One per concurrent game. |
| `Global\BG3RTX_game_excl_wait` | An exclusive run waiting for its turn; shared runs start no new game meanwhile. |
| `Global\BG3RTX_game_holder`, `_admin`, event `_shared_open` | The holder hand-over (below). |
| `Global\BG3RTX_checkout_<hash>_<tag>` | One run per checkout and tag. |

An exclusive run takes `excl_wait`, then the exclusive lock, then both slots.

Two shared games cannot both own the exclusive lock, because a mutex belongs to one
thread. A small hidden process, the holder (`gamelock.ps1 -Holder`), owns it on their
behalf: a shared run takes a free slot only while the holder owns the exclusive lock.
A sweep or QA run starts its own holder with `-WhilePid <its pid>`, so the lock stays
held for the whole sweep, including the gaps between scenes. A holder started by a
lone shared run releases the lock about 6 s after the last slot became free.

**What each run touches.**
- Only its own game, by PID: `captures\game[.<tag>].pid`. At start a run closes only
  a game left behind by the same checkout and tag, never anyone else's.
- `-Tag a|b` gives a lane its own files: `captures\auto.<tag>.ini`,
  `captures\auto.<tag>.done`, `bg3rtx.<tag>.log` and `captures\<stamp>-<tag>\`. The layer
  reads `BG3RTX_RUN_TAG`.
- **Display settings.** The player's `Fullscreen` and `FakeFullscreenEnabled` are saved
  once, to `%LOCALAPPDATA%\BG3RTX\display_prev.json`, by the first background run. The
  last one to finish restores them (reference count in `display_users\`). A run that
  finds no live user and a leftover `display_prev.json` restores it first (crash
  recovery). Every background run re-applies borderless before its launch, because the
  game sometimes rewrites `graphicSettings.lsx` without these two entries. An entry
  that was absent is saved as `null` and removed again on restore
  (`ensure-settings.py`), so the file comes back byte-identical. Older copies of the
  script left the borderless values in that case.
- **Saves.** Only the save folders the layer saw its own game create
  (`BG3RTX_SAVE_LOG`) are parked.
- **Second instance profile.** A game started while another BG3 runs becomes a local
  "debug client": it uses `PlayerProfiles\Debug_Client_Profile_1`, which has no saves,
  so `-loadSaveGame` leaves it in the main menu. Shared runs therefore copy the save
  folder there first; the player's folder is only read.
- **Script Extender bridge.** Each shared game gets `bg3rtxse=slot<N>` and uses
  `Script Extender\bg3rtx\slot<N>\` ([SE_BRIDGE.md](SE_BRIDGE.md)).

**Findings** (2026-10-27, measured as game-lock time with the lock waits behind other
agents excluded; 4 scenes S2, S3, S5, S8; 240 s timeout):

| | Shared (`-Parallel 2`) | Exclusive (default) |
|---|---|---|
| Wall clock, first launch to last exit | 378 s | 486 s (sum of the 4 runs' lock time) |
| Scenes captured | 2 of 4 (S3 125 s, S8 67 s) | 3 of 4 (S3 67 s, S5 81 s, S8 89 s) |
| Failures | S2 stayed in the main menu; S5 (the second instance) stopped presenting while loading | S2 stayed in the main menu (so it is not caused by concurrency) |

Per captured scene, the shared sweep was slower (189 s against 80 s), so there is no
speed-up yet. That is why shared runs stay opt-in.
- S2 (`Rivington - 74h`) does not load through `-loadSaveGame` even alone. The game
  shows the 3D main menu for the whole run. Main's sweep of 2026-10-26 did not include
  it either.
- A second instance first stayed in the main menu, because of its empty
  `Debug_Client_Profile_1`. With the save copied there, it loaded S3 and S8, but S5
  hung during its loading screen.
- The two runs were also slower than one run each (GPU at 98%, RR 3.6-5.3 ms).

Worth revisiting only if loading in the debug-client profile proves reliable. That
needs a repeat of `-Parallel 2` on several scenes, run while no old-style
`auto-run.ps1` copies are active.