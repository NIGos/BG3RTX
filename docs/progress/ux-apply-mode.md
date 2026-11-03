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

## Settings decisions (branch ux-apply-mode, kept, 2026-11-03)
- Merged three Advanced rows into Off-screen bounce light: Off / Near / Far (6 m) / Far + reflections.
- Ultra keeps its existing bounce/emission/far-field keys as Far. Reflection level stays experimental.
- Choice::mark maps materials-only/emission/bounce to Near, far-field to Far, reflection keys to top.
- Old all-hit emission remains custom and survives saving; lower levels reset the entire chain.
- Sun shadows gains Ray-traced + hidden roofs: cascade-copy casters for hidden roofs/canopies.
- Ultra uses that third level; others keep their previous sun behaviour. All sun levels apply Live.
- Reverted box shadows on 2026-11-03: ls_box=0 in every preset, including Ultra; visible in Experimental.
- Existing measured local-light cost: +0.32 ms S6 / +0.20 ms S7. No new GPU measurement needed.
- Grymforge may lose artist-unshadowed lights with box shadows; investigation pending before promotion.
- Hair origins, OMM, cutout spill and Local light shadows Full + shadowed lights are dev-only.
- Both launchers consume the consolidated shared cards/modes; WebView sample cards updated too.
- Defaults regenerated (no value delta), schema regenerated; USER_SETTINGS and local-light TRACK updated.
- Main merge reconciliation keeps cascade deduplication and the static-only fallback: developer-only,
  both choices Live, requiring Sun shadows / Ray-traced + hidden roofs. Defaults/schema regenerated.
- Fixed the stale native_hair_origin_test Apply::Partial assertion to Restart, with Live Off / Restart On.
- Validation: 102 C++ targets built, shader fixture excluded; selected 110/110 CTests passed (152.96 s).
- Settings: 2636 checks, 0 failed; generated defaults --check and git diff --check pass.
- Shader requests delta 0; ABI unchanged. No game, shader tools, commits or git metadata mutations.
