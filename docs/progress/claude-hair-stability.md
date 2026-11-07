## Strand flicker and lag: coverage hold, this frame's publish (branch claude/hair-stability, READY_FOR_LIVE, 2026-11-07)

- Flicker (2026-11-07 hair-flicker): draws near the coverage bound (9861, about 430 card pixels, 0.833..0.925 per
  window) swapped 0..300 frames per window; refusals `coverage low` 521-896 and `coverage stale` 19-300 per window.
  The strict bound's noise term is the offline footprint's (0.019); at 430 pixels the same formula gives 0.088.
  Empty measurements (0 card pixels) replaced a good ratio and read as stale.
- Key `rt_hair_primary_hold` (off): a swapping key keeps strands while its newest non-empty measurement is inside
  [0.9105 - sigma, 1.0753 + sigma] with sigma = (sqrt(S) + r sqrt(C)) / C of that measurement, and at most
  kHoldFrames = 12 + 4 x 4 = 28 frames old (freshness plus one full count readback ring). An outside measurement,
  an invalidation or an older one returns it to cards; a new swap keeps the strict bound. Strike, orphan and device
  fail-safes unchanged. Test: hair_groom_asset_test test_coverage_hold (strict flickers, hold 60/60 on the same
  measurements; 0.60 drops it; invalidation and age drop it).
- Lag: the strand G-buffer is recorded before the frame's scene build, so it applied the previous frame's publish
  (rebased for the camera only): strands trailed the animated cards by one frame and their velocity carried the
  previous frame's motion. Key `rt_hair_groom_primary_current` (off, needs rt_hair_groom_primary_order=1): the
  G-buffer copies its header from a host slot written at recording with that safe publish and upgraded at
  vkQueueSubmit to this frame's when its build is submitted earlier or earlier in the same submit, with the same
  grooms in the same draw slots. Count tag, GbFrame tag and the repeated-tag check follow the final choice.
  Tests: test_current_pick (submit orders, groom/slot changes), hair_groom_test primary twin (previous publish:
  > 1 px behind an accelerating card and a wrong MV; this frame's: card position and MV exactly).
- Temporal reconstruction (item 3, no change): the strand rays use the inverse jittered view-projection at the
  fragment's raster position (hair_primary_gbuffer.hlsl:129-135), the cards' own sample. Tips use the native TAA
  alpha-test dither (twin tips_keep). Scratch temporal model (Halton 18 phases, 72 frames, per-pixel area
  reference): single ray residual 0.056, EMA flicker 0.043; M3.2 pair with hashed pick 0.091 / 0.052; a parity pick
  0.115 / 0.041. The CTest gate judges the pair on a 3 x 3 box instead, where it wins. Live A/B of
  rt_hair_groom_coverage with the two keys above on decides; no new pick adopted.
- Period line `[hair primary] frame N:` adds `coverage hold on/off, swap frames only the hold allowed n, strand swaps
  resumed after card frames n; G-buffer headers at submit: this frame's publish n, record-time because not
  recorded n, build after the G-buffer n, different grooms n`.
- Next live (Test Dialogue1 close-up, rt_hair_groom_primary_order=1): toggle rt_hair_primary_hold 0/1 and
  rt_hair_groom_primary_current 0/1 in one run. Expect with hold: `resumed after card frames` near 0, swapped frames
  per draw 300 for in-bound draws, `coverage stale` refusals gone for empty measurements. With current: `this
  frame's publish` close to 300 per window (the adjacent-tag counted frames stay: the count still reads the previous
  G-buffer's pixels); strands no longer trail
  the head. Cost: one 3.5 KB host write per G-buffer submit; no GPU pass added.
