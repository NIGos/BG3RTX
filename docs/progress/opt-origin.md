## Exact deferred origin shading normal (branch opt-origin, READY_FOR_LIVE, 2026-11-05)

- Added `rt_origin_lazy_normal`, specialization 1027, default 0 in every preset;
  developer Performance control. No ABI change; shader request delta 0.
- Saves one shading-normal fetch/decode/transform for each finite nondegenerate
  origin with statistics off. No ray, output, tolerance or counter is removed.
- Full resolution: 6,144,000 pixels / 96,000 groups at 3840 x 1600; one short
  alpha-tested primary query per positive depth, plus optional hair receiver work.
  Cascade changes the ray mask only. No safe GI-grid ray skipping or measured
  occupancy improvement was found; groups remain 8 x 8.
- Inn log read: origin 1.182 ms/frame over 28 mixed Ultra/High receipts.
  Candidate GPU cost is unmeasured; comparisons must use the same-run OFFs.
- OFF proof: `release_shader_equivalence.py --baseline main --freeze-spec 1027
  --filter gi/rt_origin.hlsl`, all 3 affected requests equivalent, 17 assignments,
  3 universal certificates, negative control PASS. Other shader sources unchanged.
- ON decision fixture: 10 named cases + 16,384 edge states; output float32 bytes
  preserved, including signed zero and nonfinite/degenerate fallbacks.
  Compiled native inn ON fetches the normal conditionally after traversal;
  OFF fetch dominates traversal. Three static ray initialization sites unchanged.
- Inn draft: `tools/live_batches/opt_r2/origin/`, OFF/ON/OFF/ON with both pipelines
  warmed, detailed logging, separate cost, float pre-post and rendered K8 QA.
  Frozen lint PASS, 0 failures; only 7 additive unproven readiness snapshot entries.
  145.684 s after load, 300 s watchdog; no live measurement or quality pass claimed.
- In game after authorization: inspect static inn surfaces and shadow stability;
  analyzer requires both ON cost savings beyond OFF-repeat spread, energy within
  OFF-derived noise, and temporal QA versus both OFF bursts. No manual toggling.
- `scripts/build.ps1 -Test`: 176/177 passed initially; fixed the new knob's Low
  dependency and regenerated settings schema, rebuilt, then reran the failed
  test through `-Test -TestRegex '^settings_test$'`: PASS (4,129 checks).
- Standard build briefly created oversized PDB/ILK and SDK copies; removed these
  exact generated files. Final workspace scan found no files over 20 MB.
  No commit, game launch or queue staging. Merge-main refresh was denied by
  sandbox ORIG_HEAD permissions; HEAD already equals main (26b06649).
