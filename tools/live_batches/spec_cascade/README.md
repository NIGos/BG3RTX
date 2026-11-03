# spec_cascade: rt_spec_cascade qualification, S8 (live_batch spec-cascade), READY_FOR_LIVE, not authorized

One job, `scc1-spec-cascade-s8-fg2`, Water Queens House, one launch, 17 phases, 221 s
(about 6 min with the load), the smoke_ultra keys (preset ultra, rt_cascade_casters=1 at launch) plus rt_spec_cascade=0.
- prime-spec: rt_spec_cascade=1, the RT_SPEC_CASCADE modules compile once. toggle-spec-c0: rt_cascade_casters=0 with
  the key on. Gate: no `[shaders] gi/... compiled|read from the cache` line in any window where only the casters change
  with the key on (toggle-spec-c0, cost-c1-on, shot-c1-on, cost-c0-on, shot-c0-on); the value-0 pipelines are created
  there, each logged as `gi: <request> generation N spec cascade=0, pipeline <ms> ms` (pipeline creation time).
- Energy: shot-c1-on vs shot-c1-off-a/b and shot-c0-on vs shot-c0-off-a/b (dumps.json float mean, exposure held).
- GPU ms: gi, rf, ls and rt passes lines of cost-c*-on vs the mean of the off windows on both sides.
- Prediction (predicted.json, committed before the run): every delta 0 within the off-a/off-b spread.
- Offline: `powershell -NoProfile -ExecutionPolicy Bypass -File tools\live_batches\spec_cascade\dryrun.ps1`.
