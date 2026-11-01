## Water compose flicker: keep the lighting's scene pair instead of refusing (branch worktree-agent-afa742daadc3632ff, READY_FOR_LIVE, 2026-11-01)
- Bug (user, main 89dd500, Ultra, all water keys, rt_cascade_casters=1): water reflection and shadows switch
  on/off each frame. Log 20:35-20:41: 211 to 245 composed of 300 frames in the bad windows.
- Cause: "refused: no scene" (51 to 84 per 300 frames), not the lighting age. It tracks the scene-wait
  timeouts (116 and 133 per 600 in the same minutes): the lighting traced the previous build, the late
  build of its frame then committed with an index below the next pass's, and compose_at_pass_end refused
  a newer scene that was not this frame's. Each such pass left the water uncomposed for one frame.
- Frame generation is not a factor: frame indices count the app's presents only (DLSS-G's generated ones
  go through Streamline below the layer; [sl] line: 60 indices, 120 displayed). The 3-index lighting bound
  is now written as 1 x (1 + 2 frames recorded ahead), same value; the old FG derivation was wrong.
- Fix (core only, no ABI change): common/water_compose.h pick_scene. A pass on an earlier lighting whose
  current scene is neither the stash's nor this frame's binds the lighting's own pair, kept alive for this
  frame through rt_omm_scene_reference (stamps that slot; refuses a retired one; no OMM work with rt_omm off,
  which water needs). rt_scene_set is still never called for an older build (it would wait on the game's thread).
- New line "[water] compose stability": frames with a water pass, composed, uncomposed (on/off flips), the
  no-lighting refusals by cause (cleared, old, ahead) and the earlier-lighting cases (current slot stale,
  newer build before this frame, newer pair unavailable, lighting's pair kept).
- Gates: expect_every "uncomposed 0 (on/off flips 0)" in every compose-on window of m5w3..m5w6, m5p1..m5p3
  and su1. Unit tests: pick_scene call order (no rt_scene_set for an older build), lighting_refusal,
  ComposeContinuity flips (water_compose_test).
- Not done: holding the previous frame's delta when even the lighting's slot is retired (expected 0;
  the stability line counts it). Live check: su1 or m5p1 at S8, the stability line in every window,
  and the water should no longer blink while panning. Cost: two host calls per pass, no GPU change.
