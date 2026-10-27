# Project status (paused 2026-10-26, about 21:00)

Main is buildable and verified up to `1e87553`.

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
  - the act-2, Rivington and Crèche saves don't load with `-loadSaveGame` (likely a dialog).
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

## Environment notes
- **Script Extender:** v33 is installed (`bin\DWrite.dll`); remove that file to uninstall, and a copy is in `D:\BG3_Tools`. The bridge mod is in `Data\Mods\GustavX\ScriptExtender` (`scripts/install-se-bridge.ps1 -Uninstall`).
- **Save backup:** `D:\BG3_Save_Backup\20261026_PlayerProfiles`.
- **The player's settings:** 3840x1600 exclusive fullscreen, HDR; the desktop is 4K at 120 Hz.
