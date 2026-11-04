# Release candidate check

Date: 2026-11-04 18:51:33 +01:00. Commit: 3622f36e549d9c6ba300dbbf777662aed83f1ce5.
Source state at start: clean tracked files (untracked files excluded).
Result: FAIL. Candidate version: rc-20261104-3622f36e-9973c0aa.

Rerun: `powershell -NoProfile -ExecutionPolicy Bypass -File scripts/release/verify_rc.ps1`.

| Item | Result | Evidence |
|---|---|---|
| Build | PASS | Core, launcher and lean layer built from this tree; diagnostics OFF. No shader compile or game run. |
| Archive | PASS | Real ZIP, 72.61 MiB; SHA256 42785b2ccf9110b608d4978989046b9c5c5e44100e1ea155c92a2ec73c360d7c; symbols kept in separate archive. |
| Manifest | PASS | 229 files; expected 229 (delta 0); 0 differences across ZIP / extracted / files.txt / source plan. |
| Hashes | PASS | 12 shipped DLLs and launcher match build/copy sources; 226 verbatim files checked; 9 Streamline DLLs match production SDK. |
| Exclusions | PASS | 0 symbols, developer tools, SDK payloads, Aftermath DLLs, Python or unexpected binaries. Private symbols outside player ZIP. |
| Licences | PASS | 15 required notices plus project licences and font OFL present, verbatim hashes checked; README licence lines 3-4 PASS. |
| Version | PASS | rc-20261104-3622f36e-9973c0aa; commit matches HEAD; layer lean. |
| Install | PASS | 229 real payload files hash-match after install; fake game unchanged; 3 original collisions backed up. |
| Update | PASS | 229 files hash-match after same-candidate update; corrupted installed DLL repaired; original backups, settings and unrelated file preserved. |
| Uninstall | PASS | 5/5 original files restored byte-for-byte; fake game unchanged; no owned payload/metadata remains. |
| Setup | FAIL | verify compiled Setup failed (exit 1); see C:\Users\quali\Documents\Code Projects\BG3RTX\.claude\worktrees\inno-setup\.scratch\rc\20261104-184848-9973c0aa\verify_compiled_Setup.stdout.log and C:\Users\quali\Documents\Code Projects\BG3RTX\.claude\worktrees\inno-setup\.scratch\rc\20261104-184848-9973c0aa\verify_compiled_Setup.stderr.log |
| Cleanup | PASS | Owned TEMP test tree deleted; no existing installation, cache, shortcuts or game files changed. Setup Apps cleanup is checked in the Setup row. |

Archive: C:\Users\quali\Documents\Code Projects\BG3RTX\.claude\worktrees\inno-setup\.scratch\rc\20261104-184848-9973c0aa\BG3RTX-rc-20261104-3622f36e-9973c0aa.zip

Scope: real ZIP legacy install.ps1/uninstall.ps1 route, no shortcut, settings retained. Update reapplies this candidate, not a different release.
Only synthetic installer child shells mock named-process discovery, as release_install_test.py does; build process guards remain real. No game or payload binary executes.
Setup row covers compiled Inno setup/update/uninstall in TEMP, with shortcuts disabled and isolated LOCALAPPDATA; only PASS qualifies that route. No fresh-PC, signing, shortcut UI, locked-file, interruption, game/visual acceptance or release/upload approval is claimed.
Git metadata is read-only. Generated archives, symbols and logs remain under .scratch/rc and must not be added to Git.
