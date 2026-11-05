# Release candidate check

Date: 2026-11-05 16:04:43 +01:00. Commit: f004d929e1ffa6f9dd34b944968fa8e2e7ea9ec2.
Source state at start: clean tracked files (untracked files excluded).
Result: PASS. Candidate version: rc-20261105-f004d929-29c248b9.

Rerun: `powershell -NoProfile -ExecutionPolicy Bypass -File scripts/release/verify_rc.ps1`.

| Item | Result | Evidence |
|---|---|---|
| Build | PASS | Core, launcher and lean layer built from this tree; diagnostics OFF. No shader compile or game run. |
| Archive | PASS | Real ZIP, 72.59 MiB; SHA256 391b69ebbbf788da0a88b8eb580cf0f6214a68eeeb52b90c6b054a1d5adba17e; symbols kept in separate archive. |
| Manifest | PASS | 223 files; expected 223 (delta 0); 0 differences across ZIP / extracted / files.txt / source plan. Runtime source dependencies covered. |
| Hashes | PASS | 12 shipped DLLs and launcher match build/copy sources; 220 verbatim files checked; 9 Streamline DLLs match production SDK. |
| Exclusions | PASS | 0 symbols, developer tools, SDK payloads, Aftermath DLLs, Python or unexpected binaries. Private symbols outside player ZIP. |
| Licences | PASS | 15 required notices plus project licences and font OFL present, verbatim hashes checked; README licence lines 3-4 PASS. |
| Version | PASS | rc-20261105-f004d929-29c248b9; commit matches HEAD; layer lean. |
| Install | PASS | 223 real payload files hash-match after install; fake game unchanged; 3 original collisions backed up. |
| Update | PASS | 223 files hash-match after same-candidate update; corrupted installed DLL repaired; original backups, settings and unrelated file preserved. |
| Uninstall | PASS | 5/5 original files restored byte-for-byte; fake game unchanged; no owned payload/metadata remains. |
| Setup | PASS | Inno 6.7.3 compiled; 223 ZIP payload hashes match after install/update; settings retained by default and removed on opt-in; shader cache removed; 0 owned files or HKCU Apps entries remain. Shortcuts disabled in qualification; no launcher execution. |
| Cleanup | PASS | Owned TEMP test tree deleted; no existing installation, cache, shortcuts or game files changed. Setup Apps cleanup is checked in the Setup row. |

Archive: C:\Users\quali\Documents\Code Projects\BG3RTX\.scratch\rc\20261105-160045-29c248b9\BG3RTX-rc-20261105-f004d929-29c248b9.zip

Scope: real ZIP legacy install.ps1/uninstall.ps1 route, no shortcut, settings retained. Update reapplies this candidate, not a different release.
Only synthetic installer child shells mock named-process discovery, as release_install_test.py does; build process guards remain real. No game or payload binary executes.
Setup row covers compiled Inno setup/update/uninstall in TEMP, with shortcuts disabled and isolated LOCALAPPDATA; only PASS qualifies that route. No fresh-PC, signing, shortcut UI, locked-file, interruption, game/visual acceptance or release/upload approval is claimed.
Git metadata is read-only. Generated archives, symbols and logs remain under .scratch/rc and must not be added to Git.
