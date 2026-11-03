## Setup lifecycle (branch ux-c1-setup, offline candidate, 2026-11-02)
- Added per-user Inno Setup source with stable AppId, HKCU Apps uninstall metadata,
  optional shortcuts, no elevation fallback and no automatic game launch.
- Added shared PowerShell lifecycle: SHA256 payload preflight, complete staging,
  settings snapshots, journalled replacement, rollback, interrupted recovery and restore/redo.
- Retain one verified previous snapshot under per-destination Local AppData backups.
  Clean successful staging and retired recorded files without recursive directory deletion.
- Default uninstall keeps both INIs, reports, logs, cache and unrelated files.
  Settings/cache removal are separate opt-ins; cache prompt includes count and bytes.
- Explicit legacy ownership transfer retains original collision backups; old scripts
  refuse lifecycle-owned installs and direct setup owners to Windows Apps.
- Disposable fixtures cover clean/update/restore, failed copies, deferred commit,
  interruption, corrupt payload/backup, Unicode/spaces, process guards, unrelated files
  and payload/destination/backup/cache ancestor junctions. Current token is non-elevated.
- Lifecycle fixtures: 14/14 passed in the final full run (163.63 s); added legacy-entry
  ownership guard fixture: 1/1 passed (5.38 s). Legacy install fixtures: 7/7 passed.
- Release PowerShell parser and git diff whitespace checks passed.
- ISCC.exe absent from PATH and standard locations; no setup EXE compiled or executed.
  Compilation/payload contract and VM acceptance checklist: scripts/release/SETUP.md.
- C2: generate final payload hashes, wire packaging/branding/signing, pin Inno and test
  actual Apps registration/update/uninstall in a disposable standard-user VM.
- A2/C2: consume Inspect/Restore/Recover in Support and gate Play on interrupted state.
- No game, launcher binary, real installer, saves, DXC or shader tools executed.
  Shader request delta: 0; no renderer, ABI, preset or GPU cost changes.
- Git metadata was read only; no commit, merge or branch mutation.
