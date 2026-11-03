## Pak publication audit (pakjoin-publish, offline; not qualified)
- cp4 ON frames 2280/2520: exact/records/ready 0/0/0; witness 21,588 means rejections, not full joins; startup key worked, basis is downstream.
- Sampled-only readback lacks full raw object-space witnesses; no safe publication shortcut. Native rows already serve 195/309 pool draws, 259/473 instances, 199 ready at frame 2520.
- Log/catalogue audit: 286 readbacks = 46 unique/229 missing/11 ambiguous; all 229 misses have count candidates (UV mismatch). Aggregate missing 91.39% of attempts, 58.98% of probed draws; family/pixel shares unidentifiable.
- inputs_pending 166,194 is cumulative unavailable draws, not backlog; readbacks cap at 16 jobs/16 MiB. Fix terminal absent-count draws being counted as waiting; add explicit missing_count/missing_uv and rejection labels.
- Predict ON exact/records/ready 0/0/0 with sampled-only inputs; attempts + inputs_pending = draws; missing_count + missing_uv = missing. Missing-count transfer size unknown; no positive publication prediction justified.
- Host build, 6/6 focused CTests, real 40,983-row parser and diff check pass (settings binary rebuilt after stale-binary failure); ABI/shader request delta 0, key/presets unchanged; no game, commit or GPU cost claim. Full raw witness/decoding remains required.
- Evidence: tools/live_batches/cov_paks_compact/publish-audit.json; design: TRACK_D_REFLECTIONS.md. pakjoin-zero entry read from evening archive (standalone file absent).
## Full raw witness follow-up (pakjoin-publish, READY_FOR_LIVE)
- Unique UV candidates now stage raw index/position/half2 or float2 UV copies through existing retirement; ordered SHA match feeds unchanged recipe/resource/cull/basis gates.
- Bounds: 4 MiB transfers/frame, 16 jobs/16 MiB in flight, 2 raw retirements/frame; chunk large ranges, reject oversize/mapped mutable inputs, invalidate dependent records on reuse/writes.
- Host build and 7/7 focused CTests pass: 88-byte candidate -> raw decode -> SHA -> publication -> hit lookup, unsampled mismatch, budget retry, chunk discard and reuse invalidation.
- cp4 predicts witness_ok > 0, published_records > 0 and basis ready > 0 in both ON windows; live_flow preflight passes (1 job, 0 failures).
- rt_hit_material_paks stays off in presets; ABI/shader request delta 0. No game launch, commit, measured coverage or GPU cost.
