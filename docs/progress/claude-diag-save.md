## Diagnostic report: opt-in latest save, Home link (branch claude/diag-save, ready for review, 2026-11-09)

- Report dialog: new checkbox "Include my latest save (helps reproduce the problem; contains your character and
  progress)", unticked by default. The native fallback preview asks the same question (default No);
  `Diagnostics.cmd` asks once when started without arguments (Enter = No), or takes `-IncludeLatestSave`.
- Save selection (launcher `diagnostics::find_latest_save`, ps1 `Find-LatestSave`): newest `.lsv` by last write under
  `%LOCALAPPDATA%\Larian Studios\Baldur's Gate 3\PlayerProfiles\<profile>\Savegames\Story\<folder>\`, plus its `.webp`.
  Bounded nonrecursive listing, links skipped. Copied read-only with writers denied (a save being written is
  skipped with "close the game"), into `saves/<folder>/` with the DOS read-only bit on the archived copy.
  `CONTENTS.txt` names the save and its size. Cap 200 MB (lsv + webp): a bigger save is left out and named.
- The save bytes never reach the preview (JSON `binary`, no contents view; native preview shows the note).
- Home: the Play card ends with "Problem? Create a diagnostic report…" (same `report` message as Support).
  Native fallback Overview has the same button. No new knob or category.
- Tests: launcher_diagnostics_test (fixture profiles: newest Story save picked over older/decoy files, exact bytes,
  source time/attributes unchanged, cap, writer-locked save, none found, ZIP read-only bit via Python),
  launcher_web_bridge_test (save flag default off, bytes not in JSON), launcher_ui_test (Home link, unticked by
  default, no contents view for a save), diagnostics_package_test (ps1 latest save, none found, refused while running).
- Docs: USER_SETTINGS.md, release README, changelog_0.9.5 (Launcher). Packaging lists unchanged (no new files).
- Check in the launcher: Home > "Create a diagnostic report…", tick the save box, Preview files: the save shows as
  `saves/<folder>/<name>.lsv` with its size and no contents view.
