## Strand swaps never started: prepass/G-buffer card-depth deadlock (branch claude/hair-swap-fix, READY_FOR_LIVE, 2026-11-07)

- Symptom (user run 2026-11-07 19:08-19:12, Test Dialogue1, primary_order 1): grooms join (matched 1297), but every
  period line has 0 prepass, 0 G-buffer, 0 velocity strand draws. A close-up draw (483b, LOD0, 75k card pixels) held
  coverage 0.918 inside [0.891, 1.095] for 600 frames, probed in every window, and still 0 swapped frames.
- Cause: a circular gate. The G-buffer swaps only after this key's prepass dropped its card depth
  (`!g_depth.dropped(key, now)`, layer/hair_primary.cpp, added by 5f3b8a88 on 2026-11-04 19:01, after the hv9
  18:02 run that drew strands). The prepass drops only after a G-buffer strand draw (`SwapGate::allow_prepass` needs
  `gbuffer_prev`, e43731cc). With every other gate passing, neither can go first.
- Second limit, not changed: most draws in this scene are small (100-1800 card pixels) with ratios 0.60-0.89, below
  kCoverMin. Its noise term is derived at the offline footprint (about 9400 samples); at 800 pixels the same formula
  gives sigma 0.06, not 0.019. That is a decision for the hair owner and is not part of this fix.
- Fix: SwapGate `ready_now/ready_prev`. A G-buffer draw that passes every gate except the card-depth drop (and has its
  strand variant) marks the key ready; the next prepass may then drop. If that G-buffer still refuses, its cards take
  the existing native-depth fallback. Velocity still needs a real strand draw. No shader, key or ABI change.
- Diagnostic: the `[hair primary] frame N: ...` period line ends with `swap refusals (main-view hair draws kept as
  cards, first refusing gate): G-buffer: coverage low n, card depth not dropped n, ...; prepass: ...`.
- Tests: hair_groom_asset_test `test_prepass_gbuffer_bootstrap` (0 swaps without readiness, first swap within 3
  frames and 297 of 300 with it), `test_refusal_tally` (coverage reasons, tally format).
- Next live (same save and keys): prepass and G-buffer strand draws > 0 for draws inside the coverage bounds (the
  close-up 483b), `swapped frames` > 0 in the coverage line, `prepass card depth dropped without its G-buffer swap`
  small, depth orphans none; the refusal tally names the rest (expected mostly `coverage low` for small draws).
