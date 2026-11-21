## Ultra temporal post without jitter terms: rr_post_jitter_align=3 (branch claude/r5-ultra-shift, READY_FOR_LIVE, 2026-11-21)

- Question (from r3-d3-jitter, claude-r3-core-live.md): the temporal path reprojects its history by previous - current
  jitter and reads it at uv + jitter (`layer/rrpost.cpp` rrpost_temporal / rrpost_final). On High the same +jitter lookup
  made the blurred background flicker 2-4x, because the game's DoF layers do not carry the jitter there. The code cannot
  settle it: the terms are self-consistent only for jittered A/C/B (1D model in tests/rr_post_temporal_test.cpp: each
  convention is stationary for its own content and moves for the other).
- Switch (layer only, no shader edit): `rr_post_jitter_align=3` sets both temporal terms to 0 (history by the motion alone,
  output at the plain uv; the real jitter is still recorded). Without the temporal pass 3 is the plain final, like 0.
  Developer choice "No jitter shift (check)". Key 0: the same float arithmetic (common/rr_post_temporal.h helpers).
  No ABI change, variants.txt delta 0, cost 0 ms.
- Staged, not queued: `tools/live_batches_staged/r5_ultra_shift` r5-d3-ultra-shift (Dialogue3, Ultra, 0/3/0, K8 bursts,
  presented K8, float energy), `predicted.json`, `analyze_jitter.py`. Lint 0 failing with and without --frozen.
