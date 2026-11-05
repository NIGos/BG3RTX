## BLAS flags replay (branch blasflags, incomplete, 2026-11-05)

- Replay-only --blas-policy legacy/layer/trace and paired --ab-blas; product code unchanged; shader request delta 0.
- Classes exact from pool/soup/held InstInfo bits and manifest casters; static alpha slots cannot overlap held bit 31.
- Layer A: static FAST_TRACE | ALLOW_COMPACTION; dynamic/cascade FAST_BUILD | ALLOW_UPDATE (cascade refit on).
  B changes dynamic/cascade to FAST_TRACE | ALLOW_UPDATE; static BLAS shared, replay does not compact them.
- Cached current sun/far production request with native/instance/cascade/far constants; radius 6 m, 2 rays,
  rt_stats=0, compact-hit-state=1, roof-early=1, --sun-trust; 40 ABBA/BAAB pairs, 8 warmups per sample.
- Exact Ultra unavailable: packs have lighting set 2, no emissive scene. _em trial stopped as invalid;
  retained timings use gi_trace_rt_hit_sun (no emission). Frozen geometry also limits animated-refit inference.
- S8 trace gain 0.046672 [0.043168, 0.049936] ms (95% median CI); outputs/counters 100% bit-equal,
  0 differing pixels, max diff 0. Static 164/369437 tris, dynamic 272/1258387 tris, no cascade/held.
- S8 build A/B 1.316648/1.802176 ms; paired extra 0.457704 [0.420960, 0.487472].
  Refit A/B 0.199992/0.374704 ms; paired extra 0.174720 [0.173088, 0.177696].
- Stable Ultra cycle: 600 refits + 1 build, cascade 60 refits + 1 build. S8 extra AS 0.175191 ms/frame;
  net gain -0.128519 ms/frame, conservative 95% Bonferroni model interval [-0.136867, -0.122779].
- S8 same-flags control trace B-A +0.004112 [-0.004304, 0.024368] ms; exact outputs/counters.
- Camp control INVALID: Stalker2 started 23:35:02 during 23:34:26-23:35:29 run; candidate stopped at pair 11.
  Partial candidate: 11/11 quartets bit-equal, but GPU timings confounded (8 -> 20-25 ms). Inn pending.
- GPU work stopped; user notified. No bg3.exe, game launch, queue activity or git writes.
- Verification: replay build; existing synthetic self-test PASS; class + AB statistics tests PASS;
  synthetic dynamic/cascade A/B (build/refit included) under Vulkan validation, exact outputs/counters, 0 errors.
- Useful replay-only code retained for remaining comparisons. No product proposal: S8 net loss; full Ultra unproved.
- Receipts: .scratch/blasflags/{s8,s8-control,camp,camp-control}; summary.json; shader.json; command.json/log/CSV per run.
