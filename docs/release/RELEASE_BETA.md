# Nexus beta checklist

Developer checklist. Player instructions: [README.md](README.md).
This is not release approval. Packaging remains paused until the open decisions and gates are resolved.
Offline status audit: [RELEASE_OFFLINE_STATUS.md](RELEASE_OFFLINE_STATUS.md).

## Defaults and memory

- [ ] Reconcile the final merged settings and spec-constant migration before packaging.
  Read `docs/release/RELEASE_DEFAULTS_REVIEW.md` on `codex-release-defaults` if it is not merged.
- [ ] Keep diagnostics, crash dumps and unqualified features off. Keep developer controls hidden.
  Do not promote native hair, OMM, generated rows or GI guards from partial evidence.
- [ ] Remove the five rejected or null water experiments. Keep the qualified RT water chain.
- [ ] Resolve the redundant `ls_box` and `rt_emissive` release pins with the defaults owner.
  Ultra's emission control must enable its prerequisites together.
- [ ] Verify README preset descriptions against the final package. Avoid GPU model guarantees.
- [ ] Qualify VRAM across scene transitions and long sessions. The launcher's suggestion uses GPU
  model, not available memory. The reported 32 GB port case reached 31.7 GB against a 30.6 GB budget.
  No capacity or preset has a complete memory qualification receipt.
- [ ] Test 12 GB and 16 GB cards starting with Low. Test RTX 30 and RTX 40 hardware.
  Do not advertise Ultra as safe on 12-24 GB cards or guaranteed on 32 GB cards.

## Installer review and offline check

The launcher enables a Vulkan layer only for the game process. Installation is a per-user copy,
not a game-folder mod. No registry entry or administrator rights are needed.

- [x] Review `scripts/release/install.ps1` and `uninstall.ps1` for game/profile protection.
  Both accept optional `-GameDir <root containing bin\bg3.exe>` as a safety check.
  The launcher discovers Steam installs; Browse selects other installs. The scripts do not search saves.
- [x] Add `tests/release_install_test.py` to the single CMake Python test list.
  Run it directly with `python tests/release_install_test.py`; no build or CMake is needed.
  It uses fake package and game folders. Process discovery is mocked only in its child shell,
  so an open user game does not block disposable filesystem tests.
- [x] Direct test: 7 tests passed in 17.702 s. Details: `docs/progress/codex-release-docs.md`.
- [ ] Review package completeness after the diagnostics branch lands. Include Diagnostics.cmd
  and diagnostics.ps1 in files.txt. A README reference is not proof that they ship.
- [ ] Fresh-PC acceptance, separately authorized: unzip, install without elevation, start through
  the launcher and Steam, update, uninstall, and confirm original files and unrelated files survive.
  Test a PC without VULKAN_SDK. Runtime DXC must come from bin.
- [ ] Test shortcut creation and removal from a standard Windows account. The temporary test
  uses NoShortcut and does not change the user's Start menu or real shader cache.
- [ ] Test interruption recovery and locked-file errors on disposable installs.

Updates keep bg3rtx_user.ini. Original file collisions are copied once to `.bg3rtx_backup`.
Later updates keep those originals. Retired package files stay in the ownership manifest so
uninstall can restore their originals too. A missing or unsafe manifest fails before deletion.
Do not delete the backup folder before uninstall. Portable use does not need installation.
`-WhatIf` is available on both scripts. It validates paths and lists the intended action without writes.

## Live quality and compatibility (not run by this review)

- [ ] Run smoke_ultra with every qualified release key enabled. Read every-window gates.
- [ ] Run 2-4 hour sessions with level transitions. Check VRAM growth, scene capacity,
  log rotation, shader-cache reuse and frame generation stability.
- [ ] Cover Act 2 and more Act 3 locations. Record save, preset, driver and game version.
- [ ] Confirm the first-enable native hair compile behaviour in the final merged implementation.
  The README currently warns of shader compilation and a pause or stutter.
- [ ] Keep the port water limitation: the observed flicker is present in the game's own water.
- [ ] Confirm the tested driver and game versions in README against the final release logs.
  The recorded driver 621.01 is not a measured minimum. Do not infer support for older drivers.
- [ ] Check overlays, GOG Browse launch, HDR and other graphics mods before claiming compatibility.

Live runs require fresh explicit user steering. This checklist does not authorize them.

## Diagnostics and privacy

Implementation reference: `git show codex-diag-package:scripts/release/Diagnostics.cmd`
and the companion diagnostics.ps1 on that branch. Do not describe a planned launcher report button
or automatic upload as shipped behaviour.

- [ ] Merge and independently verify the diagnostics implementation and its tests.
- [ ] Confirm Diagnostics.cmd creates a local ZIP, prints its path and contents, and uploads nothing.
- [ ] Confirm the report contains logs, settings, VERSION.txt, effective config, GPU/OS information
  and existing crash dumps. It must not enable crash dumps or change settings.
- [ ] Confirm saves are absent by default. IncludeSave must require explicit selection and
  confirmation, read only the selected .lsv, and leave the original unchanged.
- [ ] Verify text redaction and CONTENTS.txt. Warn that binary saves and dumps are not redacted.
- [ ] Verify the manual-report fallback in README when Diagnostics.cmd is unavailable.

## Licences and package sign-off

Inventory and unresolved terms: [THIRD_PARTY.md](THIRD_PARTY.md).
This checklist does not resolve redistribution permission.

- [ ] Choose the project's licence and resolve the NVIDIA notification decision.
- [ ] Verify the required NVIDIA, Streamline, DXC and Vulkan licence texts are present in `licences/` (SHaRC opt-in uses `LICENSES/`).
- [ ] Resolve asset DB redistribution under Larian's terms.
- [ ] Keep SHaRC source headers out unless redistribution is resolved. Keep sharc off.
  Keep Aftermath, OMM SDK, development Streamline DLLs and developer tools out of the player archive.
- [ ] After approval to resume packaging, use the existing package script and inspect files.txt.
  Keep the symbols ZIP private. No package command was run for this review.
- [ ] Upload only after review of the final archive, README, known issues and release evidence.
