# Release candidate check

Date: 2026-11-08 23:03:46 +01:00. Commit: 32a0d5ac0e41eb35bc10167036c789cbcc7b610a.
Source state at start: clean tracked files (untracked files excluded).
Result: FAIL. Candidate version: rc-20261108-32a0d5ac-2d7f9a60.

Rerun: `powershell -NoProfile -ExecutionPolicy Bypass -File scripts/release/verify_rc.ps1`.

| Item | Result | Evidence |
|---|---|---|
| Build | PASS | Existing outputs reused (-NoBuild); compilation/freshness not checked by this invocation. Output files/cache paths checked; diagnostics OFF. |
| Archive | PASS | Real ZIP, 73.33 MiB; SHA256 ca92c4e2be9158e722b22f675a1f7bcb8df9e030486919b743603bd97edd22f2; symbols kept in separate archive. |
| Manifest | PASS | 229 files; expected 229 (delta 0); 0 differences across ZIP / extracted / files.txt / source plan. Runtime source dependencies covered. |
| Hashes | PASS | 12 shipped DLLs and launcher match build/copy sources; 226 verbatim files checked; 9 Streamline DLLs match production SDK. |
| Exclusions | PASS | 0 symbols, developer tools, SDK payloads, Aftermath DLLs, Python or unexpected binaries. Private symbols outside player ZIP. |
| Licences | PASS | 15 required notices plus project licences and font OFL present, verbatim hashes checked; README licence lines 3-4 PASS. |
| Version | PASS | rc-20261108-32a0d5ac-2d7f9a60; commit matches HEAD; layer lean. |
| Install | PASS | 229 real payload files hash-match after install; fake game unchanged; 3 original collisions backed up. |
| Update | PASS | 229 files hash-match after same-candidate update; corrupted installed DLL repaired; original backups, settings and unrelated file preserved. |
| Uninstall | PASS | 5/5 original files restored byte-for-byte; fake game unchanged; no owned payload/metadata remains. |
| Setup | FAIL | verify compiled Setup failed (exit 1); see E:\Storage\Code Projects\BG3RTX\.scratch\rc\20261108-230137-2d7f9a60\verify_compiled_Setup.stdout.log and E:\Storage\Code Projects\BG3RTX\.scratch\rc\20261108-230137-2d7f9a60\verify_compiled_Setup.stderr.log |
| Cleanup | PASS | Owned TEMP test tree deleted; no existing installation, cache, shortcuts or game files changed. Setup Apps cleanup is checked in the Setup row. |

Archive: E:\Storage\Code Projects\BG3RTX\.scratch\rc\20261108-230137-2d7f9a60\BG3RTX-rc-20261108-32a0d5ac-2d7f9a60.zip

Scope: real ZIP legacy install.ps1/uninstall.ps1 route, no shortcut, settings retained. Update reapplies this candidate, not a different release.
Only synthetic installer child shells mock named-process discovery, as release_install_test.py does; build process guards remain real. No game or payload binary executes.
Setup row covers compiled Inno setup/update/uninstall in TEMP, with shortcuts disabled and isolated LOCALAPPDATA; only PASS qualifies that route. No fresh-PC, signing, shortcut UI, locked-file, interruption, game/visual acceptance or release/upload approval is claimed.
Git metadata is read-only. Generated archives, symbols and logs remain under .scratch/rc and must not be added to Git.
This invocation used -NoBuild: only existing output presence/cache paths and lean configuration were checked. Compilation and source freshness require separate build evidence; rerun the default command after the last promotions.
