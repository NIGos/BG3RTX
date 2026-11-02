## WebView2 release launcher (branch ux-c2-integration, offline candidate, 2026-11-02)

- Embedded WebView2 with static loader; SDK referenced by BG3RTX_WEBVIEW2_SDK.
  Missing SDK fails configure clearly. Missing runtime/UI falls back to native A2.
- Bundled local UI follows the approved mockup: charcoal, gold, serif headings,
  preset pills, sidebar, summary, feature cards and pinned actions. No network fonts.
  Lato is bundled with OFL; Georgia is the system serif fallback (no OFL display serif).
- Virtual hosts map only UI/fonts/branding. Navigation, popups, browser shortcuts,
  permissions, devtools and context menus are restricted; background networking disabled.
  Normal WebView profile: %LOCALAPPDATA%/BG3RTX/WebView2.
- C++ bridge owns model policy, hidden developer/custom values, dependencies,
  preset/save confirmation, dirty/discard, compatibility, restart and Play intents.
  Adapter selection/recheck and game-folder chooser stay in C++.
- Report / --report use B2: privacy, selected groups, exact sanitized preview,
  local ZIP chooser, worker progress/cancel and retained preview on failure.
  Native runtime fallback also has a scrolling preview and worker collector.
- B1 icon/version resources wired. Portable/setup include UI, fonts and branding;
  static loader licence/notice included. package.ps1 -DryRun performs no build/write.
- Validation: launcher and CPU fixtures built; 8/8 scoped CTests passed (shader
  fixtures excluded). Bridge 62/62 checks, UI state 15/15; independent JSON parse.
  Three embedded captures and 3/3 mixed-mode guards passed; config hashes unchanged.
  Screenshots: captures/gui-product-review/webview/ (normal, Experimental, missing,
  probe, long error, report, 720 px and embedded WebView sample).
- --no-game --screenshot <PNG> uses sample data, no player configuration/probe.
  Screenshot profile is isolated beside the executable. Restricted environment capture
  needed test-only WEBVIEW2_ADDITIONAL_BROWSER_ARGUMENTS; no sandbox override is shipped.
- Remaining release QA: keyboard/Narrator, real high contrast/per-monitor DPI,
  normal desktop runtime and installer lifecycle. Package dry-run remains blocked
  by the pre-existing missing DXC and Vulkan-Headers notices.
- No game, installer, saves, DXC/shader tools or Git metadata writes. No commit.
  Shader request delta 0; no renderer/ABI change or GPU-pass cost change.
