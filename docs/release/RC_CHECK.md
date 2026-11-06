# Release candidate check

Date: 2026-11-06 17:35:03 +01:00. Commit: e1ce73291f7acda13a18c3fea9ea2e0d19c86aa9.
Source state at start: clean tracked files (untracked files excluded).
Result: PASS. Candidate version: rc-20261106-e1ce7329-291da9f8.

Rerun: `powershell -NoProfile -ExecutionPolicy Bypass -File scripts/release/verify_rc.ps1`.

| Item | Result | Evidence |
|---|---|---|
| Build | PASS | Core, launcher and lean layer built from this tree; diagnostics OFF. No shader compile or game run. |
| Archive | PASS | Real ZIP, 72.65 MiB; SHA256 fd1d37298df5599c4c4e700ec7b1e914cc887cb2ce4a11c23aad06904b8c545f; symbols kept in separate archive. |
| Manifest | PASS | 223 files; expected 223 (delta 0); 0 differences across ZIP / extracted / files.txt / source plan. Runtime source dependencies covered. |
| Hashes | PASS | 12 shipped DLLs and launcher match build/copy sources; 220 verbatim files checked; 9 Streamline DLLs match production SDK. |
| Exclusions | PASS | 0 symbols, developer tools, SDK payloads, Aftermath DLLs, Python or unexpected binaries. Private symbols outside player ZIP. |
| Licences | PASS | 15 required notices plus project licences and font OFL present, verbatim hashes checked; README licence lines 3-4 PASS. |
| Version | PASS | rc-20261106-e1ce7329-291da9f8; commit matches HEAD; layer lean. |
| Install | PASS | 223 real payload files hash-match after install; fake game unchanged; 3 original collisions backed up. |
| Update | PASS | 223 files hash-match after same-candidate update; corrupted installed DLL repaired; original backups, settings and unrelated file preserved. |
| Uninstall | PASS | 5/5 original files restored byte-for-byte; fake game unchanged; no owned payload/metadata remains. |
| Setup | PASS | Inno 6.7.3 compiled; 223 ZIP payload hashes match after install/update; settings retained by default and removed on opt-in; shader cache removed; 0 owned files or HKCU Apps entries remain. Shortcuts disabled in qualification; no launcher execution. |
| Cleanup | PASS | Owned TEMP test tree deleted; no existing installation, cache, shortcuts or game files changed. Setup Apps cleanup is checked in the Setup row. |

Archive: C:\Users\quali\Documents\Code Projects\BG3RTX\.scratch\rc\20261106-173120-291da9f8\BG3RTX-rc-20261106-e1ce7329-291da9f8.zip

Scope: real ZIP legacy install.ps1/uninstall.ps1 route, no shortcut, settings retained. Update reapplies this candidate, not a different release.
Only synthetic installer child shells mock named-process discovery, as release_install_test.py does; build process guards remain real. No game or payload binary executes.
Setup row covers compiled Inno setup/update/uninstall in TEMP, with shortcuts disabled and isolated LOCALAPPDATA; only PASS qualifies that route. No fresh-PC, signing, shortcut UI, locked-file, interruption, game/visual acceptance or release/upload approval is claimed.
Git metadata is read-only. Generated archives, symbols and logs remain under .scratch/rc and must not be added to Git.
