## Holes when a groom joins while others swap: nearest-strand trace (branch claude/hair-v3-settle, offline, 2026-11-10)

- hs1 125217: not v3 and not the warm-up test. The trips at 2171-2176 start with the session's first joins of three
  more hair draws (45252, 100101, 7092) after the warm-up ended; existing keys lose their strands. 103633 had the same
  arrivals inside its warm-up window (0 trips). Same mechanism as the "groom joins mid-play" item.
- GPU fixture (hair_primary_gpu_test nearest_check, twin groom, card-kept copy 3 cm in front): the strand G-buffer's
  restart loop (kHpSkips 3, restart 2 cm past the other strand) leaves 2567 of 25478 strand pixels empty and returns a
  farther strand on 11154 more.
- Fix `rt_hair_groom_nearest` (off; specialization 1047, header flag 64 live off-switch): one non-opaque query that
  passes over other rows' candidates and commits accepted ones (shaders/rt/hair_primary_nearest.hlsli). Fixture: 25478
  of 25478 exact, 0 holes, 0 card-kept hits. Key off: 223 identical + G-buffer equivalent with 1047 frozen; variants 0.
- hs1 staged job: key on for the whole run (pipeline-creation key). Predicted: 0 frame watchdog trips in the arrival
  period; GPU cost from the strand G-buffer line (0.133 ms/frame before). Details: docs/RTX_HAIR_PRIMARY.md 5u.
