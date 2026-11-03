## Moving dialogue sun band (branch fix-cascade-dialogue, READY_FOR_LIVE, 2026-11-03)
- No commit (git metadata read-only); no game launch, save access or other-branch edits.
- User A/B isolates the moving band to Roof sun shadows; RT sun remains enabled.
- Admission defect: `common/cascade_casters.h:46` hashes placement offsets;
  `layer/geocapture.cpp:1395` requires the full opaque draw key. Equal meshes with
  different UBO offsets survive (mesh dedupe is for discarding draws only).
  `layer/rtscene.cpp:936` adds that captured pose at F-1/F-2. Exact face-hit draw unproven.
- Fix: `rt_cascade_static_only=0`, Advanced / Static roof casters only, all presets off.
  ON keeps recognised affine class-A instances with known non-storage position VBs.
  Rejects skin/cloth/wind, per-draw and unknown paths, even after opaque dedupe misses.
  Filters both standalone and Current Sun's compacted legacy soup; raw Current Sun unchanged.
- Limits: may omit unrecognised static roofs; affine instance transforms or CPU-uploaded
  vertices can still move. No evidence yet for cascade-set hysteresis or a matrix fix.
- `[cascade] casters`: dynamic/unclassified added/excluded per frame, set changes per window.
  Sets ignore order; placement ring offsets can change identities without moving geometry.
- Build passed. Seven targeted CTests passed (cascade, settings, shader variants, native VT
  recipes, replay pack, roof cap, OMM scene binding). Defaults/schema current; diff check clean.
- Native read-only SPIR-V fixtures: static 646a930e41f002bb accepted, skinned
  1cf36b97d3067b6c and wind 50fbfceb06192473 rejected. CPU regression reproduces
  offset-mismatched opaque character admission and checks ON exclusion / OFF preservation.
- ABI unchanged. Shader edits 0; shaders/variants.txt request delta 0; no identity run needed.
- Port proposal for worktree-agent-a0c9be1ab336eae1a's ds3-dialogue3-fg2 (Test Dialogue3):
  retain its nocascade window. Add base-A / static-only-A / base-B / static-only-B / base-C
  in ONE static shot; `rt_shadows=1,rt_cascade_casters=1,rt_sun_cascade_current=0` throughout,
  changing only `rt_cascade_static_only=0/1`. Repeat at camp ds1 (Test Dialogue1).
- After >=2 frames of capture replacement, collect complete 120-frame stats windows;
  job hold >=90, visible_unfocused_fg, patterns + expect and expect_every for every ON window.
  Match dynamic/unclassified added 0.0 and excluded >0 on ON; buffer full and other matrix 0.0.
  Compare face/torso band motion with OFF/OFF repeats and existing nocascade (motion should stop).
  Use separated PNGs for inspection; temporal/energy tolerances come from OFF-repeat noise.
  Keep AO/hair/local/exposure settings equal. Use pre-post float scene means, not PNG energy.
- Prediction: ON removes all dynamic/unclassified legacy additions and their stale-pose band;
  static roofs remain. If the band persists, inspect static membership changes before hysteresis.
- smoke_ultra: qualified Ultra keys + filter ON/OFF, S8 roof/canopy and S7 cut-away interior.
  Require retained static roof triangles, no roof/interior light leaks beyond OFF-repeat noise,
  water composed every frame, no capacity/scene-wait failures, existing sun trust guards intact.
- GPU cost unmeasured (no new pass). Compare captured-pass median, `gpu` RT/GI/water and build
  cost in diagnostic-free windows; no concurrent builds. Pending lives are not a quality pass.
