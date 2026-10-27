# Project status (stable snapshot 2026-10-27, about 19:45)

## Stable build: how to play and compare
- **Main is stable.** The intermittent device lost was D-LS reading a destroyed SSS-profile
  buffer of the game (docs/DEVICE_LOST_DLS.md). After the fix (`query_binding`): 0 faults in
  more than 60 runs, including 8/8 in the stability check of 19:17-19:27 (Campsite, Grymforge),
  with `rt_bvh_split=1`.
- **Start:** `bin\BG3RTX.exe`, then a preset, then "Save and launch"; or the launcher as the
  Steam launch option (docs/USER_SETTINGS.md).
- **A/B:** **Ctrl+Shift+F9** while playing switches every ray-traced effect off and on (the
  game's own lighting in the same frame; DLSS and frame generation unchanged). Logged as `[ab]`.
  Not yet tried by hand: background runs can't press real keys.
- **On by default:** RT sun shadows (with the roof-based replace), RT local shadows (all lights
  except darkvision; traced visibility replaces the game's shadow maps on screen), GI (diffuse
  bounce), RT reflections, DLSS RR, BVH split.
- **Off by default until their gates pass:** SHaRC (`sharc`), exact-k light sampling (`ls_k`).
  On branches, not in main: D-EM emissive capture (one material open in AF), EN2 hit materials
  (in test), the runtime-heuristic fixes and exposure hold (in test).
- **Known QA failures that are not defects:** flicker in wind-blown grass and foliage (the RT sun
  and GI sharpen per-blade lighting the wind moves, R0), contact occlusion under platform rims.
- **Modded saves** (33 of 110: Act 2, Rivington) don't load in background runs: the game drops
  `-loadSaveGame` when mods are missing, and posted clicks don't press its menu buttons.
  Set aside by the user (2026-10-27): not tested for now (docs/TEST_SCENES.md, "Saves made with mods").

## Test process
`scripts/test-queue.ps1` (docs/TEST_SCENES.md, "Test queue"): one runner launches the game,
serially, hidden and click-through; agents enqueue JSON jobs; shared sessions per save; results in
`captures/queue/results.md`, per-run logs in `captures/queue/logs/`. Thresholds: every gate
derived (QUALITY R0 addendum, docs/QA.md). Quality before frame rate on Ultra (QUALITY.md).

## Earlier snapshot (2026-10-26)

## In main
- **RR:**
  - RR guides v2;
  - RR before post (the game's grade is replayed after RR);
  - RR-active flag (ABI 7);
  - CBT and SSS guides;
  - Preset F pinned.
- **Ray-traced effects:**
  - sun shadows (soft, with the sun radius from the game);
  - GI (RR mode);
  - local-light shadows with per-light data from the asset DB and the Script Extender bridge;
  - reflections (D-RF).
- **Scene build:** the static/dynamic BVH split.
- **Audit fixes, batch 1:** back-lit translucency, material classes, AO on direct light, alpha buffer sizing, ray origin.
- **Engine data:**
  - the offline asset DB (`docs/ASSET_DB.md`; build it with `python -m tools.assets build --out data/assetdb`);
  - the Script Extender bridge, active only while the layer runs (`docs/SE_BRIDGE.md`).
- **Compatibility and settings:**
  - the Script Extender coexistence guard (`se_unhook=1`);
  - settings phase 1 (`common/settings.h`, presets, the `bin/BG3RTX.exe` launcher).
- **Test tooling:**
  - test scenes and `-Save` (`docs/TEST_SCENES.md`);
  - background mode (`-Background`);
  - the frame pacing log (`[pace]`);
  - the QA harness (`python -m scripts.qa`, `docs/QA.md`).
- **Principles:** `docs/QUALITY.md` R0 (physically correct, not "same as the original").

## Paused work (one branch each, with `PAUSED.md` at its worktree root)

| Worktree / branch | Task | Notes |
|---|---|---|
| `agent-ac91da91fddcd3510` | Foliage alpha test in RT rays | Grass cards act as opaque: black GI patches, square sun shadows, reflections. Evidence: `docs/img/bug_campsite_gi_alpha_ab_*.png`. Verify with `python -m scripts.qa run --scenes S3,S1`. |
| `agent-a88047a9678afc2e5` | Local-light shadow cost back to 0.35 ms or less | Now about 0.6 ms. |
| `agent-a369c987e3572f784` | Image lifetime at level load | `VUID-vkDestroyImage-image-01000` since RR before post. |
| `agent-a57aa2a0cc31585bb` | Two concurrent game instances for tests | Feasibility test ran with two live instances. |

State of each branch at the pause, as reported:
- **Foliage alpha** (`7a6a90a`).
  - Root cause: 24 grass shader families were missing from `alpha_fs.txt` (the table generator missed the temporary-variable form), and 76 shaders used the wrong cut-off.
  - Effect on Campsite QA: GI hard-edged darkening 10.9‰ → 0, flicker 76 → 16.
  - To do: a `-Move` run, a check of the other alpha content (Goblin Camp foliage, foundry chains), and 20 shaders that still use the loose cut-off.
- **Local-light shadow cost** (`fa7f537`).
  - −31 to −37% (foundry 0.40 ms), bit-exact (`ls_check=1`).
  - To do: validation, `-Move`, the docs (draft in `tools/lsref/`).
  - Grymforge is still 0.72 ms because of its many torches.
- **Image lifetime** (done).
  - The validation ID is gone in 3 scenes: the descriptor ring held stale bindings.
  - The destroy hooks now wait on our fence. No waits at level loads; 3 waits of 0.1 to 6.8 ms in 5 `-Move` runs.
  - 0 non-finite values in the traced motion frame.
  - Open for the alpha owner: QUALITY.md, "the alpha set's game texture views, one frame late".
- **Two instances** (`91b50c3`).
  - Feasible: two instances ran for 150 s, using 26 GB of VRAM.
  - The slot locks, the PID-only control, the display-settings restore counted across runs and the per-instance Script Extender bridge are written.
  - To do: the foundry capture timeout when shared, the second instance writing no bridge data, the 4-scene speed-up measurement, and the docs.
  - Before merging, confirm old-style scripts can no longer kill shared games.

**To resume:** read each `PAUSED.md`, finish and verify, merge one branch at a time into main (`git merge --no-ff -X renormalize -X ignore-space-at-eol`), rebuild, run QA, then remove the worktree.

## Next after the paused work
- **Bugfix pause:**
  - QA over all scenes (`scripts/scene-sweep.ps1`, `scripts/qa`);
  - which effect causes the flicker (2.5× in the foundry);
  - the act-2, Rivington and Crèche saves don't load with `-loadSaveGame`. Cause: the
    missing-mods dialog (texture-only mod; Crèche: its non-ASCII name). Fix:
    `auto-run.ps1 -AcceptModWarning` (default auto), verification jobs pending
    (TEST_SCENES.md, "Saves made with mods").
- **Rendering coverage matrix and ranked gaps:** [COVERAGE.md](COVERAGE.md) (2026-10-27).
  Top gaps: forward water (89% of the S1 docks view), box lights (36.6% of placed lights,
  93% of the shadow-mapped ones, not traced), emission not bouncing in GI. Refresh with
  `python -m scripts.qa.coverage --update-doc`.
- **R0 follow-ups:**
  - the sun RT shadow replaces the game's mask where the BVH is complete, with the asset DB roofs for the cut-away roofs;
  - local-light shadows replace the game's shadow-mapped lights.
- **State-of-the-art wave:**
  - hit materials (shader → material table);
  - emissive geometry capture;
  - opacity micromaps for foliage;
  - a SHaRC radiance cache;
  - the game's sky and cloud shadows for misses;
  - a ray-count sweep for Ultra.
- **Foreground-only checks** (when the PC is free): the frame-gen real present with all merges, and the Ultra pacing pan.

## State-of-the-art backlog (ordered; from AUDIT_SOTA, AUDIT_ENGINE_DATA rounds 1-2, SETTINGS_DESIGN)

Each item goes through the usual loop: QUALITY R0-R6 gates, QA harness A/B, and
exclusive runs for timings.

**Implementation-ready plans** for many-light sampling (D-LS exact-k + RIS remainder,
a GPU light BVH, ReSTIR DI on evidence), a screen-injected SHaRC cache, and opacity
micromaps are in [DESIGN_SOTA_NEXT.md](DESIGN_SOTA_NEXT.md) (2026-10-27).
- Recommended order: 1a → 2 v1 → 3 → 1c → 2 v2 → 1b.
- Downloads needed: SHaRC, the OMM SDK, and RTXDI (optional).
- Done there: the CPU light-tree oracle (`common/light_tree.h`, `light_tree_test`).

| # | Upgrade | Unlocks | Status |
|---|---|---|---|
| 1 | RR before post (grade replayed after RR), CBT/SSS guides, Preset F | unbiased denoising | **done** |
| 2 | Static/dynamic BVH split | −2/−3 ms in motion | **done** (todo: skinning output, wind foliage, class-A UBO variants) |
| 3 | Emissive geometry capture, then emissive in GI/reflections + an emissive light list (RIS/ReSTIR DI for emissives) | lava/magic/candle bounce, glowing reflections | data side in progress |
| 4 | Hit materials: shader → material table plus VT page table, albedo/emission at secondary hits | correct bounce colour, off-screen reflections | next |
| 5 | SHaRC world-space radiance cache, fed on screen | GI from 1 m to room scale, multi-bounce, reflection misses | after 3-4 |
| 6 | Opacity micromaps for alpha-tested meshes (needs 2) | faster foliage traversal (−0.3 to −1 ms) | after the alpha fix |
| 7 | The game's sky/atmosphere LUTs and cloud shadows for ray misses | correct miss radiance, cloud-shadowed RT sun | partly in the sun R0 task |
| 8 | Shadow-map atlas and cascades as fallback occluders for off-screen casters | no leaks from culled geometry | after the sun R0 task |
| 9 | Offline static world (level placements → GR2 meshes, HLOD proxies, roof data) | closed interiors, off-screen casters and reflections behind the camera | large; after 4-5 |
| 10 | Local-light shadows replace the game's shadow-mapped lights (R0) | physically correct local shadows | after the D-LS performance task |
| 11 | Ray-count sweep (QA/FLIP) for Ultra; ReSTIR GI if 5 isn't enough | evidence-based quality per ms | QA harness ready |
| 12 | Forward pass capture: water and glass in the BVH; refraction (D-RFR) | reflections/refraction on water | later |
| 13 | Reflex markers from `SDL_PollEvent`, `frameLimitUs` cap with DLSS-G | latency, pacing at 120 Hz | after the two-instance work |
| 14 | In-game settings overlay (SETTINGS_DESIGN phase 2) | per-user tuning | after the presets are validated |
| 15 | NRD fallback for non-RR GPUs; presets validated on 30/40-series | distribution | later |

## Environment notes
- **Script Extender:** v33 is installed (`bin\DWrite.dll`); remove that file to uninstall, and a copy is in `D:\BG3_Tools`. The bridge mod is in `Data\Mods\GustavX\ScriptExtender` (`scripts/install-se-bridge.ps1 -Uninstall`).
- **Save backup:** `D:\BG3_Save_Backup\20261026_PlayerProfiles`.
- **The player's settings:** 3840x1600 exclusive fullscreen, HDR; the desktop is 4K at 120 Hz.
