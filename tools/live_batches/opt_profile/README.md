# Optimization profiling: READY_FOR_LIVE

Four jobs, one launch per scene, Ultra then High in each launch. These are drafts;
no game launch, queue staging or authorization receipt is created by this generator.
Queue source folder: `tools/live_batches/opt_profile/jobs/`.
Exact glob: `tools/live_batches/opt_profile/jobs/opt-profile-*.json` (four files).
Coordinator commits first. Live execution requires explicit steering for this batch.

| Job | Save | Expected including 120 s load | Hard cap |
| --- | --- | ---: | ---: |
| opt-profile-s8 | Water Queens House | 3.26 min | 5 min |
| opt-profile-camp | Campsite - 27h | 4.54 min | 5 min |
| opt-profile-inn | The Blushing Mermaid - 96h 39m | 4.58 min | 5 min |
| opt-profile-dialogue1 | Test Dialogue1 | 4.70 min | 5 min |

Total estimate 17.1 minutes; hard caps total 20 minutes. Readiness transitions lack
matching VALID evidence: their zero learned delay is **unproven**, not instant readiness.
Startup has a 60 s readiness cap, High transition 10 s, and whole-launch timeout 300 s
including loading. Cold compilation or throttling can invalidate a launch; never extend it.
Use GPU ms and log costs when covered, never presented FPS. No builds, shader compilation
or other GPU work during the live batch. All input is `visible_unfocused_fg`.

Profiles come from release_qual/profile.cpp using common/settings.h, bg3rtx.ini and
data/release.ini. Derived values are resolved before writing settings (`rt_build=1`).
Detailed logging stays on, including across the High reset. Only observation/capture
controls differ from the exported profiles; rendering policy stays at release values.
CPU submit timing is explicitly enabled with `cpu_timing=1`. Use the normal test build
with `BG3RTX_DIAGNOSTICS=ON` (CMake default): a packaged diagnostics-off DLL cannot
measure submit CPU. Zero-only CPU totals invalidate the profiling receipt.
Shared target-state readiness omits disabled box, exact-k, temporal and water pipelines.
The release_qual/final jobs were regenerated with the same correction.

Each preset has a warmup, one static window and one slow Q pan (100 ms held per 400 ms).
Each measured window requests a motion-ready acknowledged stats reset, 120-frame
producer cadence, 360 frames and one acknowledged period from every required timer.
The gate/analyzer needs at least two samples; the third period accommodates delayed GPU
readbacks. Live-flow's static duration advisory compares against the two-period gate
minimum: the third period is intentional. Measured motion begins at motion-ready.
No traces, images, floats, shader dumps, diagnostic views or ray/hit counters are requested.
The High static view follows the Ultra pan; these rows describe their own views and
must not be presented as a matched-camera quality or preset A/B comparison.

Water and emissive gates are conditional on actual participation in that view. Missing
work is shown as MISSING, never zero. High disables water/emissive/temporal post.
With release RR enabled, GI denoising is shared with RR and has no independent timer.
Streamline FG copy does not consume stats resets: analysis discards its first line in
each window to exclude a straddling 120-sample accumulation. All other selected timers
consume the reset. CPU submit and driver values are watch totals in ms per 120-frame
period, not per-frame GPU costs. Emissive build is ms/build. Medians/ranges summarize
period summaries, not independent per-frame percentiles. No speedup is predicted yet.

Regenerate and validate offline from the repository root:

```powershell
. ./scripts/low-priority.ps1
& 'C:/Program Files/LLVM/bin/clang++.exe' -std=c++17 tools/live_batches/release_qual/profile.cpp -o "$env:TEMP/bg3rtx-opt-profile.exe"
python tools/live_batches/release_qual/make_final.py --profile-exe "$env:TEMP/bg3rtx-opt-profile.exe"
python tools/live_batches/opt_profile/make_jobs.py --profile-exe "$env:TEMP/bg3rtx-opt-profile.exe"
python tools/live_batches/live_flow.py tools/live_batches/opt_profile --preflight --frozen
python tools/live_batches/live_flow.py tools/live_batches/release_qual --preflight --frozen
python tests/release_qual_test.py
python tests/batch_jobs_lint_test.py
python tests/opt_profile_test.py
```

The frozen snapshot retains its 79 VALID logs, FPS models and previous entries; only
15 required readiness queries were appended. No full-log corpus cache was persisted.
Shader request delta: 0. No generated file exceeds 20 MB.
Verified: 88 tests pass across release_qual, batch_jobs_lint, opt_profile, live_flow
and test_registration. Frozen preflight passes all 4 profiling and 5 release jobs.
Regenerating both batches with the frozen snapshot produces no JSON hash drift.

Each job's post command writes `<log>.profile.md`. To analyze again:

```powershell
python tools/live_batches/opt_profile/analyze.py --job tools/live_batches/opt_profile/jobs/opt-profile-s8.json --log <game.log> --runner <run.runner.txt> --out <game.log.profile.md>
```

The output has Markdown rows ready for `docs/OPTIMIZATION_20261105.md`. Review validity,
pass participation and driver compilation before copying rows there. Missing required
costs or fewer than two complete periods produce an INVALID report and nonzero exit.
No live measurements or runtime quality pass are claimed by this offline batch.
