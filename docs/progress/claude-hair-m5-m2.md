## Hair M5 participation, M3.4 post depth, M2.2 cascade cards (branch claude/hair-m5-m2, READY_FOR_LIVE, 2026-11-07)
- Found in the joined run (.scratch/coord/hair-joined-20261107.log): 0 prepass/G-buffer/velocity strand draws in
  every period, "header order prepass": with rt_hair_groom_primary_order=0 no pass swaps (hair_primary_swap), so the
  strands the user saw were RT-only (shadow/GI). On-screen strands need rt_hair_groom_primary_order=1.
- M5: `hair_primary::Participation` counts every hair pipeline draw (static and instanced FS 0ca5ee93) by pass and
  view in the period line ("hair draws strand/total ...; card-only pass groups k"). Portrait/instanced variants not
  built: one header and pixel record serve the main view; a portrait draw would seal main-view entries, and the
  groom follows the main view's character, not a portrait instance (RTX_HAIR_PRIMARY 5f).
- M3.4: every post consumer reads the D24S8 after the G-buffer; CPU proof (`primary.post_depth`, test_post_depth):
  0 halo pixels of 1576 card pixels, error within one D24 step; control with card depth kept fails all. No fix.
- M2.2 `rt_hair_groom_cascade_drop` (off, live, no spec id): in sun cascade passes, static hair card draws of
  published grooms use a depth-write-free clone (XFB unchanged). Trade-off: no ribbon/double shadows; no hair shadow
  where receivers fall back to the game mask. Counter "sun cascade hair card draws T, without card depth D".
- Tests: hair_groom_asset_test (participation, cascade drop), hair_groom_test (post depth). Shader identity
  221/221 byte-identical to main, request delta 0. Full CTest numbers in the last commit.
- Live (one strands-on run, order=1): period line shows other views / instanced counts and D = T with the key on;
  neck/shoulder card silhouettes gone in Test Dialogue1 with the key on; DoF halo check in the L2 PNG pairs.
