# Windows release setup

`BG3RTX.iss` targets **Inno Setup 6.7.3**, Windows PowerShell 5.1 and a standard Windows user.
Compiler: `C:\Users\quali\AppData\Local\Programs\Inno Setup 6\ISCC.exe`.
Pinned Inno installer SHA256:
`9c73c3bae7ed48d44112a0f48e66742c00090bdb5bef71d9d3c056c66e97b732`.
`build_setup.ps1` rejects a different compiler banner version. The hash identifies the
compiler distribution installer, not ISCC.exe or the generated BG3RTX setup.
No elevation fallback, global Vulkan registration, game-folder deployment or game launch is used.
Only Inno creates/removes its HKCU Apps entry. The lifecycle does not access the registry.

## Build from the ZIP payload

```powershell
.\scripts\package.ps1 -Version 0.1.0-beta1 -OutDir .scratch\release
.\scripts\release\build_setup.ps1 `
  -PayloadDir .scratch\release\BG3RTX-0.1.0-beta1 -OutDir .scratch\release\setup
```

The second command reads the version from the assembled `VERSION.txt`, writes
`payload.sha256.json` and the combined GPL/NVIDIA-exception licence page beside the
setup, then compiles `BG3RTX-<version>-Setup.exe`. All setup outputs must be under
this checkout's `.scratch/`; never commit them. The original ZIP payload remains
**229 files**: the hash manifest is setup metadata, embedded separately and not deployed.
Every deployed payload byte comes from the same assembled folder used for the ZIP.
`verify_rc.ps1` compiles from the extracted ZIP, proving archive/setup equivalence.

Setup uses `assets/branding/bg3rtx.ico`; Apps and shortcuts use its identical payload
copy. Start menu shortcut is selected; desktop is unchecked. The finish page offers
"Start BG3RTX" (suppressed in silent setup). The welcome page says:
"The game folder is not changed; set the game to Vulkan + DLSS."
The default is `%LOCALAPPDATA%\Programs\BG3RTX`, per-user, without admin.
The fixed AppId supports upgrades in place and preserves user settings.

Configure timestamped Authenticode for setup, uninstaller and launcher before public release.
No signing identity or trusted-publisher claim is fabricated here.

### Payload contract

The build step generates UTF-8 `payload.sha256.json` **after** assembling the final payload:

```json
{"schema":1,"files":[{"path":"bin/BG3RTX.exe","sha256":"<64 hex digits>"}]}
```

List every deployed payload file once, with its SHA256; the example is not a usable package.
Include VERSION.txt, runtime data, licences, release defaults and helpers as required by C2.
The launcher payload includes `bin/ui/{index.html,app.css,app.js}`, `bin/ui/fonts/`
(both Lato files, full OFL, README and identity manifest), and `bin/ui/branding/`.
Inno's recursive payload entry includes these files; preprocessor checks reject a missing
UI entrypoint, stylesheet, script, font, OFL or emblem. The same files are in the portable
package. No WebView2 runtime installer is bundled or invoked: the launcher detects the
installed runtime and falls back to its native controls when it is absent.
`scripts/package.ps1 -DryRun` validates and lists notices and this static launcher payload
without a build or output writes; missing release notices stop validation.
`-DryRun -OutDir <fresh scratch>` audits the complete planned payload, writes `files.txt`,
`payload-list.json`, `files.diff.json` and `audit.json`, and reports missing runtime inputs.
No runtime files are copied in that mode. A zero manifest diff proves list consistency only;
it does not validate missing binaries, build freshness, signing or a final archive.
Do not list the hash manifest itself. Hashes verify integrity, not publisher authenticity.
The .iss embeds the payload and extracts it into a private Inno temporary directory. Its
PowerShell helper stages and verifies every listed file again before destination mutation.
The helper rejects duplicate/canonical duplicate paths, traversal, ADS, device paths, reparse
points in ancestors, settings/control files, unins* and setup-support in the payload allowlist.
Inno exclusively owns `setup-support/` (helper and uninstaller), shortcuts and the Apps entry.
The lifecycle exclusively owns the hashed files and `.bg3rtx-state.json`.
Do not package arbitrary files outside the allowlist. The RC Setup stage checks portable/setup equivalence.

## Lifecycle entry points

For automation/portable installs, invoke the same engine directly, without Python or an SDK:

```powershell
.\lifecycle.ps1 -Action Install -Destination "$env:LOCALAPPDATA\Programs\BG3RTX" -Payload C:\release\payload
.\lifecycle.ps1 -Action Inspect -Destination "$env:LOCALAPPDATA\Programs\BG3RTX"
.\lifecycle.ps1 -Action Restore -Destination "$env:LOCALAPPDATA\Programs\BG3RTX"
.\lifecycle.ps1 -Action Recover -Destination "$env:LOCALAPPDATA\Programs\BG3RTX"
.\lifecycle.ps1 -Action Uninstall -Destination "$env:LOCALAPPDATA\Programs\BG3RTX"
```

For setup-owned installs, use Apps for uninstall; the setup/UI caller passes `-Owner inno`
for Inspect/Restore/Recover. `Restore.cmd <install-path>` is an explicit support fallback.
The A2 Support UI can consume Inspect JSON (`restoreAvailable`, `backupPath`, `owner`) and
invoke Restore after explaining that the previous version **and settings** will be restored.
A2/C2 must check interrupted lifecycle state before enabling Play; no launcher dispatch changes
belong to C1. Setup itself offers recovery before retrying a blocked transaction.

Install validates hashes, containment, process closure, file collisions and conservative free
space on both drives. It stages all incoming files, snapshots the complete touched file set
and both user INIs, then atomically publishes a recovery journal **before** replacement/removal.
Copy errors restore the snapshot. A stopped process leaves `pending.json`; the next mutation
refuses until Recover succeeds. Recover is idempotent and prevalidates the complete snapshot.
`-DeferCommit` leaves the journal pending for Inno; only successful Inno file/shortcut work calls
Commit. Setup abort calls Recover. Restore snapshots the current set first, so it can be undone.
The first clean install has no previous installed version; Restore becomes available after update.

Backups are under `%LOCALAPPDATA%\BG3RTX\backups\<destination SHA256>\txn-<id>\snapshot`.
The destination hash separates simultaneous installs without trusting a version as a filename.
Snapshots include the previous payload's VERSION.txt when supplied. `previous.json` selects one
verified previous complete state. Successful commits remove superseded recorded snapshot files
and staged files, never recursively deleting a directory. Empty directories and failed/interrupted
transaction artifacts are retained for recovery; do not bulk-clean this tree or follow links.

Uninstall removes only manifest-owned program files. It keeps both INIs, reports, logs, unrelated
files and original collision backups by default. Direct lifecycle calls also keep shader cache by default. `-RemoveSettings` opts into removing
the two INIs. `-RemoveShaderCache` separately opts into deleting only files inside the standard
per-user cache; it preflights the whole tree for links and prints its file count and byte size.
Cache removal is disposable cleanup after the program transaction, outside rollback. Reports
are always retained, including reports stored elsewhere. No save folder is enumerated.
Inno always requests shader-cache removal, matching the ZIP uninstaller. Its settings prompt
defaults to keeping `bg3rtx_user.ini` and `bg3rtx_launcher.ini`; silent uninstall keeps them
unless `/REMOVESETTINGS=1` is supplied. Inno owns removal of its shortcuts and Apps registration. Clear any BG3RTX Steam launch option
manually, as the uninstall completion message explains. No Steam configuration is edited.

## Legacy transfer

Setup asks for explicit transfer when `.bg3rtx_install` exists. Direct automation requires
`-TransferLegacy`. Validate the complete old install_manifest.txt, journal the old metadata and
files, and keep `.bg3rtx_backup` originals intact. On failure/recovery the legacy ownership is
restored. On success the new state owns the install; the old marker/manifest are removed.
Uninstall restores legacy collision originals, including retired files. The legacy install and
uninstall scripts refuse a lifecycle-owned folder and direct setup owners to Windows Apps.
The old scripts remain available for old ZIP packages; their historic copy algorithm is not
the transactional fallback. C2 should ship the new lifecycle entry points for new releases.

## Verification and remaining release checks

Run `python tests/release_lifecycle_test.py` and `python tests/release_install_test.py`.
These execute temporary script copies with fake binary text payloads and child-only process
discovery mocks, a temporary LOCALAPPDATA and Unicode/space paths. No executable payload runs.
Fixtures cover clean/update/restore/redo, fresh and late-copy failure, incomplete/corrupt payload,
interrupted recovery and deferred commit, settings/report/cache policy, legacy ownership,
unrelated collisions, corrupt backups, game/profile rejection and ancestor junction attacks.
The development process is not elevated. Existing legacy fixture checks also remain applicable.

Run `powershell -NoProfile -ExecutionPolicy Bypass -File scripts/release/verify_rc.ps1`.
Its Setup row compiles from the extracted ZIP and runs actual setup silently using
`/VERYSILENT /SUPPRESSMSGBOXES /CURRENTUSER /DIR=<throwaway>` and `/TASKS=""`.
It refuses an existing BG3RTX Apps entry, isolates TEMP/LOCALAPPDATA, checks 229 hashes
after install and update (including repair of a corrupt DLL), then checks default
settings retention, explicit settings removal, shader-cache removal and zero owned
files or HKCU Apps entries after uninstall. Only the generated uninstaller removes
its Apps entry. No Start menu/desktop shortcut or launcher runs during qualification.
See `docs/release/RC_CHECK.md` and `docs/progress/inno-setup.md` for actual outcomes;
implementation alone is not a compiled-installer pass.

Still qualify in a disposable standard-user VM: wizard/shortcuts/finish-launch UI,
abort, unwritable destination, disk exhaustion, locked files and forced termination.
Fixture failure injection does not prove OS power-loss durability. Fresh-PC, signing,
game/visual acceptance and upload approval remain separate.

Inno extraction semantics: [ExtractTemporaryFiles](https://jrsoftware.org/ishelp/topic_isxfunc_extracttemporaryfiles.htm).
Per-user mode: [PrivilegesRequired](https://jrsoftware.org/ishelp/topic_setup_privilegesrequired.htm).
