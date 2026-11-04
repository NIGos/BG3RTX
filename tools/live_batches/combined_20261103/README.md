# Next live series, prepared 2026-11-03

Offline drafts only. Nothing is queued and no game launch is authorized.

| Save | Source order | Launches | Phase seconds |
| --- | --- | --- | --- |
| S8 part 1 | smoke_ultra, gpu_opt1 S8, gpu_opt2 Medium | 3 -> 1 | 629.7 |
| S8 part 2 | water_offscreen, wf9 | 2 -> 1 | 843.4 |
| S8 part 3 | post_temporal S8 | 1 -> 1 | 131.7 |
| Camp part 1 | cov_paks_compact, gpu_opt1 camp | 2 -> 1 | 671.5 |
| Camp part 2 | gpu_opt2 High | 1 -> 1 | 241.0 |
| Test Dialogue3 | post_temporal, edge_flicker | 2 -> 1 | 544.5 |

The generator splits at source boundaries to keep scheduled time plus VALID-median
readiness under 900 s (largest preflight total: about 876.5 s). Old oversized S8
and camp jobs remain inactive historical artifacts. Windows and source captures
retain their gates. Phase seconds exclude loading and conditional readiness;
the exact totals are in `../readiness_report.json`. Medium/High retain their presets;
each segment restores its own preset, explicit baselines and warmup. The restart
capture budget is pinned at 1024 MB throughout. Q/E motion is unchanged.

Regenerate with `python tools/live_batches/combined_20261103/make_jobs.py`.
Run `python tools/live_batches/combined_20261103/dryrun.py` for shared lints and
synthetic extraction checks on all six active manifests. The dryrun checks that
real gates reject fake data, then strips observation gates to verify isolated
log slices and original phase numbering. It never runs original post commands.
Source batch dryruns separately exercise the fake queue.

Each job's gated post command calls `combine.py --extract ... --run-post` using
its checked-in manifest. It writes per-batch logs, restored runner files and
`results.json` under `<combined log>.batches`; original analysis receives only its
own slice. Keep manifests, sources and original trace captures until all posts
finish. Extraction fails closed on incomplete runs or plan drift.

Camp warmup now waits for prefetch and baseline GI/water pipeline readiness; the
scaled reflection warmup waits for RT_TRACE_HALF and RF resolve pipeline generations.
The failed run compiled baseline GI/water through 18:08:33 (prefetch completed
18:08:48); its RT_TRACE_HALF and RF resolve compiled inside warmup at 18:13:50.
All measured feature values already had earlier warm phases; fixed-duration startup
was insufficient for the cold shader cache. These waits have finite timeouts.
