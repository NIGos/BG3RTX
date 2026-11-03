## Beta docs and installer review (branch codex-release-docs, offline verified, 2026-11-02)

- README explains per-user installation, presets, uninstall, native hair's first-enable
  shader compile, native port water flicker and Diagnostics.cmd reports with optional saves.
- Replaced GPU model promises with capacity guidance from codex-release-defaults' review.
  Ultra is not memory-qualified, including 32 GB: reported port peak 31.7 GB / 30.6 GB budget.
- Scripts now preflight manifests, reject traversal/streams/wildcards and linked ancestors,
  use literal paths, and protect Vulkan/DX11 game folders and Larian profile folders.
- Added optional GameDir safety checks and install WhatIf. No elevation or game writes.
- Back up original file collisions once. Keep them across updates and retired files;
  restore on uninstall. Preserve user settings on reinstall and with uninstall KeepSettings.
- Keep recovery backups until uninstall operations succeed. Refuse missing ownership metadata,
  overlapping source/destination trees and adoption of existing runtime folders.
- Rewrote RELEASE_BETA as an evidence checklist. Diagnostics implementation is on
  codex-diag-package; it still needs merging and inclusion in the package file list.
- Direct verification: python tests/release_install_test.py, 7 tests passed in 17.702 s.
  Covers update/reinstall/restore, settings, dry runs, runtime cleanup, missing update payload,
  malformed manifests, protected game/profile paths and NTFS junction refusal.
- Tests run copied scripts with fake game/package folders and inert binaries in temp storage.
  Process discovery is mocked only in test child shells because a user game/settings process
  is open. NoShortcut and KeepShaderCache avoid the real Start menu and cache.
- git diff --check passed. No build, CMake, CTest, DXC, package or game command ran.
  Git metadata was not intentionally changed; no commit, merge or push. Shader request delta: 0.
- Still open: fresh-PC/standard-account shortcuts, interruption/locked-file acceptance,
  final driver/game-version confirmation, long-session/VRAM and hardware qualification,
  diagnostics integration, licences and package sign-off. No live approval requested.
