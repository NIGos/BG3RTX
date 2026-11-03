# Stage 4: generated rows and GI guard screening

Prepared offline only. No game launch, queue submission, permission, merge or commit.
`python tools/live_batches/qual_stage4/make_batch.py` regenerates the two jobs/plans
and predictions using read-only Git queries. Historical audit: merge_audit.json.

One job per save: camp 300 s phases, inn 199 s. S8 CPU screening is removed:
perf_hook_state was rejected, perf_hitmat_lists had no gain, perf_submit_batch
was within noise. All three keys remain off. Variants delta: 0.

Camp/inn retain every m4_generated gate, Ultra settings, and masks-off native
source diagnostics. Generated rows start enabled to latch capture, then toggle
inside one run. Compare share, clip checks, capacity, float energy and GPU ms
against same-run OFF repeats; matching regexes alone do not qualify the feature.
The speculative generated 2S capture restores recording-order independence;
expect historical deferred-stream allocation 57 -> 93 MB, with live cost pending.

Camp guard shots wait up to 60 s each for exact pipeline-ready regexes. Search
all of this run's log, including earlier cached records; use the latest matching
line for each regex. Waiting pauses phase time and fails closed at timeout.
OFF shots post pointer centre with WM_MOUSEMOVE, without a Home key or camera
movement; verify camera return registration. Q/E traces at 3 s are during motion.
GI remains screening only: PNG pairs cannot satisfy the original K16 transient
gate (~11 GiB). Preserve the original contracts in predicted.json. No promotion.

Dryrun: `powershell -NoProfile -ExecutionPolicy Bypass -File tools/live_batches/qual_stage4/dryrun.ps1`.
It uses synthetic runners in TEMP with no live authority; unknown phase fields fail.
PASS: 2/2 INVALID without evidence, 2/2 structurally valid, scene-wait/capacity lints.
Estimate: 499 s phases + <=240 s readiness waits + 2 x 120 s loads = 16.3 min;
18 traces, 1,100 MB. Cost windows have no traces or expensive diagnostics.

Before a later live batch, obtain explicit user steering and use the existing
serial queue. Keep builds/compiles out of cost windows. Do not copy jobs into
pending now. The runner restores the original INI on exit/failure; final phases
also reset experimental keys and diagnostics. Never modify saves.
