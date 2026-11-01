## M5.4 water sun shadows: record R = C - D, not D (branch worktree-agent-abd9303a6e22b012e, READY_FOR_LIVE, 2026-11-01)
- Live m5w4 (S8): shadow geometry right, shadowed pool dark with black and bright specks. `clamped 145573`
  is the 120-frame total (1213 per frame = 0.44% of sun records), not half of the records.
- Audit of the three dumps: D is exactly the FS's sun term times dC/dSun (a, T, X in G; no later multiply,
  no exposure or fog mismatch, no ambient in it, disjoint from M5.3's B). Cause: precision. C - D was
  formed from the B10G11R11 target (1 ulp = 2^-6 / 2^-5 of C) and a half D; where the sun is most of C
  (shadowed pool median 66%, the glint up to C ~ 976) that rounding exceeds the rest.
- Fix (key-on only): the FS stores R = C - D (f32, then half) and lum(D) in words 8-9; the compose writes
  V C + (1 - V) R. Key off byte-identical (M5.3 patch FNV pinned, plain compose SPIR-V SHA1 unchanged).
- Test 4j: the FS model through the real encodings; at V = 0 the compose equals the FS with its sun colour
  set to 0 within half rounding; the old formula fails it.
- m5w4 job: expect_every `clamped` < 120 per window in precheck-shadows-on and cost-on (fails on the old run).
- Expected live: occluded ~62.6%, removed ~80% of D and ~46% of mean C (unchanged: the FS's own sun share),
  clamped ~0, no specks. The shadowed half stays darker than the lit half (its water is mostly sun).
