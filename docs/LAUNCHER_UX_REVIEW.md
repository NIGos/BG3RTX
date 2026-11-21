# Launcher UX review (2026-11-21, main 067fa304, branch claude/launcher-ux)

Scope: the WebView2 launcher (`tools/launcher/ui/app.js`, `app.css`, `index.html`) in release mode: Home, Lighting,
Surfaces, Performance, DLSS 5, Experimental (switch off and on), Hotkeys, Help & support, the report dialog and the
review states (missing game, write error, restart pending, preset applied, GPU probe failed, hair conversion stopped).
Rendered offline with `node tools/launcher/ui_shots.js <dir> --sizes 1180x760,736x620`: the default window
(1180 x 760 client) and the smallest window the host allows (736 x 620). The layout audit of that tool found 0
problems before and after. No setting, default, preset, release policy, bridge, layer or core code changed.

Screenshots (not committed): `.scratch/ui_review/before/<view>-<w>x<h>.png` and `.scratch/ui_review/after/...`,
21 views x 2 sizes each. The view names are those of `ui_shots.js` (home, lighting, surfaces, performance, dlss5,
experimental-on, hotkeys, support, report, write-error, lighting-restart, lighting-preset-notice, ...).

## Findings, ranked by user impact

| # | Finding | Shot | Fix | Status |
|---|---|---|---|---|
| 1 | Wide choice controls squeeze the text column. Water lighting's 4 choices leave ~110 px for the description (seven lines of two words) and turn the restart badge into a four-line box; Sun shadows' cost chip breaks mid-number. At 736 px the last choice wraps alone onto a second line. | surfaces-1180x760, lighting-1180x760, surfaces-736x620 | Rows whose choice labels exceed ~40 characters put the control under the description at every width (`.row.stack`); a wrapped second line of choices fills the row. | Done |
| 2 | The restart badge is ambiguous or too long. "Restart" alone reads like a command or a state; "Restart for Reflections, Reflections and shadows" is a sentence in a pill; "Restart to turn on" appears on Standard/Driver metering, which have no on. | lighting-1180x760, surfaces-1180x760, experimental-on-1180x760 | "Restart to apply" (all other choices need a restart); "Restart for X" (one); "Restart for some choices" (several; the tooltip and Takes effect name them); "turn on/off" only for Off/On pairs. | Done |
| 3 | Detail panel is a wall of text. Description and details run together; the preset line "Off · Solid surfaces · Include foliage · Include foliage" wraps mid-value; "Off in every preset." opens the details and is repeated under the presets heading; the cost source repeats the numbers at full weight. | lighting-hair-1180x760, performance-1180x760, surfaces-1180x760 | Separate paragraphs; Presets as four labelled lines (Low, Medium, High, Ultra) with the current preset in bold; the leading preset sentence is dropped from the details (release only); the source note is dimmed under the numbers. | Done |
| 4 | "May briefly pause" is lost: DLSS Ray Reconstruction and Bounce trace reordering say "Applies at once; may briefly pause", but Takes effect says only "At once". | lighting (detail of DLSS RR) | Takes effect adds "The game may pause briefly." when the apply text says so. | Done |
| 5 | One concept, three names: the page is "Hotkeys", its group and the Home card say "In-game keys", the row line says "In game", the detail heading "In-game key", the link "Change keys". | home-1180x760, hotkeys-1180x760, dlss5-1180x760 | "Hotkeys" everywhere: Home card "Hotkeys" with "Change hotkeys", page group "In-game hotkeys", row line "Hotkey Ctrl+Shift+F8 Change", detail heading "Hotkey". | Done |
| 6 | Every row on the Experimental page carries an "Experimental" badge under a page titled Experimental and a purple switch that says it: noise that hides the restart badges. | experimental-on-1180x760 | No per-row badge on the Experimental page; DLSS 5 keeps its badge (it lives on its own page). | Done |
| 7 | The experimental switch copy has an unclear "them" ("Turning this off hides them and sets them all back to Off"). | experimental-on-1180x760 | "Experimental options are not fully tested and are off in every preset. Turning this switch off hides them and sets them back to Off." | Done |
| 8 | Two primary buttons in the report dialog once a preview exists ("Refresh preview" and "Save report..."). | report-1180x760 | "Refresh preview" becomes a secondary button; "Save report..." is the one primary action. | Done |
| 9 | The header jumps 11 px when a preset is applied (the "Custom, based on..." line disappears), moving every control under the pointer. | lighting-preset-notice-1180x760 | The preset note keeps one line of height when empty. | Done |
| 10 | DLSS 5 runtime: "No file chosen: looking in the game folder" is set in the monospace path font like a real path; Browse touches the text above it. | dlss5-1180x760 | Placeholder in body text ("No file chosen. BG3RTX looks for it in the game folder."); 8 px above the buttons. | Done |
| 11 | At the smallest window the 212 px sidebar takes 29% of the width; content rows wrap early. | lighting-736x620 | Sidebar 184 px below 860 px (all labels still fit). | Done |
| 12 | Preparation lead is clumsy ("also minimised; it continues where it stopped. Never blocks Play."). | home-1180x760 | "Runs while this window is open, even minimised, and resumes where it stopped. You can play at any time." | Done |
| 13 | Hotkeys hint sits flush under the panel. | hotkeys-1180x760 | 8 px gap. | Done |

## Checked and fine (no change)
- Keyboard: one Tab stop per choice group with arrow keys, focus ring (teal, 2 px) on every control, row focus bar,
  focus kept across bridge refreshes, Ctrl+Z undo outside text fields.
- Contrast: every text token on every surface is at least 4.5:1, control borders 3:1 (`tests/launcher_ui_test.py`).
- Errors and empty states: write error with Retry and Dismiss, missing game folder on Home and as a banner with
  Choose folder, GPU probe failure with Check again, hair conversion stopped with its error once, Experimental off
  with an empty-state line, detail panel empty-state text.
- Live versus restart: no badge = applies while playing; amber badge = a change waits for a restart; filled
  "Restart pending" badge plus the amber banner and the status strip "(after a game restart)" once saved.
- Units: ms with a ≈ for estimates and "(estimate)", fps, %, MB; the cost chip tooltip names the GPU.

## Deliberately left (and why)
- Water lighting's apply metadata marks every level above Off as "Restart the game to apply", while its details say
  lowering applies while you play: the badge then says "Restart for some choices" at Include refraction. The truth
  is in `common/settings.h` (apply policy), not the UI; owner decision, not a polish change.
- Turning the Experimental switch on reports "Custom value -> Off" for each option in the status strip when the
  file held no value: bridge wording (`web_bridge.cpp`), outside this pass.
- A live setting has no badge at all (by design since the 2026-11-14 rework: the absence means live, and the
  detail panel says "At once, also while the game runs"). Adding a "Live" badge on 20 rows would be noise.
- "Reset to Ctrl+Shift+F9" stays a full label (also when disabled): it tells what the default is; tests pin it.
- The preset strip stays in the header on Hotkeys and Help: one shell on every page (rework decision).
- The native fallback window (`ui_product.h`) was not reviewed: it only opens when WebView2 fails.
- Cost provenance text in the settings table repeats its numbers (e.g. Water lighting): dimmed, not rewritten,
  since the table text is shared with the developer views and the sample sync.
