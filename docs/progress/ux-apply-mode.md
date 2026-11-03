## Launcher apply modes (branch ux-apply-mode, offline verified; capture QA partial, 2026-11-03)
- Explicit Live / Hitch / Restart metadata covers 27 controls and all 69 choices, including developer choices.
- Missing modes fail settings_test. The generated JSON schema includes every choice mode.
- Audited runtime readers: core/gi.cpp, ngx.cpp, streamline.cpp, framecap.cpp, geocapture.cpp,
  hit_material.cpp, alphatest.cpp, tracker.cpp, layer.cpp, water.cpp and startup resource setup.
- Cards declare the most demanding transition; Details explains mixed controls. Water/hair-origin Off,
  and disabling optional core variants, have explicit Live choice metadata.
- Hair-origin first enable and BVH split now participate in startup-change detection.
- Both launchers always show Live / Applies after a short hitch / Restart to apply badges.
- A saved pending warning says Restart BG3 to apply N changes, counting controls rather than bundled keys.
- Session baselines survive repeated saves and reopening. Saved reverts/new sessions clear the warning;
  draft edits, failed saves and transient status messages do not clear it.
- Validation: launcher C++ build and five relevant CTests pass. Settings: 2636 checks; UI state: 23;
  bridge: 73. JavaScript syntax and generated-default checks pass. Shader request delta: 0; ABI unchanged.
- Native screenshots regenerated and visually checked under captures/gui-product-review/apply-mode/.
  The BMP extension reflects the native capture format. Review frames are synthetic; no game was launched.
- BLOCKED: WebView2 screenshot navigation aborts (WEB_ERROR_STATUS_CONNECTION_ABORTED = 9).
  Isolated Chromium headless capture also exits -2147483645; browser access host is unavailable.
  Error sidecars and capture instructions are retained. WebView screenshot QA remains outstanding.
- No game launch, DXC, shader tools, commits, merges or git metadata writes. No GPU pass changed.
