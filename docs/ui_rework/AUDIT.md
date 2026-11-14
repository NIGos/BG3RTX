# Launcher UI audit (2026-11-14, 0.9.6, main bdee48f6)

Scope: `tools/launcher/ui/` (app.js, app.css, index.html), `common/settings.h`, `web_bridge.cpp`,
`preparation.cpp`, `ui_product.h`. Studied in a plain browser with `?sample&chrome=1&section=...` and the
views `missing`, `probe`, `error`, `restart`, `report`, `experimental`, `granny`, `hairerror`, at
1280 x 800 and 1000 x 700. Nothing was built or launched. Severity: H = blocks or misleads a top task,
M = slows or confuses, L = polish.

## Users and their top tasks

| User | Task | Today |
|---|---|---|
| Player | Pick a preset and play | Works, but Play and the preset sit in different places on Home and on the other pages |
| Tweaker | Change two or three things and understand the cost | Has to read 12 full cards on Lighting; cost is inside prose; "Details and requirements" hides the rules |
| Tester | Try an experimental feature, compare, turn it back | Three different opt-in models; turning the switch off resets values with one status line |
| Player with a problem | Fix the game folder, send a report | Report is reachable from two places with two names; folder and GPU live under "Help" |

## Problems

### Structure and navigation
1. **H. Play, preset and status move between pages.** Home hides the header and the footer Play and draws its
   own (app.js:433, 443, 349-355); every other page has the preset in a header and Play in the footer. A player
   learns two layouts. Home also shows Save twice (status list app.js:330 and footer).
2. **H. Home is four pages in one.** Play, a status list, the RTX Hair switch, hair conversion with its source,
   granny2.dll picker and schedule (app.js:241-301), shaders and a key list. At 1280 x 800 the conversion
   options are below the fold; the four green "all fine" lines push the one line that matters down.
3. **M. Pages mix qualified, tuning and experimental controls.** Performance shows Frame Generation, Reflex
   and the frame limit next to two engine optimisations ("Bounce trace reordering": "shader execution
   reordering", settings.h:339) and "Low-latency scene", whose description starts with "Experimental:"
   (settings.h:352). Nothing separates what a player should touch from what exists for troubleshooting.
4. **H. Three models for experimental features.** (a) A switch in the navigation footer reveals an Experimental
   page (app.js:417, settings.h:1314 `kOptInKnobs`); (b) Low-latency scene sits on Performance with an
   experimental choice gated by the same switch; (c) DLSS 5 has its own page and "(experimental)" in its
   label, RTX Hair says "Beta:" in its description. The switch is far from what it controls.
5. **M. RTX Hair has no hierarchy.** On Lighting the order is Hair shadows, RTX Hair, RTX Hair detail at
   distance, RTX Hair motion stability (app.js:388): four equal cards. Hair shadows is about the game's own
   hair, not strands. The RTX Hair switch is duplicated on Home; conversion is only on Home; the fourth strand
   control (Strand hair edge resolve) is on Experimental. The two sub-controls do nothing when RTX Hair is
   off, and nothing shows it.
6. **M. "Ray-tracing detail" is a master switch drawn as an ordinary card.** Off disables all ray-traced
   lighting, shadows, reflections and water (settings.h:166); that sentence is inside the collapsed details.
7. **M. Hotkeys are in four places**: the Hotkeys page, a Home card, a line on the setting's card and a paragraph
   on Help ("Comparison and overlays", app.js:383-384). Ctrl+Shift+F10 is called reserved without saying why.
8. **L. Help & support mixes setup, a setting and help.** Game folder and GPU checks (setup), Detailed logging
   (a restart setting), reports and tips share one grid; the report is "Create a diagnostic report…" on Home
   and "Report a problem…" on Help.

### Applying changes
9. **H. Every card carries an apply badge, so the badge says nothing.** "Applies after saving" is on about
   two thirds of the cards (app.js:77-83). The useful one, "Restart to apply", is the same size and place.
   Mixed cases become "May need a restart" with the direction only in a tooltip (Frame Generation, Water
   lighting, Hair shadows, Motion stability, Low-latency scene).
10. **H. Dependencies change things where the player is not looking.** Turning something on raises what it
    needs and reports it only in the footer status line ("Also turned on: ...", web_bridge.cpp:211). Turning
    something down lowers its dependents at Save time, in a dialog titled "Review required settings"
    (web_bridge.cpp:152, app.js:448). This is the flow in which RTX Hair came back on in 0.9.6.
11. **M. Save, Play and the dialog follow three rules.** Save is silent when nothing else changes, opens a
    dialog otherwise; Play saves without saying so except in a grey sentence on Home; a preset click always
    opens a dialog. There is no per-control undo, only "Discard changes" for everything.
12. **M. "Restart needed" is a small line in the footer** (index.html footer, app.js:441) that names a
    count, not the settings. It does not say whether the game is running. Cards do not show what is pending.
13. **M. Turning the experimental switch off resets every experimental value** with one status line
    ("Experimental options hidden and reset to Off", web_bridge.cpp:228) and no confirmation.
14. **L. Preset state is unclear.** "Custom" is a label after the four buttons (app.js:315); it does not say
    which preset it is based on, what differs or how to go back. Turning on RTX Hair (off in every preset)
    is enough to become Custom.

### Copy
15. **M. Descriptions are long and uneven.** One line ("Reduces noise in ray-traced lighting.") up to three
    sentences with scene names ("about 1.5 ms faster at the camp and 0.8 ms at the inn on top of the faster
    bounce light tracing"). They mix benefit, mechanism, cost and apply rules.
16. **M. Cost is inconsistent.** A regular expression pulls "About N ms" off the end of some descriptions
    (app.js:85); costs in the middle stay in the prose; many controls have none. The `Cost` struct
    (settings.h:123) is shown only in developer mode (app.js:126). Savings and costs look the same.
17. **M. Labels need knowledge the player does not have.** "Low-latency scene", "Bounce trace reordering",
    "By hit kind and object", "RTX Hair detail at distance: Off" (Off is the most expensive choice),
    "DLSS 5 Neural Rendering (experimental)", "Rendered frame limit" with Off/On next to a slider.
18. **L. "Details and requirements" is the same closed disclosure on every card** (app.js:129); it holds both
    trivia and the one rule that matters ("Needs Ray-tracing detail: Include foliage").

### Layout and visual
19. **M. Two-column card grid** (app.css:94): reading order is ambiguous, cards in a row stretch to the
    tallest one (large empty areas under short descriptions), related cards land in different rows. At
    1000 px it becomes one column of 12 tall cards, about 2200 px of scrolling on Lighting.
20. **M. Version is hard to find.** It is in the native title bar and in a grey facts line on Home
    (app.js:351); not on Help, not copyable. A developer build shows "0.9.6.0 (developer build)".
21. **L. Segmented controls fall back to a dropdown** when they do not fit (app.js:170-177), so the same
    control looks different at different widths. Narrow rows with the control under the text avoid it.
22. **L. Preparation states use the "Restart" badge style for Paused, Stopped and Unavailable** (app.js:230),
    so amber means two unrelated things.
23. **L. The basic window (`ui_product.h`) has its own structure** (setup steps, combo boxes). Keep it, but
    take its copy from the same source as the web UI once labels change.

## What works and should stay
- The bridge owns policy; the page only renders state and sends intents (`set`, `preset`, `save`, `play`,
  `prep`, `shortcut`, `experimental`, report messages). No redesign needs to move logic into the page.
- Sample mode and review views (`?sample&view=...`), and the DOM-free renderer test (tests/launcher_ui_test.js).
- Preset review that lists every change before applying it; Save without a dialog when nothing else changes.
- "Needs X" reasons on unavailable choices, and options that turn on what they need.
- The report dialog: file preview, privacy text, opt-in for the save file, nothing uploaded.
- Hotkey capture with clash and reserved-key checks; preparation that never blocks Play.
- Keyboard basics: focus ring, `aria-pressed`, labelled groups, focus kept across re-render.
- The identity: charcoal and gold, flat panels, the gem mark, Lato. The rework keeps it.
