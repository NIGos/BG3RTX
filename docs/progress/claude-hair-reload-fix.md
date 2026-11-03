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
- Review fixes (main f5b339eb findings): groom_make_room keys residents by an index into (id, LOD) refs (id * 4 + LOD
  lost the top bits of 64-bit hash ids: empty asset, out-of-range LOD on eviction). LOD bounds read from a 4-slot
  readback ring, newest slot of a GPU-completed frame (was the slot this frame's copy may still write). A failed groom
  TLAS build clears kInfoGroomReplaced on its cards (they had lost occlusion and shadow). Strand G-buffer hit path
  checks k < grooms like the miss path. hair_fibre removes the native sun term only for a known visibility (kept on
  kRtUnknown, as local lights). Shader identity: 258/262 byte-identical; only hair_fibre (3) and
  hair_primary_gbuffer differ (key-on shaders). Open: strand ownership per card draw (overlapping grooms) needs a
  per-draw channel to the fragment shader; not a small fix.
- Strand ownership per card (overlapping grooms): the G-buffer and velocity variants keep a hit only when it lies in
  the band of the fragment's own card along its ray (hp_on_card: (10 mm radius bound + segment length / 2) / |cos| +
  pixel spread; strand points lie on their card triangles). The pixel cache is unchanged (closest strand of all
  grooms); the owning card's fragment writes it. Pixel entries sealed (word 3 = hp_seal(tag, words 0..2)): a torn
  entry never pairs this frame's tag with an older key. Python twin + two-groom overlap fixture in hair_groom_test.
  Identity: 256/262 byte-identical; only the key-on hair_fibre (3), hair_primary_gbuffer, _velocity, _count differ.
  Cost: one 4x4 inverse per strand fragment (was per pixel miss); not measured live.
- Device lost 2026-11-03 19:53:28 (user, strand hair turned on live, main 71f92bd3): the faulting read 0x23c4e00000 is
  mesh pool [0x22cae00000, +2 GB) + 0xFA000000, inside the 4 GB a 32-bit offset reaches from the pool's BDA; the
  registry named it "old soup" only because the retired 2047 MB soup (elastic shrink 19:53:06) occupied that VA and
  was never untracked. Readers: the strand G-buffer, velocity and fibre passes load pool + row offset + i0 * 12 with
  i0 read from the pool (32-bit, unchecked). Fix: header word 14 = pool size; hp_segment checks every pool byte a
  reader touches for the segment (64-bit offsets) and fails closed; the retired soup leaves the fault registry.
  Python twin + the crash's numbers in hair_groom_test. Identity 257/262 (only key-on shaders). Not run live; the
  source of the bad i0 is not identified (the bound makes it harmless).
