## Hair reload fail-safe (branch claude/hair-reload-fix, 2026-11-03)
- hv1 18:40 device lost after the F8 reload with strand variants on: native hair FS module map was never pruned on
  vkDestroyShaderModule (a reused handle could give a non-hair pipeline a strand variant); now erased on destroy and
  on reuse. Level loads (new G-buffer depth image) reset the groom gate, pixel record and feedback and pause swaps
  300 frames. Regression test in hair_groom_asset_test (reload sequence). Build, 143/143 CTests. Not run live.
