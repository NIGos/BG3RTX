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
