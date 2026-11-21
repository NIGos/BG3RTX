## Launcher UX polish pass (branch claude/launcher-ux, ready to merge, 2026-11-21)

- Review of every launcher page and state at 1180 x 760 and 736 x 620: `docs/LAUNCHER_UX_REVIEW.md` (13 findings,
  ranked; before/after shots in `.scratch/ui_review/`, not committed). UI only: no setting, default, preset,
  release policy, bridge, layer or core change.
- Rows with wide choices (Water lighting, Sun shadows, Indirect lighting) put the control under the text: the
  description no longer squeezes to two words per line.
- Restart badge: "Restart to apply", "Restart for X", "Restart for some choices"; "turn on/off" only for Off/On.
- Detail panel: paragraphs, Presets as four labelled lines with the current preset bold, no repeated "Off in
  every preset." in the details, dimmed cost source, "The game may pause briefly." where the apply text says so.
- One name: Hotkeys (Home card, Hotkeys page group, row line, detail heading).
- No Experimental badge on each row of the Experimental page; clearer switch copy; one primary button in the
  report dialog; header no longer jumps on preset apply; DLSS 5 placeholder in body text; sidebar 184 px below 860 px.
- Tests: launcher_ui_test.js updated for the new badge text and switch copy, plus checks for the new logic
  (badge variants, stacked rows, preset lines, details prefix). `ui_shots.js` audit: 42 pages, 0 layout problems.
- Check: open BG3RTX.exe, Surfaces > Water lighting (control under the text), any row's detail panel (Presets).
