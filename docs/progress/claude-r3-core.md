## Post jitter alignment without the temporal pass: rr_post_jitter_align (branch claude/r3-core, READY_FOR_LIVE, 2026-11-21)

- Cause: the plain output grade (`shaders/rr_post.hlsl` `final`, Low-High) read the jittered render-resolution A/C/B at
  the unjittered uv; the jitter reached `final` only when `rr_post_temporal` ran (`layer/rrpost.cpp`). The DoF and
  bloom layers then move by the Halton jitter (~1 render px peak to peak) against the stable RR image: DoF-edge shimmer.
- Fix (layer only, no shader edit): `rrpost_temporal` stores the jitter before its key-off return; with
  `rr_post_jitter_align` 1 `rrpost_final` runs the existing `temporal_final` entry (final plus exactly the shift) on the
  plain A/C/B. 2 = opposite sign, the sign check for both paths (the convention was never checked: AUDIT_RT row 9).
  Pipeline not built yet or no jitter: the plain final (logged `[rrpost] jitter align`). Off: same command stream.
- Key: `rr_post_jitter_align` 0 (Advanced, developer knob "Depth of field edge alignment"), live, game restart for the
  layer DLL. Cost 0 ms by construction. No ABI change, variants.txt delta 0, shader identity unchanged (no shader edit).
- Offline: `tests/rr_post_temporal_test.cpp` (jittered ramp, 16 phases: aligned stationary, unaligned moves, wrong sign
  twice as far, mean kept). Live staged: `tools/live_batches_staged/r3_core` r3-d3-jitter (Dialogue3, High, 0/1/0).
- User check: Test Dialogue3 on High, hair and fine edges over a blurred background: with On the edge stops crawling.

## Sun trace beside the GI trace: rt_sun_overlap (branch claude/r3-core, READY_FOR_LIVE, 2026-11-21)

- `core/gi.cpp` run_pre: with the key the shadow mask goes to GENERAL before the GI block, the sun trace is recorded
  right after the GI trace command (inside `hit_gi_dispatch` before the ray tracing path's RT->ALL barrier, which does
  not wait for a compute dispatch; after the dispatch on the other paths), the barrier after the GI trace orders both,
  and the sun resolve runs beside the GI temporal pass. No data dependency (read): the GI reads neither the sun mask nor
  rtVis; the sun passes write only texels 8-47 of the sun-trust image, the GI reads 0-7 and 48+.
- With the key the sun trace has no per-pass timestamps (an ALL_COMMANDS timestamp between the traces would wait) and
  runs inside the GI trace's interval: judge `rt passes: GPU cost`. Key `rt_sun_overlap` 0 (Advanced, developer knob),
  live (core hot reload). Expected gain not derivable offline (bounded by the 0.68 ms sun trace); may be negative.
- Offline replay of both orders: not possible with tools/frame_replay (one dispatch per pack: gi_hit_direct,
  reflections, local shadows; no rt_shadow / rt_sun_resolve, no dispatch sequence). Live staged: r3-s8-sun-overlap
  (S8 Ultra static, 0/1/0, cost only).
