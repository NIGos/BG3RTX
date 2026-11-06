## BLAS flags replay (branch blasflags, rejected, 2026-11-06)

- Replay-only --blas-policy legacy/layer/trace and --ab-blas retained; no product change; shader request delta 0.
- Exact classes from pool/soup/held InstInfo bits and manifest casters; no held instances in these packs. Layer A: static FAST_TRACE | ALLOW_COMPACTION, dynamic/cascade FAST_BUILD | ALLOW_UPDATE; B changes only
  dynamic/cascade to FAST_TRACE | ALLOW_UPDATE. Static BLAS shared; replay does not compact them.
- Sun/far request with Ultra native/instance/cascade/far specs; radius 6 m, 2 rays, rt_stats=0,
  compact-hit-state=1, roof-early=1, --sun-trust; 40 ABBA/BAAB pairs per trace/build/refit, 8 trace warmups/sample.
- LIMIT: packs lack emissive scene descriptors; supported sun/far without emission approximates full Ultra.
  Frozen geometry cannot establish traversal/refit cost after animation. Build timings exclude allocation/TLAS.
- All values ms; trace gain = A-B; extra build/refit = B-A. Brackets are 95% median CIs.

| Pack | Trace gain | Extra AS/frame | Net gain/frame [conservative 95% model interval] |
|---|---|---|---|
| S8 | 0.046672 [0.043168, 0.049936] | 0.175191 | -0.128519 [-0.136867, -0.122779] |
| Camp | 0.377312 [0.232448, 0.641920] | 0.500884 | -0.123572 [-0.389910, 0.193367] |
| Inn | 0.075696 [0.039024, 0.189216] | 0.692690 | -0.616994 [-0.810208, -0.462765] |

| Pack/class | Extra build [95% CI] | Extra refit [95% CI] |
|---|---|---|
| S8 dynamic | 0.457704 [0.420960, 0.487472] | 0.174720 [0.173088, 0.177696] |
| Camp dynamic | 0.251400 [0.183968, 0.324512] | 0.334608 [0.331808, 0.337856] |
| Camp cascade | 0.607416 [0.546464, 0.648304] | 0.159064 [0.157968, 0.160048] |
| Inn dynamic | 1.391960 [1.121872, 1.701520] | 0.434976 [0.429584, 0.441536] |
| Inn cascade | 0.792368 [0.699072, 0.900816] | 0.247184 [0.245120, 0.249232] |

- Net = trace gain - (build + R * refit)/(R+1) extra, R=600 dynamic/60 cascade; Bonferroni component bounds.
- Every measured quartet: outputs/counters 100% bit-equal, 0 differing pixels, max diff 0 (candidates and controls).
- Same-flags trace B-A controls: S8 +0.004112 [-0.004304, 0.024368], camp +0.046920 [-0.103152, 0.239136], inn -0.021656 [-0.134704, 0.026384].
- Old camp runs remain INVALID (Stalker2 overlap); completed reruns guarded before launch/every second for Stalker2/bg3/Cyberpunk; no game detected, no game/queue/git writes.
- Receipts: .scratch/blasflags/{s8,s8-control,camp-idle,camp-control-idle,inn-idle,inn-control-idle}; summary.json, shader.json, command/log/CSVs; previous synthetic/class/AB tests passed, 0 validation errors.
