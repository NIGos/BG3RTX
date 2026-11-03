## Hair reload fail-safe (branch claude/hair-reload-fix, 2026-11-03)
- hv1 18:40 device lost after the F8 reload with strand variants on: native hair FS module map was never pruned on
  vkDestroyShaderModule (a reused handle could give a non-hair pipeline a strand variant); now erased on destroy and
  on reuse. Level loads (new G-buffer depth image) reset the groom gate, pixel record and feedback and pause swaps
  300 frames. Regression test in hair_groom_asset_test (reload sequence). Build, 143/143 CTests. Not run live.
- Join caches: bvhsplit drops groom join verdicts, UV joins and UV readbacks in flight at the same level-load
  boundary (identities built from Vulkan handles a new level reuses). Checked, no change: held hair bounds read
  from the mesh pool at stride 88; groom points come from this frame's soup; held groom update guarded (e43731cc).
- Live jobs: hair_validation hv5-reload-bisect (Test Dialogue1, one launch, visible_unfocused_fg, F8 per stage:
  native + scalp, + physical origin, + groom, + primary, 3 x 20 s survival windows with frames gated in each);
  hv4 adds the reload under GPU-AV. live_flow 5 jobs 0 failing; dryrun passes (31.3 min, 1.49 GB; budget 40 min).
  Build, 143/143 CTests. Not run live.
