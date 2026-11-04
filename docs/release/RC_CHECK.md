# Release candidate check

Date: 2026-11-04 17:17:08 +01:00. Commit: a48aaba0ef8c965d353800ad34806d65942beecc.
Source state at start: clean tracked files (untracked files excluded).
Result: PASS. Candidate version: rc-20261104-a48aaba0-d438c16a.

Rerun: `powershell -NoProfile -ExecutionPolicy Bypass -File scripts/release/verify_rc.ps1`.

| Item | Result | Evidence |
|---|---|---|
| Build | PASS | Existing outputs reused (-NoBuild); compilation/freshness not checked by this invocation. Output files/cache paths checked; diagnostics OFF. |
| Archive | PASS | Real ZIP, 72.62 MiB; SHA256 fb39d7e507290c95811cae45d3c60acc7a1d2a1643d8b51afa36ba35bab9a39b; symbols kept in separate archive. |
| Manifest | PASS | 229 files; expected 229 (delta 0); 0 differences across ZIP / extracted / files.txt / source plan. |
| Hashes | PASS | 12 shipped DLLs and launcher match build/copy sources; 226 verbatim files checked; 9 Streamline DLLs match production SDK. |
| Exclusions | PASS | 0 symbols, developer tools, SDK payloads, Aftermath DLLs, Python or unexpected binaries. Private symbols outside player ZIP. |
| Licences | PASS | 15 required notices plus project licences and font OFL present, verbatim hashes checked; README licence lines 3-4 PASS. |
| Version | PASS | rc-20261104-a48aaba0-d438c16a; commit matches HEAD; layer lean. |
| Install | PASS | 229 real payload files hash-match after install; fake game unchanged; 3 original collisions backed up. |
| Update | PASS | 229 files hash-match after same-candidate update; corrupted installed DLL repaired; original backups, settings and unrelated file preserved. |
| Uninstall | PASS | 5/5 original files restored byte-for-byte; fake game unchanged; no owned payload/metadata remains. |
| Cleanup | PASS | Owned TEMP test tree deleted; no global installation, cache, shortcuts or game files changed. |

Archive: C:\Users\quali\Documents\Code Projects\BG3RTX\.claude\worktrees\agent-a0c9be1ab336eae1a\.scratch\rc\20261104-171518-d438c16a\BG3RTX-rc-20261104-a48aaba0-d438c16a.zip

Scope: real ZIP legacy install.ps1/uninstall.ps1 route, no shortcut, settings retained. Update reapplies this candidate, not a different release.
Only synthetic installer child shells mock named-process discovery, as release_install_test.py does; build process guards remain real. No game or payload binary executes.
Separate Inno Setup is not packaged or compiled by this check. No fresh-PC, signing, locked-file, interruption, game/visual acceptance or release/upload approval is claimed.
Git metadata is read-only. Generated archives, symbols and logs remain under .scratch/rc and must not be added to Git.
This invocation used -NoBuild: only existing output presence/cache paths and lean configuration were checked. Compilation and source freshness require separate build evidence; rerun the default command after the last promotions.
Native build evidence: core, launcher and lean layer builds succeeded before reuse, with unchanged tracked sources. Logs: `.scratch/rc/20261104-170828-fe7af03d/build_core.stdout.log`, `build_launcher.stdout.log` and `build_lean_layer.stdout.log` in the same folder. The lean layer rebuilt at 17:07:47; its incremental recheck passed at 17:08:36. The final invocation used `-NoBuild` because the game opened independently.

Independent fixtures: `python tests/package_licences_test.py` 8/8 PASS; `python tests/release_install_test.py` 7/7 PASS. No files were staged and no commit was created.
