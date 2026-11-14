# Release launcher QA - 2026-11-04

> 2026-11-14, branch `claude/ui-rework` (not merged): the WebView launcher applies each change at once. There is no
> Save, Discard, review dialog or unsaved-changes prompt in the WebView any more; the lines below that name them
> describe 0.9.6 and the basic fallback window, which keeps Save. New WebView contract: a click is written at
> once and named in the status strip with Undo (Ctrl+Z); dependencies change at the click and are listed in the
> row; a failed write changes nothing and offers Retry; "Restart the game to apply: <names>" stays under the
> header while the game runs with older startup settings. Pages: Home, Lighting, Surfaces, Performance, DLSS 5,
> Experimental (with its switch), Hotkeys, Help & support. Offline evidence: `launcher_web_bridge_test`,
> `tests/launcher_ui_test.js`, screenshots in `docs/ui_rework/shots_impl/`. Not yet checked in the real WebView2
> window or with the game running.

- PASS (offline): 15 release controls, shared metadata, exact choices/ranges and release visibility match CONTROL_CONTRACT.md. Preset values, keys, dependencies and apply transitions are unchanged.
- PASS (offline): both renderers use the Lighting order below; depth of field and bloom smoothing is on Surfaces. One compact header shows version, preset, GPU and mod-file presence. No total-cost tile or unknown-cost/memory badges.
- PASS (offline): measured costs appear after descriptions; estimates and separate hair-pass timings are in details. No aggregate rendering-cost or frame-rate claim.
- PASS (offline): Play saves first. Injected callbacks verify save-before-launch, failed-save edit retention, dependency-review cancellation and launch after confirmation. The game was never launched.
- PASS (offline): dependency/close dialogs name actions; restart tracking, settings metadata, DOM focus/disclosure retention, navigation and report privacy/export retain regression coverage.
- PASS (offline): Help & support owns game-folder selection, graphics recheck, GPU selection when needed, compatibility, memory, overlay/comparison guidance, an actionable notices link and one report entry. Ctrl+Shift+F9 help is here.
- PASS (offline): bundled Lato/system fallback, original branding, wrapping controls, forced-color focus and reduced motion remain.
- PASS: MSVC environment located with vswhere/vcvars; launcher and requested test targets build. Targeted CTests: 6/6 pass; settings 4,422 checks, bridge 987, diagnostics 59 and UI state 33. No Git metadata writes or rendering/shader changes.
- PASS (offline, 2026-11-08): WebView Home page, first and default. Play card (preset, Play, version/GPU/mod files), RTX Hair (control, preparation, Pause, When to convert), Shaders (preparation row) and three performance shortcuts. `tests/launcher_ui_test.js` checks order, default page, cards, identical `set` messages from Home and the own pages, `prep`, `preset` and `play`. Sample preview screenshots at 1280x900, 1.5x scale and 853 CSS px (150% DPI) reflow without clipping; the 720 px media query gives one column.
- PASS (offline, 2026-11-09): WebView information architecture. Home is a dashboard (Play, RTX Hair with conversion
  numbers, progress/ETA, Start/Pause, Convert all styles gated on the player's granny2.dll, When to convert and the last
  error; Status; Shaders); the performance shortcuts are gone. Hotkeys page lists every binding; Advanced is hidden in
  release; Help & support drops its Preparation copy and merges compatibility and memory. Badges: Applies now / Applies
  after saving / Restart to apply. `tests/launcher_ui_test.js` covers each; headless Edge sample screenshots at
  1280x900 and 150% (853 CSS px, one Home column below 1000 px) reflow without clipping.
- OPEN: native report preview has no issue field or inclusion checkboxes. It collects the default logs, settings and graphics details, then previews frozen files before saving. WebView provides the issue field and three inclusion checkboxes; parity is not claimed.
- OPEN: WebView screenshot capture produced no artifact in this environment; DOM checks pass. Interactive WebView2 zoom at 100/150/200%, multi-monitor DPI, high contrast, Narrator, native/WebView Apply/restart, action dialogs and keyboard/focus acceptance. Offline checks do not certify these.

Final release controls, in display order (each has details/requirements and apply information):

| Page | Visible settings |
|---|---|
| Home (WebView) | Preset; Play; RTX Hair (its Lighting control, in-game key and Change); style conversion: Start/Pause/Resume/Try again, Convert (Styles seen in game / All styles), Choose/Change/Clear granny2.dll, When to convert; Status (Choose game folder…, Recheck, Save); Shaders |
| Lighting | Ray-tracing detail; DLSS Ray Reconstruction; Sun shadows; Indirect lighting; Off-screen bounce light; Local light shadows; Interior light shadows; Hair shadows; Mod lighting strength |
| Surfaces | Reflections; Water lighting; Depth of field and bloom smoothing |
| Performance | Frame Generation; Rendered frame limit; NVIDIA Reflex |
| Hotkeys (WebView) | All ray tracing off/on; DLSS 5 off/on; RTX Hair off/on (when the binding exists); each with Reset to its default and None |
| Help & support | Report a problem; Choose game folder…; Recheck graphics; GPU (multiple GPUs only); Fonts and third-party notices; compatibility, memory, comparison and overlay guidance |
| Overview (native setup) | Game folder; Choose folder…; Recheck graphics; Continue / Finish setup |

Global actions: Low, Medium, High, Ultra; Custom indicator; Discard changes, Save, Play. Native also has Use suggested preset, Restore preset settings and Close. Play saves settings first in both renderers.
WebView report: What happened? (optional); Logs, Graphics settings, Graphics hardware; Preview files / Refresh preview; Cancel; Close; Save report…. Native report: file list, file contents, Cancel / Close and Save report…. No report uploads automatically.
Preset/dependency review: Apply changes / Cancel. Native dependency policy: Enable requirements / Adjust options / Keep editing. Closing with edits: Save / Discard changes / Keep editing.
