## Final release qualification (branch final-qual, READY_FOR_LIVE, 2026-11-04)

- Prepared from main ab66c855. No commit, game launch, real queue staging or authorization receipt.
- One batch, two launches: final-s8-ultra-high (rel1 Ultra + rel2 High at Water Queens House),
  final-stock-hair-dialogue1 (rel3 Ultra stock cards). Actual exported release profiles, dev=0.
- Expected 3.72 / 3.32 minutes including 120 s load, hard timeout 300 s each; about 200 MB total
  (three energy traces at 60 MB allowance plus two 10 MB logs). No raw floats/bursts/shader dumps.
- Preserved release smoke gates, patterns/expect/expect_every, five S8 camera windows per preset,
  master toggle, stock hair/scalp records and zero bounds/capacity/scene-wait errors. No device loss
  or crash. High OMM same-ray compared > 0, unavailable 0, violations 0 in a separate diagnostic.
- All measured windows reset stats at motion-ready and acknowledge one period. visible_unfocused_fg;
  no diagnostics or captures in cost windows. Existing tools generate/reset/wait and restore settings.
- predicted_final.json contains reference per-pass costs from rel1 20:58:53 and rel3 21:03:04,
  High rel2 12:13:05, all gates and shader-change predictions. Shader request delta 0.
- Costs expected within observed scene ranges; report signed changes and explain any exception.
  Observed sparse/moving ranges are not statistical noise thresholds. No build/compile overlap.
- Post analysis automatically reads each energy trace and writes <log>.final.json: GPU pass deltas,
  float pre-post C energy (rrpost slot 2), signed historical energy deltas where available.
- Dialogue baseline luminance 0.3743881342. Rel1/rel2 have no float capture: historical S8 energy
  comparison is UNAVAILABLE, not PASS. No baseline noise repeats exist. Retain new float summaries.
- Expected energy within noise globally; corrected cutout visibility has scene-dependent sign,
  invalid history uses fresh/native (can brighten former black gaps); half-float clamps cap tails.
  Any material signed change needs physical attribution/PT; do not accept unexplained darkening.
- Live-flow preflight: all 5 source/final jobs PASS against 79 VALID logs. Frozen lint: all PASS;
  only 6 required readiness queries and missing regexes appended. Tests: release_qual 4/4,
  batch_jobs_lint 8/8, live_flow 69/69, test_registration 1/1. Frozen CLI avoids raw-log caches. No live quality pass is claimed.
- Before queueing: coordinator commits predictions, merges/builds test-main, obtains explicit user
  steering and freezes the normal job/tree/build authorization. From main, exact queue command:
  `Copy-Item -LiteralPath @('tools/live_batches/release_qual/final/jobs/final-s8-ultra-high.json', 'tools/live_batches/release_qual/final/jobs/final-stock-hair-dialogue1.json') -Destination captures/queue/pending/`
- Queue only those two final jobs into the existing serial runner. Do not queue the source receipts
  or start another runner. Cold/missed readiness fails fast within the five-minute launch cap.
- Inspect High cutout edges/holes, Ultra water/edges, stock hair cards and face/neck shadows in the
  static captures. Occluded presents/frame time cannot qualify GPU cost or visual output.
