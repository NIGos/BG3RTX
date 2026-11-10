## hm1 edge bursts were refused: FG inputs hit the edge extent budget before the name filter (branch claude/hair-mv-burst, offline, 2026-11-10)

- hm1 201105 INVALID: every K8 edge burst logged "[pan] edge capture REFUSED: incompatible mode or extent budget"
  (3 per frame, 4668 lines) and no "burst 9940x complete". Cause: with pan_inputs=1 the layer also offers the
  display-size FG inputs (fg_hudless, fg_ui, fg_final at 3840x1600); layer/dump.cpp checked the 2560 px budget before
  the edge-mode name filter, so these images invalidated the burst although it never keeps them.
- Fix (no key, diagnostics path only): `EdgePanSamples::kept` (common/pan_epoch.h) lists what an edge burst keeps
  (depth, final, material, albedo; rr_post_ac/eb with pan_post; rr_mvec with pan_mvec); pan_capture skips everything
  else before the budget check. pan_epoch_test: FG inputs and rr_color not kept, rr_mvec only with pan_mvec, every
  wanted image kept. Shaders untouched.
- The hm1 counter line answered the other question: 780 of 780 keyed rows with previous points per 60 frames and every
  publish with the previous camera, so missing previous points are disproved; rt_hair_mv_card=1 is useless, =2 is the
  test (docs/RTX_HAIR_PRIMARY.md 5y).
- hm1 unchanged (4 edge bursts, about 0.4 GB; live_flow --frozen 0 failing). Rerun it with a tree that has this fix.
