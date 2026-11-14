## Launcher UI rework, Direction A with instant apply (branch claude/ui-rework, in progress, 2026-11-14)

- User decision 2026-11-14: Direction A; settings apply when clicked (no Save, no review, no unsaved state);
  dark only; Low-latency scene on the Experimental page. Audit, proposal and mockups: `docs/ui_rework/`.
- Phase 1 (done): sidebar with Play and version, preset in the header, setting rows in groups with indented
  children, detail panel (inline below 1100 px), one amber restart badge with its direction, cost chip,
  Experimental page with its switch, Home reduced to attention plus preparation, Help regrouped.
- Phase 2 (done): every `set`, preset, hotkey, DLSS 5 runtime and opt-in change is written at once through the
  save hook (`Bridge::commit`). Dependencies resolve at the click (`preview_dependencies`, `lower_dependents`
  unchanged in settings.h): the reply names what else changed (`notice.also`), shown in the clicked row and
  in the status strip, with Undo (one write, restores everything the click changed). A dependent lowered with
  its prerequisite comes back when the prerequisite does (same session only; the file has no memory).
  A failed write changes nothing and shows a red banner with Retry. Restart indicator by name
  (`ui_state::restart_names`, `restartNames`), rows carry "Restart pending". Play only launches; a file from
  an older version that lacks a prerequisite or startup key is completed first and named.
  Removed: `save`, `confirm`, `cancel`, `discard`, `dirty()`, the review dialog, the close prompt.
- Behaviour changes to know: an option whose prerequisite cannot be met is refused at the click (it used to
  be accepted and blocked at Save); `--no-game` review mode shows a write error on every click; the basic
  fallback window (`ui_product.h`) keeps its own Save.
- Tests: `launcher_web_bridge_test` (instant apply, dependency apply and undo, remembered dependents, retry,
  restart names, opt-in off resets Low-latency scene), `launcher_ui_state_test` (names), renderer test
  (`tests/launcher_ui_test.js`: shell, rows, notice, undo, retry, restart pending, sample data against the
  table with the opt-in on), JSON test. `build.ps1 -Test -TestRegex "launcher|settings|package"`: 12 of 12.
- Screenshots of the real UI with sample data (headless Edge): `docs/ui_rework/shots_impl/`.
- Not done yet: phase 3 (copy pass in settings.h, cost line from the Cost struct, preset values in the detail,
  the four accepted renames, USER_SETTINGS.md), phase 4 (keyboard pass, ARIA review, fallback wording).
- How to check: open `tools/launcher/ui/index.html?sample&chrome=1` in a browser (views: `&view=notice`,
  `restart`, `error`, `missing`, `hairerror`; `&experimental` shows the opt-in options).
