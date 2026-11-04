# Release launcher QA - 2026-11-04

- PASS: release navigation is Lighting, Surfaces, Performance and Help & support. Native fallback also retains its setup Overview. Advanced and Experimental require dev=1.
- PASS: exactly 15 release settings; bridge rendering and direct requests enforce knob_visible and choice_visible. Exact labels, choices, slider bounds and steps match CONTROL_CONTRACT.md.
- PASS: Low, Medium, High and Ultra remain visible globally in both renderers; Custom counts visible deviations, including preserved custom values. No GPU-model guarantee.
- PASS: native fallback separates Lighting and Surfaces, uses the same central metadata and apply transitions, and shows prerequisites. Hidden release controls remain unavailable.
- PASS: saved/pending restart state, dependency confirmation, failed-save edits, keyboard focus and card/report disclosure state retain regression coverage. Native redraws restore focus and scroll.
- PASS: Help & support includes compatibility, memory, game-folder selection, graphics recheck, adapter selection when needed, overlay troubleshooting and a local report preview/export. Nothing uploads automatically.
- PASS: Ctrl+Shift+F9 copy describes comparison with the game's own image. Release save copy has no F10 or Experimental instructions.
- PASS: sample cards match all 15 release controls. Unmeasured sample costs are explicit; actual cards use central measurements and provenance. No new rendering-cost claim.
- PASS: bundled Lato/system fallback, original branding, flat text panels, wrapping controls, forced-color focus and reduced motion remain. No Larian/WotC artwork or fonts introduced.
- PASS: vcvars64 located through vswhere; launcher, bridge test and settings test build. launcher_web_bridge_test: 977 checks; settings_test: 4,422 checks. launcher_web_bridge_json and launcher_ui_test pass; requested CTest group 4/4.
- PASS: offline tests use injected effects; no game launch, player settings write, common/settings.h edit or Git metadata write. Shader request delta 0.
- OPEN: actual WebView2 100/150/200% zoom, multi-monitor DPI, Windows high contrast, Narrator and interactive native/WebView Apply/restart and focus acceptance. Offline DOM/source tests do not certify these.

Release controls (details/requirements and apply badges accompany each setting):

| Page | Visible settings |
|---|---|
| Lighting | Ray Reconstruction; Ray-traced geometry; Sun shadows; Global illumination; Local light shadows; RT lighting strength; Off-screen bounce light; Box light shadows; Temporal depth of field and bloom; Hair shadows and occlusion |
| Surfaces | Reflections; Water lighting |
| Performance | Frame generation; Rendered frame limit; NVIDIA Reflex |
| Help & support | Report a problem; Choose game folder; Recheck graphics; Graphics adapter (multiple adapters only); compatibility, memory, overlay and comparison guidance |

Global actions: four presets, Custom indicator, Report a problem (WebView footer), Discard, Save and Play. Native additionally has Use recommended, Reset entire preset and Close; setup Overview retains the game-folder field, Browse and Continue/Finish setup. Report dialog: issue text; Logs, Graphics settings and Graphics hardware selections; Preview/Refresh contents; Cancel collection; Close; Create package. Preset/dependency confirmation: Apply changes or Cancel (native equivalents use standard Windows dialogs).
