# Release candidate check

Date: 2026-11-04 18:23:49 +01:00. Commit: ccbbb1d28130eb97a00a1a6f69671cf5a998cbd3.
Source state at start: clean tracked files (untracked files excluded).
Result: PASS. Candidate version: rc-20261104-ccbbb1d2-0efe5f84.

Rerun: `powershell -NoProfile -ExecutionPolicy Bypass -File scripts/release/verify_rc.ps1`.

| Item | Result | Evidence |
|---|---|---|
| Build | PASS | Core, launcher and lean layer built from this tree; diagnostics OFF. No shader compile or game run. |
| Archive | PASS | Real ZIP, 72.60 MiB; SHA256 29f1fa9a29acdcffc13c8933de8c6fdb171fcb40b8988175b38de5ef8238446f; symbols kept in separate archive. |
| Manifest | PASS | 229 files; expected 229 (delta 0); 0 differences across ZIP / extracted / files.txt / source plan. |
| Hashes | PASS | 12 shipped DLLs and launcher match build/copy sources; 226 verbatim files checked; 9 Streamline DLLs match production SDK. |
| Exclusions | PASS | 0 symbols, developer tools, SDK payloads, Aftermath DLLs, Python or unexpected binaries. Private symbols outside player ZIP. |
| Licences | PASS | 15 required notices plus project licences and font OFL present, verbatim hashes checked; README licence lines 3-4 PASS. |
| Version | PASS | rc-20261104-ccbbb1d2-0efe5f84; commit matches HEAD; layer lean. |
| Install | PASS | 229 real payload files hash-match after install; fake game unchanged; 3 original collisions backed up. |
| Update | PASS | 229 files hash-match after same-candidate update; corrupted installed DLL repaired; original backups, settings and unrelated file preserved. |
| Uninstall | PASS | 5/5 original files restored byte-for-byte; fake game unchanged; no owned payload/metadata remains. |
| Cleanup | PASS | Owned TEMP test tree deleted; no global installation, cache, shortcuts or game files changed. |

Archive: C:\Users\quali\Documents\Code Projects\BG3RTX\.scratch\rc\20261104-182143-0efe5f84\BG3RTX-rc-20261104-ccbbb1d2-0efe5f84.zip

Scope: real ZIP legacy install.ps1/uninstall.ps1 route, no shortcut, settings retained. Update reapplies this candidate, not a different release.
Only synthetic installer child shells mock named-process discovery, as release_install_test.py does; build process guards remain real. No game or payload binary executes.
Separate Inno Setup is not packaged or compiled by this check. No fresh-PC, signing, locked-file, interruption, game/visual acceptance or release/upload approval is claimed.
Git metadata is read-only. Generated archives, symbols and logs remain under .scratch/rc and must not be added to Git.
