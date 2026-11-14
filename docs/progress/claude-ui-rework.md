## Launcher UI rework, Direction A with instant apply (branch claude/ui-rework, ready for review, 2026-11-14)

- User decision 2026-11-14: Direction A; a setting applies when clicked (no Save, no review, no unsaved state);
  dark only; Low-latency scene on the Experimental page. Audit, proposal, mockups: `docs/ui_rework/`.
- Phase 1, layout: sidebar with Play and version, preset in the header, setting rows in groups with indented
  children (RTX Hair with Detail at distance and Motion stability; Tracing speed collapsed), detail panel
  (inline below 1100 px), one amber restart badge with its direction, cost chip, Experimental page with its
  switch, Home reduced to attention plus preparation, Help regrouped with the version.
- Phase 2, instant apply: every change is written at once through the save hook (`Bridge::commit`).
  Dependencies resolve at the click (settings.h logic unchanged); the reply names what else changed, shown in
  the clicked row and in the status strip with Undo (one write). A dependent lowered with its prerequisite
  returns with it in the same session only (the file has no memory). A failed write changes nothing and offers
  Retry. Restart indicator by name, rows carry "Restart pending". Play only launches; a file that lacks a
  prerequisite or startup key is completed first and named. Removed: save, confirm, cancel, discard,
  `dirty()`, the review dialog, the close prompt.
- Phase 3, copy: one benefit line per setting without numbers, cost in the `Cost` struct, the rest in the
  detail text; "Off" of Detail at distance is "Full"; "(experimental)" dropped from the DLSS 5 label; the
  bridge sends what each preset sets. Ids, keys and preset values unchanged; defaults check clean; schema
  regenerated (`bin/settings_test.exe --write-schema`). `docs/USER_SETTINGS.md` rewritten for instant apply.
- Phase 4, keyboard and screen readers: one Tab stop per group of choices, arrows move without writing,
  Up/Down in the sidebar, Ctrl+Z undoes, the status strip stays a live region while empty, switches have
  `role="switch"`, child rows keep their full name for screen readers.
- Behaviour changes to know: an option whose prerequisite cannot be met is refused at the click (it used to be
  accepted and blocked at Save); `--no-game` review mode shows a write error on a click; sliders write on
  release; the basic fallback window (`ui_product.h`) is unchanged and keeps its own Save.
- Tests: `launcher_web_bridge_test` (instant apply, dependency apply and undo, remembered dependents, failed
  write and retry, restart names, opt-in off), `launcher_ui_state_test`, `tests/launcher_ui_test.js` (shell,
  rows, notice, undo, retry, restart pending, keyboard, sample data against the table), JSON test (copy
  contract). Full `build.ps1 -Test`: 226 of 226 (2026-11-14).
- Screenshots of the real UI with sample data (headless Edge): `docs/ui_rework/shots_impl/` (17 files).
- Not done, not verified: nothing was run in the real WebView2 window or with the game (no live); the page
  was rendered in headless Edge only. Measured GPU cost is still missing for most settings (chip only where
  the table has a number). The sample after a copy change: `py -3 tools/launcher/sync_ui_sample.py <exe>`.
- How to check: open `tools/launcher/ui/index.html?sample&chrome=1` in a browser (`&view=notice`, `restart`,
  `error`, `missing`, `hairerror`; `&experimental` shows the opt-in options). Not merged.
