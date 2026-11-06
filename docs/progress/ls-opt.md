## Local-shadow replay feasibility (branch ls-opt, stopped at analysis, 2026-11-05)

- Audited all 5 `test-main/captures/replay-*` manifests: 20261101-142819-1,
  20261102-013032-1, 013545-2, 013914-3 and 035919-1.
- Verdict: these packs cannot drive the production Ultra box-k local-shadow pass.
  No candidate implemented; no `ls_fast` key or specialization ID allocated.
- Present: camera, full-resolution depth/normal/origin, point/spot flags, scale,
  froxel headers/index list/light array/constants/cam2 (set 0 b21-26, b29),
  joined light slots (b46), material/alpha geometry and TLAS reconstruction data.
  Albedo/material can be remapped from pack set 2 b2/b3 to LS set 0 b3/b4.
- Missing in every pack: LS set 0 b27 shadow-view records (game set 1 b40)
  and b28 shadow atlas (game set 1 b41). `bg3_lights.hlsli:192-242` uses
  both to reproduce the game's visibility term; zero dummies change that term.
- Missing: game set 1 b29 projector records, bound at LS set 3 b29, and
  bindless 2D cookie images at LS set 3 b45. Pack set 2 b45 contains only
  cube views (0 captured 2D cookies in every manifest); these cannot substitute.
  See `bg3_boxes.hlsli:25-27`; the cookie sampler alone is insufficient.
- Missing: initial R11G11B10 diffuse/specular (LS set 0 b6/b7), which
  `rt_local_shadows.hlsl:347-385` reads for AO give-back, addition and clamping.
  `prevRadiance` is not these two separate pre-LS buffers.
- Missing: sun mask b16 and auxiliary image b44, used conditionally by the
  production AO/sun tail; b44.zw is also written. Pack AO at set 2 b6 is
  captured before the RT AO fold, not a captured LS-time b12 input.
- Capture supplies GI push constants and GI outputs only (core/gi.cpp:2639-2662).
  LS additionally selects `ls_has_mask`, join-success flags and exact-k mode
  immediately before dispatch (core/gi.cpp:3315-3370); no LS output reference
  or LS counter reference is captured. Joined slots themselves are available.
- Required for faithful replay: an LS-time input snapshot including the missing
  resources, its actual push/constants and specialization values, plus output
  references. Restore both colour inputs, auxiliary state and counters before
  each paired dispatch, outside timing. Current packs cannot recover these bytes.
- Existing `--compile-only --compile-pass local-shadows` creates a pipeline only;
  its 168-register audit establishes no output equivalence or timing gain.
- Stopped per feasibility gate. No GPU, compiler, build, game, queue or git writes.
  No shader/core changes; request delta 0; release code unchanged. Build/tests
  and release-equivalence compilation not run for this analysis-only outcome.
