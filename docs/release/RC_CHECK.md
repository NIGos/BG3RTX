# Release candidate check

Date: 2026-11-04 19:12:25 +01:00. Commit: ee448f7fcc6aee1b6f4c56007648cf92ee0e81a5.
Source state at start: clean tracked files (untracked files excluded).
Result: PASS. Candidate version: rc-20261104-ee448f7f-a04c5ed9.

Rerun: `powershell -NoProfile -ExecutionPolicy Bypass -File scripts/release/verify_rc.ps1`.

| Item | Result | Evidence |
|---|---|---|
| Build | PASS | Core, launcher and lean layer built from this tree; diagnostics OFF. No shader compile or game run. |
| Archive | PASS | Real ZIP, 72.61 MiB; SHA256 644372945dc38c6fd2b634981c0a7f8b15bbba0889cdeb3fa127b264d5ff1e18; symbols kept in separate archive. |
| Manifest | PASS | 229 files; expected 229 (delta 0); 0 differences across ZIP / extracted / files.txt / source plan. |
| Hashes | PASS | 12 shipped DLLs and launcher match build/copy sources; 226 verbatim files checked; 9 Streamline DLLs match production SDK. |
| Exclusions | PASS | 0 symbols, developer tools, SDK payloads, Aftermath DLLs, Python or unexpected binaries. Private symbols outside player ZIP. |
| Licences | PASS | 15 required notices plus project licences and font OFL present, verbatim hashes checked; README licence lines 3-4 PASS. |
| Version | PASS | rc-20261104-ee448f7f-a04c5ed9; commit matches HEAD; layer lean. |
| Install | PASS | 229 real payload files hash-match after install; fake game unchanged; 3 original collisions backed up. |
| Update | PASS | 229 files hash-match after same-candidate update; corrupted installed DLL repaired; original backups, settings and unrelated file preserved. |
| Uninstall | PASS | 5/5 original files restored byte-for-byte; fake game unchanged; no owned payload/metadata remains. |
| Setup | PASS | Inno 6.7.3 compiled; 229 ZIP payload hashes match after install/update; settings retained by default and removed on opt-in; shader cache removed; 0 owned files or HKCU Apps entries remain. Shortcuts disabled in qualification; no launcher execution. |
| Cleanup | PASS | Owned TEMP test tree deleted; no existing installation, cache, shortcuts or game files changed. Setup Apps cleanup is checked in the Setup row. |

Archive: C:\Users\quali\Documents\Code Projects\BG3RTX\.claude\worktrees\inno-setup\.scratch\rc\20261104-185937-a04c5ed9\BG3RTX-rc-20261104-ee448f7f-a04c5ed9.zip

Scope: real ZIP legacy install.ps1/uninstall.ps1 route, no shortcut, settings retained. Update reapplies this candidate, not a different release.
Only synthetic installer child shells mock named-process discovery, as release_install_test.py does; build process guards remain real. No game or payload binary executes.
Setup row covers compiled Inno setup/update/uninstall in TEMP, with shortcuts disabled and isolated LOCALAPPDATA; only PASS qualifies that route. No fresh-PC, signing, shortcut UI, locked-file, interruption, game/visual acceptance or release/upload approval is claimed.
Git metadata is read-only. Generated archives, symbols and logs remain under .scratch/rc and must not be added to Git.
