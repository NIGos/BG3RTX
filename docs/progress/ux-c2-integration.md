## WebView2 launcher (ux-c2-integration, offline candidate, 2026-11-02)

- Applied the approved original ornament assets: filigree frame/card corners,
  gem dividers/toggles, stone/panel textures, sidebar glyphs, engraved ember Play
  and decorative lantern hero. Bundled Lato and system serif fallbacks remain.
- Copy is factual. Hero shows resource version, selected preset (marks unsaved
  changes), detected GPU/VRAM and mod file status; no slogans or taglines.
  Version resource and bridge share version.h. File presence is not a runtime check.
- A1 costs/provenance remain authoritative. Bridge omits unknown costs; UI hides
  absent cost/VRAM pills. Far-field estimate +3.2-3.7 ms remains with RTX 5090,
  scene, output, date and 6 m provenance. Summary says Not measured yet once;
  hardware capacity is labelled separately from mod usage. No synthetic totals.
- Existing local virtual host and CMake copy serve assets/branding/ornaments.
  package.ps1 explicitly inventories all 12 files, including provenance README;
  Inno requires each at compile time. No downloaded fonts or franchise artwork.
- Bridge still owns dependencies, presets, dirty/discard, compatibility and Play.
  B2 report keeps privacy, exact preview, progress/cancel and local ZIP workflow.
  WebView restrictions and native A2 fallback remain in place.
- Validation: launcher and CPU fixtures built. All 7 scoped CTests passed:
  settings, UI state, bridge, JSON, diagnostic fixtures and two package suites.
  JSON checks include version, unchecked status, unknown-cost omission and A1
  provenance. Package fixture verifies every ornament. JS syntax and diff checks pass.
- Regenerated and visually reviewed embedded sample PNGs in
  captures/gui-product-review/webview-styled/: normal, experimental, report,
  narrow-720. Sample mode never reads player configuration or probes hardware;
  player configuration hashes unchanged. Play is disabled in sample captures.
- Restricted capture uses process-only WEBVIEW2_ADDITIONAL_BROWSER_ARGUMENTS
  recorded in verification.json; no sandbox override ships. Default capture here
  failed navigation (HRESULT 9); the documented review arguments resolved it.
- Remaining release QA: keyboard/Narrator, real high contrast/per-monitor DPI,
  normal desktop runtime and installer lifecycle. No installer was run. Full
  package dry-run remains blocked by pre-existing DXC/Vulkan-Headers notices.
- No game, installer, saves, DXC/shader tools or Git metadata writes. No commit.
  Shader request delta 0; no renderer/ABI or GPU-pass cost change.
