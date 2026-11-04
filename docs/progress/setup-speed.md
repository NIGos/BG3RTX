## Installer feedback and lifecycle speed (branch setup-speed, qualification blocked, 2026-11-04)

- Setup now paints a clear status before extraction, Install, Inspect, Recover and Commit;
  uninstall paints its backup/removal status once its progress form exists.
  Inno 6.7.3 already pumps messages inside synchronous Exec; no custom polling or reentrancy.
  Reference: [Inno wait loop](https://github.com/jrsoftware/issrc/blob/is-6_7_3/Projects/Src/Setup.InstFunc.pas#L592).
- Replaced repeated PowerShell provider ancestor walks with a small in-process C# filesystem
  walk and streamed .NET SHA256. No path approval cache. Every ancestor/game-path check,
  both source/stage hash passes, staging, snapshots, journal writes and rollback remain.
- Real-payload timing is BLOCKED by the explicit 20 MB file limit: dxcompiler.dll,
  nvngx_dlss.dll and nvngx_dlssd.dll exceed it. Initial temporary copies of these were removed.
  No exception was assumed. The user's reported real-payload baseline is about 75 s wall / 47 s CPU.
- Measured a bounded fixture with the real 219 paths and 216 unchanged files (35.09 MB),
  replacing only those three DLLs with tiny placeholders and regenerating fixture hashes.
  Child process discovery was mocked as in the existing fixtures because the real game/launcher
  are running. Only disposable TEMP destinations and isolated LOCALAPPDATA were used.
  These figures do not qualify real-payload performance or the well-under-15 s target.

| Phase | Before (s) | After (s) |
|---|---:|---:|
| Preflight, including source verification | 19.710 | 3.473 |
| Staging copy | 17.976 | 4.955 |
| Staged verification | 5.801 | 1.690 |
| Backup snapshot (fresh install) | 2.390 | 0.592 |
| Journal and destination apply | 16.915 | 3.520 |
| Install -DeferCommit, including process startup | 63.103 | 14.508 |
| Separate Commit, including process startup | 17.470 | 3.287 |

- SHA256 time inside the above phases: 1.085 -> 0.628 s total. Path/provider overhead dominates.
- Final checks: release_lifecycle_test.py 16/16 PASS (41.59 s), release_setup_test.py
  4/4 PASS (2.35 s). New fixture creates a junction after preflight and confirms rechecking
  rejects it before journalling or touching its target. git diff --check PASS.
- Full verify_rc.ps1 was invoked as requested; exit 1 at its running-game/launcher build guard.
  No process was stopped. Its generated failure report is retained in .scratch/verify-rc-blocked.md;
  the tracked RC_CHECK.md was restored to its prior bytes. Separate tiny compile-only fixture
  also failed before compilation: sandbox denies reading the pinned ISCC.exe. No setup ran.
- Remaining: authorize oversized disposable artifacts/payload copies, obtain compiler access,
  then profile the real payload and rerun full qualification when the game/launcher are closed.
  Existing install and user settings untouched; no game launched; no commit.
