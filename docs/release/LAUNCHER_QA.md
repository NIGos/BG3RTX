# Release launcher QA - 2026-11-04

- PASS (offline): 15 release controls, shared metadata, exact choices/ranges and release visibility match CONTROL_CONTRACT.md. Preset values, keys, dependencies and apply transitions are unchanged.
- PASS (offline): both renderers use the Lighting order below; depth of field and bloom smoothing is on Surfaces. One compact header shows version, preset, GPU and mod-file presence. No total-cost tile or unknown-cost/memory badges.
- PASS (offline): measured costs appear after descriptions; estimates and separate hair-pass timings are in details. No aggregate rendering-cost or frame-rate claim.
- PASS (offline): Play saves first. Injected callbacks verify save-before-launch, failed-save edit retention, dependency-review cancellation and launch after confirmation. The game was never launched.
- PASS (offline): dependency/close dialogs name actions; restart tracking, settings metadata, DOM focus/disclosure retention, navigation and report privacy/export retain regression coverage.
- PASS (offline): Help & support owns game-folder selection, graphics recheck, GPU selection when needed, compatibility, memory, overlay/comparison guidance, an actionable notices link and one report entry. Ctrl+Shift+F9 help is here.
- PASS (offline): bundled Lato/system fallback, original branding, wrapping controls, forced-color focus and reduced motion remain.
- PASS: MSVC environment located with vswhere/vcvars; launcher and requested test targets build. Targeted CTests: 6/6 pass; settings 4,422 checks, bridge 987, diagnostics 59 and UI state 33. No Git metadata writes or rendering/shader changes.
- OPEN: native report preview has no issue field or inclusion checkboxes. It collects the default logs, settings and graphics details, then previews frozen files before saving. WebView provides the issue field and three inclusion checkboxes; parity is not claimed.
- OPEN: WebView screenshot capture produced no artifact in this environment; DOM checks pass. Interactive WebView2 zoom at 100/150/200%, multi-monitor DPI, high contrast, Narrator, native/WebView Apply/restart, action dialogs and keyboard/focus acceptance. Offline checks do not certify these.

Final release controls, in display order (each has details/requirements and apply information):

| Page | Visible settings |
|---|---|
| Lighting | Ray-tracing detail; DLSS Ray Reconstruction; Sun shadows; Indirect lighting; Off-screen bounce light; Local light shadows; Interior light shadows; Hair shadows; Mod lighting strength |
| Surfaces | Reflections; Water lighting; Depth of field and bloom smoothing |
| Performance | Frame Generation; Rendered frame limit; NVIDIA Reflex |
| Help & support | Report a problem; Choose game folder…; Recheck graphics; GPU (multiple GPUs only); Fonts and third-party notices; compatibility, memory, comparison and overlay guidance |
| Overview (native setup) | Game folder; Choose folder…; Recheck graphics; Continue / Finish setup |

Global actions: Low, Medium, High, Ultra; Custom indicator; Discard changes, Save, Play. Native also has Use suggested preset, Restore preset settings and Close. Play saves settings first in both renderers.
WebView report: What happened? (optional); Logs, Graphics settings, Graphics hardware; Preview files / Refresh preview; Cancel; Close; Save report…. Native report: file list, file contents, Cancel / Close and Save report…. No report uploads automatically.
Preset/dependency review: Apply changes / Cancel. Native dependency policy: Enable requirements / Adjust options / Keep editing. Closing with edits: Save / Discard changes / Keep editing.
