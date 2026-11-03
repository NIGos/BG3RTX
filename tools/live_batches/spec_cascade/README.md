# spec_cascade: always-on specialization migration, S8

Updated for merged main (165 variants), after the 2026-11-03 01:39 run.
No live authorization is requested or implied. No game or shader tools were run for this update.

One module per file **and remaining layout define set**; specialization constants are always used.
There is no `rt_spec_cascade` setting. Cache-read lines are allowed: only `compiled in` is a DXC compile.
The job rejects any DXC compile after warmup, across all shader directories, including water.
It requires pipeline events on both directions of each converted-key toggle: cascade casters,
GI motion/previous-radiance guards, instanced/wind/IP-grass hit families, gi_far,
native hair/scalp/tileset, physical hair origin, rf_hit_gi and water RF edge.
The original c1/c0 window labels are retained for comparison with historical evidence;
`off-a`, `on`, `off-b` now mean identical functional settings (the old switch is removed).
The extra toggle windows are compile/pipeline checks, not energy-neutrality comparisons.
The plan is 41 phases / 413 s, plus load; offline dryrun budget 12 min.

Same-state migration prediction: zero energy and GPU ms delta, bounded by the same-run
OFF-a/OFF-b spread. Use dumps.json pre-post scene-colour float means and per-pass GPU medians.
Functional key toggles change the rendered program; comparing different values does not establish
migration energy or cost equivalence. The historical run has no legacy path for comparison.

Startup resources are reserved at launch. Hit-family and native capability words are startup-latched
(core/gi.cpp refresh); INI writes alone may not change the effective constants. Such a window must
fail the pipeline gate rather than count as coverage. Full live-toggle qualification needs effective
capability switching first, or separately specified startup fixtures. RT_HIT_POOL_2S is also a startup
capability (generated two-sided pool rows), not a live key. This job is **not fully qualified**.

## Offline re-gate of 01:39

`history/scc_s8_013945.json` preserves the exact executed plan, including its ignored old key,
so plan-drift validation stays meaningful. `regate.py` uses the updated job gates on observed
windows, lists missing new windows separately, and checks global expects/forbids/runner evidence.
It never rewrites the source logs. Run:

```powershell
python tools/live_batches/spec_cascade/regate.py --log C:/Users/quali/Documents/Code` Projects/BG3RTX/captures/queue/logs/scc1-spec-cascade-s8-fg2.1.013945.log --out tools/live_batches/spec_cascade/regate_013945.json
```

Result: **FAIL**, not INVALID for obsolete S1 expects. One compile in `toggle-spec-c0`:
`rt/water_compose.hlsl?WATER_SHADOWS=1;WATER_LS=1;WATER_REFR=1;WATER_GSH=1;WATER_SPEC_RF_EDGE=1:water_compose`
at 01:41:31.120, 1640 ms. The cascade-off toggle at 01:41:24.230 caused it:
core/water.cpp make_pipe calls common/shader_variant.h water_request, which still adds/removes
`RT_CASCADE_MASKS=1;RT_CASCADE_SUN_HIT=1`. This is a missed water cascade specialization path,
not an edge-key toggle and not a missing variants.txt entry (both forms are listed).
The other compile, gi/rt_sun_resolve.hlsl?RT_SPEC_NATIVE=1:main at 01:40:41.280 (51 ms), was startup.

Energy repeat deltas c1/c0: -0.00178401 / +0.0000328426, inside spreads 0.00377013 / 0.000119527.
GI GPU repeat deltas: -0.028 / +0.0255 ms, outside spreads 0.013 / 0.007 ms.
Total RT c0: +0.01775 ms, outside 0.0105 ms spread; RF/LS and total RT c1 are within spread.
These are repeat-stability results, not evidence of a migration regression or equivalence.
First cascade-off pipelines: 31.6-3472.1 ms; cached startup cascade-on: 0.2-2.2 ms.
Full converted-key coverage is absent; the new windows cannot be retrospectively passed.
