# Stage 4 qualification draft (2026-11-03)

Prepared offline, not queued or authorized. Historical branch audit:
`tools/live_batches/qual_stage4/merge_audit.json`. No merge or commit in this task.

The batch now has two jobs: camp (300 s phases) and inn (199 s).
Both retain the m4_generated every-window gates, real Ultra settings, masks-off
source diagnostics, same-run OFF/ON/OFF energy and GPU-cost comparisons.
Camp also retains static GI guard shots and held Q/E turns.

| Candidate | Required evidence | Prediction ON minus OFF |
|---|---|---|
| Generated rows | mapped >0 ON / 0 OFF; clip rejected <0.04%; no overflow/drops/timeouts; share gain > OFF spread; float energy neutral | camp +2.5 share points [1.5,4], GI +0.6 ms [0.3,1]; inn +0.3 [0,1], +0.05 ms [0,0.3] |
| gi_temporal_mv_sample, spec 1001 | static float energy within OFF spread; edge/inner transient <= OFF max; neutral cost | energy 0; fewer depth-edge errors |
| gi_prev_radiance_guard, spec 1002 | edge transient < min(OFF)-spread; inner/energy within OFF spread | reduced edge transient; energy 0 |

GI remains screening only: PNG turns cannot satisfy the original K16 float
transient gate (~11 GiB). The original contracts remain in predicted.json.
Generated rows remain off until live gates pass; no GPU-cost claim from dryrun.

Generated AlphaTest2s captures now survive G-buffer-before-depth recording.
Missing sources still increment no_source; the final join rejects unpaired
IP_Base_VT draws. Historical deferred-stream cost: 57 -> 93 MB at camp.

Guard shots wait up to 60 s for each exact specialization-ready regex, searching
the entire current run's log for the latest matching line, including earlier
cached records. Waiting pauses the phase clock and timeout fails the run.
OFF guard shots post the pointer centre with WM_MOUSEMOVE and log recenter;
this does not move the camera. Verify camera registration before comparisons.
Q/E traces at 3 s remain inside held motion. Unknown phase fields fail dryrun.

The S8 CPU job and plan are removed. User-supplied qs4 verdicts stand:
perf_hook_state REJECTED (submit 20.2 -> 26.8 ms, matrix 0.31 -> 0.52 ms/build);
perf_hitmat_lists no gain (1.16 ms both); perf_submit_batch within noise
(OFF 20.2/20.7 vs ON 20.3/20.3 ms). All stay off; shared helpers remain.

Dryrun PASS: two jobs INVALID without evidence / two structurally valid;
scene-wait/capacity lints pass. 499 s phases + <=240 s waits + loading,
18 traces, 1,100 MB; 16.3 min upper estimate with 120 s loading per save.
Pending inventory: [other ready items](../tools/live_batches/qual_stage4/pending_items.md).
