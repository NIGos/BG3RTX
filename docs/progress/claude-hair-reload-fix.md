## Hair reload fail-safe (branch claude/hair-reload-fix, 2026-11-03, after bundle7)
- Device lost 2026-11-03 19:53:28 (user, strand hair turned on live, main 71f92bd3): the faulting read 0x23c4e00000 is
  mesh pool [0x22cae00000, +2 GB) + 0xFA000000, inside the 4 GB a 32-bit offset reaches from the pool's BDA; the
  registry named it "old soup" only because the retired 2047 MB soup (elastic shrink 19:53:06) occupied that VA and
  was never untracked. Readers: the strand G-buffer, velocity and fibre passes load pool + row offset + i0 * 12 with
  i0 read from the pool (32-bit, unchecked). Fix: header word 14 = pool size; hp_segment checks every pool byte a
  reader touches for the segment (64-bit offsets) and fails closed; the retired soup leaves the fault registry.
  Python twin + the crash's numbers in hair_groom_test. Identity 257/262 (only key-on shaders). Not run live; the
  source of the bad i0 is not identified (the bound makes it harmless).
