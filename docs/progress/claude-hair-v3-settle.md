## Post-toggle holes with the v3 groom set: cause narrowed, no fix landed (branch claude/hair-v3-settle, offline, 2026-11-10)

- hs1 125217: not v3 and not the warm-up test. The warm-up ended correctly at 2160; the trips at 2171-2176 start with
  the session's first UV joins of three more hair draws (45252, 100101, 7092) and their groom uploads (16fa158c,
  5dbb53eb, b7349689). The existing keys lose their strands, not the newcomers. 103633 had the same arrivals inside its
  warm-up window (0 trips), 095322 50 frames after the switch (trips). This is the "groom joins mid-play" item.
- Likely mechanism: a keyed but not yet swapping newcomer's strands are skipped by every swapped draw's pixel trace at
  most kHpSkips = 3 times, then no strand is stored. Mask-hole pixels 82744 in that period vs 660-4760 elsewhere.
- Not small: needs a strand G-buffer shader change (candidate-filtered trace or a per-row mask bit) plus a GPU fixture.
  Staged plan and predicted rerun numbers in docs/RTX_HAIR_PRIMARY.md 5u. No code change, no test run needed.
