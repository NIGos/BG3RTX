# Release candidate check

Date: 2026-11-04 22:24:23 +01:00. Commit: ab66c8553d834f4300db8f8e90a73a3b0d4d42e6.
Source state at start: clean tracked files (untracked files excluded).
Result: PASS. Candidate version: rc-20261104-ab66c855-d98a6f97.

Rerun: `powershell -NoProfile -ExecutionPolicy Bypass -File scripts/release/verify_rc.ps1`.

| Item | Result | Evidence |
|---|---|---|
| Build | PASS | Core, launcher and lean layer built from this tree; diagnostics OFF. No shader compile or game run. |
| Archive | PASS | Real ZIP, 72.47 MiB; SHA256 114ebd32d222fc9ec93c9de253e3beff73d537ca22b8004adda075a93c7f8a2d; symbols kept in separate archive. |
| Manifest | PASS | 219 files; expected 219 (delta 0); 0 differences across ZIP / extracted / files.txt / source plan. |
| Hashes | PASS | 12 shipped DLLs and launcher match build/copy sources; 216 verbatim files checked; 9 Streamline DLLs match production SDK. |
| Exclusions | PASS | 0 symbols, developer tools, SDK payloads, Aftermath DLLs, Python or unexpected binaries. Private symbols outside player ZIP. |
| Licences | PASS | 15 required notices plus project licences and font OFL present, verbatim hashes checked; README licence lines 3-4 PASS. |
| Version | PASS | rc-20261104-ab66c855-d98a6f97; commit matches HEAD; layer lean. |
| Install | PASS | 219 real payload files hash-match after install; fake game unchanged; 3 original collisions backed up. |
| Update | PASS | 219 files hash-match after same-candidate update; corrupted installed DLL repaired; original backups, settings and unrelated file preserved. |
| Uninstall | PASS | 5/5 original files restored byte-for-byte; fake game unchanged; no owned payload/metadata remains. |
| Setup | PASS | Inno 6.7.3 compiled; 219 ZIP payload hashes match after install/update; settings retained by default and removed on opt-in; shader cache removed; 0 owned files or HKCU Apps entries remain. Shortcuts disabled in qualification; no launcher execution. |
| Cleanup | PASS | Owned TEMP test tree deleted; no existing installation, cache, shortcuts or game files changed. Setup Apps cleanup is checked in the Setup row. |

Archive: C:\Users\quali\Documents\Code Projects\BG3RTX\.scratch\rc\20261104-221407-d98a6f97\BG3RTX-rc-20261104-ab66c855-d98a6f97.zip

Scope: real ZIP legacy install.ps1/uninstall.ps1 route, no shortcut, settings retained. Update reapplies this candidate, not a different release.
Only synthetic installer child shells mock named-process discovery, as release_install_test.py does; build process guards remain real. No game or payload binary executes.
Setup row covers compiled Inno setup/update/uninstall in TEMP, with shortcuts disabled and isolated LOCALAPPDATA; only PASS qualifies that route. No fresh-PC, signing, shortcut UI, locked-file, interruption, game/visual acceptance or release/upload approval is claimed.
Git metadata is read-only. Generated archives, symbols and logs remain under .scratch/rc and must not be added to Git.
