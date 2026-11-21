## r2-inn-em-pack readiness fix (branch claude/em-pack-2, READY_FOR_LIVE, 2026-11-21)

- Run 065644 failed fast in producer-sanity-warmup: its check was the one-shot "hit emission: first hit-GI dispatch with
  the emissive scene" line, logged at load before the first D-EM build (that run logged "without"). The emission variant
  ran from 06:59:10 (raygen build ~80 s after load).
- Job: new em-ready phase before the pack, waiting (180 s) for "the hit-GI trace runs as a ray tracing pipeline, emission
  variant" and an "[em] last N builds ... emissive > 0" line; both in expect. live_flow 0 failing with and without --frozen.
- Core: replay_pack_pass=2 now waits while the dispatch has no emissive scene instead of refusing (it refused for good
  if armed before the first D-EM build). Tests: build.ps1 -Test 240/240.
