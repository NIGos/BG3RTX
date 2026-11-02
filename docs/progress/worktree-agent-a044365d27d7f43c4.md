## Stage 6 first pass: release package, installer, README, licences (branch worktree-agent-a044365d27d7f43c4, offline, 2026-11-02)
- Release defaults audit of all 565 `cfg_*` call sites against the package profile: no new diagnostic leak on.
  Pinned `rt_hit_reference=0` in data/release.ini (developer debug reference; already 0, nothing visible changes).
  Kept on by design: ab_hotkey, mesh audits, emissive calibration, dev_fault, watchdog log, sl_logs, core hot reload.
  Doubtful items (ls_box/rt_emissive in release.ini, hidden developer knobs, no log cap) listed in docs/release/RELEASE_BETA.md.
- scripts/package.ps1 fixed and extended: it shipped no common/ headers (21 shader files include them, so the
  RT path could not compile in a player package) and no asset DB. Now: explicit file list, shader includes
  followed, assetdb, licence texts, versioned dist\BG3RTX-<ver>.zip + -symbols.zip (PDBs), files.txt, VERSION.txt.
- Install model found in tools/launcher/launch.cpp: no implicit layer or registry (VK_ADD_LAYER_PATH per process).
  scripts/release/install.ps1 / uninstall.ps1 (+ .cmd): copy to %LOCALAPPDATA%\Programs\BG3RTX, Start menu
  shortcut; uninstall removes listed + runtime-written files and the shader cache, never follows links, refuses
  Larian Studios and game folders.
- docs/release/README.md (players), THIRD_PARTY.md (shipped notice), RELEASE_BETA.md (audit, licence flags,
  what remains, diagnostic-package design).
- Licence flags (user decisions): SHaRC headers are source under the RTX licence (object code only): not shipped,
  sharc=0 everywhere; DXC and Vulkan-Headers licence texts not on disk (package warns); asset DB is Larian-derived
  data; the repository has no licence of its own; NVIDIA notification before a commercial release.
- Open: long sessions, Act 2 / most of Act 3, non-5090 GPUs, fresh-PC install test, USER_SETTINGS.md Experimental text.
