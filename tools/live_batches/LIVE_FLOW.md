# Offline live-job design checks

Run `python tools/live_batches/live_flow.py` to check every checked-in job, or
pass a batch folder or a single job JSON. `lean_dryrun.ps1` runs this check before
its reuse cache and fake queue. No command in this helper launches the game.
The current per-job findings are in [live_flow_audit.md](live_flow_audit.md).

The corpus reads `captures/queue/results.jsonl` from this checkout, falling back
to the main checkout for a linked worktree. Only rows with `valid: true` qualify.
FPS modelling uses all readable VALID logs by default; --newest can bound it.
The forbid audit retains the newest 3 readable logs per save; old rows resolve save names from
the exact job name in prepared jobs and queue `done` jobs. Unknown saves are
reported and excluded. `--results`, `--newest` and `--cache` override these choices.
The cache lives in this checkout's `captures/.cache`; result, job and log file
size/mtime signatures invalidate it. Regex hits are cached against that corpus.
Forbids matching any VALID log fail with a quoted line. Expect patterns are also
compiled and matched, with hit counts in a JSON `--report`; no historic match
is required for an expectation about a new effect. Missing corpus evidence fails.

Measured periodic comparison windows use rendered frames. Generators set
`rt_stats_period_frames=60`, `frames`, `settle_frames` and `stats_reset=true`.
The frame budget is settling plus one complete producer period. A gate with a
derived larger sample requirement must retain that requirement explicitly.
FPS converts this budget to seconds for duration and launch-cap estimates; it
does not decide when the runner ends a frame window.

The runner acknowledges the phase settings and pipelines, waits the settling
frames, then changes `rt_stats_reset`. It requires the matching
`[stats] reset token=N frame=F` acknowledgement before logging motion-ready.
Periodic producers clear their accumulated and pending samples at the reset.
The first completed period is therefore usable without a straddling-line skip.
A missing or mismatched acknowledgement cannot establish this boundary. Legacy
logs without an acknowledged reset keep the conservative first-line skip.
GPU readback completion can lag the last sampled frame; complete producer lines
must arrive before the settings advance. A frame count alone is insufficient.

Trace stalls are charged only in windows with trace requests. Trace windows
retain their requested event counts and wait for completed exports. Trace-only
capture windows have no periodic stats sample to collect; their completion is
bounded by the requested exports rather than an assumed frame rate. The
seconds estimate includes the measured capture stall, without increasing the
comparison's required sample frames. Non-comparison scene settling, reload and
failure-survival observations retain their independently justified bounds.

Required pool-log lines also need `rt_stats=1` in each gated window; startup
candidate-loading lines do not. Producer prerequisites live in `PRODUCER_SETTINGS`.
The runner sends the full job context through a JSON file. Lint checks the remaining
job-derived environment values against the Windows 32767 UTF-16-unit limit
(including the terminator), including tags and generated INI/log paths.

Jobs can declare these planning assumptions in a top-level `live_flow` object:

```json
{
  "expected_fps": 18,
  "settings_latency_s": 1,
  "trace_stall_s": 3,
  "startup_only": ["example_startup_key"]
}
```

Foreground jobs must declare expected rendered FPS. Visible unfocused jobs have
a ceiling of 30 rendered FPS; this is never assumed to be diagnostic throughput. Trace stall bounds must be declared for periodic gates in trace
windows. Unknown producers must declare `period_frames` or `period_s` on their
gate, with the source of the cadence recorded in the batch's documentation.
The lint reports the minimum window length and guaranteed complete-line count.
These are planning assumptions, not a promise about an arbitrarily stalled GPU.

Periodic trace windows also charge the VALID corpus capture-frame stall p95.
Each motion-trace is paired with its first dump decode before the next request;
the sample is max(decode seconds, maximum frame seconds in the next pace report).
The latter includes decoding, so the two are not added. Missing pace reports use
decode time alone; dumps without a trace request are excluded. The nearest-rank
p95 of 437 samples in 75 VALID logs is 2.71857 s (source wf7a 164300, maximum
2.9799 s). `trace_capture_stall` preserves this evidence in jobs and the lint
snapshot; the larger of measured p95 and declared trace_stall_s is charged per
trace. Legacy seconds-only windows also charge settings latency and the
straddling period. Trace export event scheduling retains its separate
request-to-written model.
For the historical wsf1 seconds-only plan, 26.8817 rendered FPS gives 120 / FPS = 4.464 s per geo period:
1 + 2 * 2.71857 + 2 * 4.464 = 15.36514 s, rounded exclusively to 15.4 s.
The window 4 timing replay in tests/fixtures/window_stall_wsf1.json retains the
real geo timestamps: 13.642 -> 22.432 is excluded by the old 21.797 endpoint,
but fits before ready 09.680 + 15.4 = 25.080. This replaces the historical next
toggle for a timing counterfactual only; it proves no Fresnel rendering quality.

Scene settling and failure survival are wall-clock observations, not log cadence.
Warm/prefetch bounds are retained even with readiness waits; the first warmup also
waits for scene-ready. Recent VALID logs show time after scene-ready until both
a zero-timeout/no-skip scene-wait interval and a zero-build pipeline watch interval:

| Save | Deciding VALID log | Scene ready | Last required observation | Derived floor |
| --- | --- | --- | --- | --- |
| S8 | wf9-s8-compose-shift.1.151625.log | 15:17:38.316 | 15:17:53.858 | 17 s |
| Dialogue3 | ef1-dialogue3-attribution.1.121018.log | 12:11:25.123 | 12:11:42.441 | 19 s |
| Dialogue1 | ds1-dialogue1-fg2.1.061453.log | 06:15:53.358 | 06:16:07.852 | 16 s |
| Camp | om3-camp-option-b-fg2.1.133450.log | 13:36:07.168 | 13:36:34.795 | 29 s |

Each floor is ceil(max observed delay across the newest three VALID logs) + 1 s
settings/alignment allowance. Existing longer bounds remain, including su1's 45 s.
These observed floors are not proof that every future load settles as quickly.
The reported hair failures at 2 s after enable and 25 s after reload require hv5's
three adjacent same-key 20 s survival windows (60 s uninterrupted per reload).
Other hair post-reload observations retain 26.1 s (25 + 1 s apply, exclusive end).
Repeated hair toggles retain their source's 5 s each, covering the observed 2 s
enable failure plus settings latency before a later toggle can mask the fault.

Queue preflight runs the same lint before starting a MovePlan job and prints its
scheduled duration and readiness allowance. The default launch limit is 15 minutes;
an explicit positive `max_minutes` overrides it. Duration reports include each
window, the plan total, and savings findings above 1.5 times the window minimum
or 1.3 times the plan minimum. Unresolved observation bounds remain explicit.

Fail-fast is on unless a job sets `"fail_fast": false`. The runner checks forbids,
closed every-window gates after their settling margin, and required periodic
producer expectations. A silent producer reports `producer silent: <pattern>`.
Combined jobs carry each source's checks; a local failure skips to the next
settings reset. Crash and device loss end the launch. Status records retain
`completed`, `failed_check`, `failed_phase`, and `slice_failures`; failed slices
never acquire a successful completion marker during extraction. The runner uses
its normal close path and restores the prior automatic settings file.

Rendering values used during measurement must run in earlier non-capture phases.
Only capture commands, exposure latching and diagnostic switches are exempt;
they schedule observations. Startup-only keys must remain constant after warmup.
Warmup after the first measured window cannot repair a cold first measurement.
Known load-time mesh-pool forbids have become zero-tolerance event gates over
measured windows, preserving failure detection while excluding scene loading.

## Combining saves

```powershell
python tools/live_batches/combine.py <job.json> <job.json> --out captures/offline/combined
```

The tool groups by save, writes one job/plan/manifest per save, and immediately
runs the shared offline design checks. It rejects incompatible trees, window
modes, runner args, declared startup keys and timing assumptions.
Trace-write stall budgets may differ by source; each combined window uses its
source segment's budget. Expected FPS ceilings and settings latency must still agree.
Only single MovePlan jobs are supported. It never writes a pending inbox.

A reset before each later batch sets every key used by any batch to the next
batch's startup value or its shipped preset/default value. Unknown keys without
explicit baselines in every input are rejected. Each source batch keeps its own
warmup requirements. Labels, trace phase numbers, every-window and trend gates
are prefixed/rebased; patterns and expectations are merged. Only common forbids
apply globally; each batch's full forbid list is checked again on its own slice.

Different presets are supported: each reset restores the next batch's preset and
all settings that differ between the shipped presets, including keys untouched
by either plan. An explicitly declared startup-only preset still cannot change.

The combined job's gated post command extracts each batch automatically. To
extract offline, use `--extract <manifest> --log <log> --runner <runner> --out <dir>`;
add `--run-post` to execute original post commands. Incomplete runs and plan drift
fail closed. Original labels, indices, queue log basenames and sibling runner
files are restored. Each slice contains the common launch preamble plus its own
game lines; trace paths still point to the original captures. Each original post
command receives only that slice's log, runner and captures. `results.json` records
per-batch validity and post exit codes. Keep the source jobs, captures and manifest
until their post gates have finished reading them.

## Offline live-batch preparation

Jobs and plans belong under `tools/live_batches/<batch>`, never a watched inbox.
Write `predicted.json` before generating jobs. Combine phase groups with
`combine.py` into one launch per save; use a separate short GPU-AV launch.
Startup-latched settings must agree across combined phases. Do not combine
validation timing with performance windows. Hold Q/E for turns; use wheel for zoom.

Run the batch's `dryrun.ps1`. It samples every camera phase, checks trace markers,
window roles, scene-wait/capacity lints, budgets, and fake-queue positive/negative
paths. These receipts have no live authorization. Run Python synthetic tests too.

Only the serial queue may launch a game, with fresh authorization. An offline
dryrun, missing observables, or a draw-count log never grants a quality pass.
Every ON window and baseline character must be represented in the analyzer input.
Keep raw capture provenance; report FAIL/INVALID per check in JSON and Markdown.

Camera plan fields are shared by the runner and lint in `scripts/camera-plan-fields.json`.
`registration_policy` is analysis-only metadata for edge_flicker, rr_hair_guide and
rr_post_decision; it does not schedule runner actions. Unknown fields fail before launch.
Combined jobs retain external `requires_success_of` prerequisites; inputs depending
on another input must run separately.

## FPS windows and replay (tools-fps-windows)

`fps_windows.py` groups frame-counter deltas by save and effective diagnostic state:
rt_stats, rt_primary_check, OMM check (rt_stats + rt_omm), rt_omm_reference,
and GPU-AV. It uses the lower nearest-rank tenth percentile within each window,
then the same quantile across windows. Both counter endpoints must follow that
window's settings read and precede its next boundary. Midnight rolls over. Only
app counters (`input copy frame` / `DLSS-G frame`) count; displayed FG ratios do not.
Historical runner settings are authoritative; implicit historical OMM is inferred
from its actual check producer, rather than today's evolving Ultra preset.

The model and source-log paths are embedded in each regenerated job's live_flow
fps_windows table. Preflight, capture sizing, producer waits and live_guard use
one model, including after the runner starts in a separate process. An explicit
expected_fps is an upper bound. Cache signatures include runner and job files.

Without a matching VALID save/state, the fallback is
`1000 / (1000 / base_fps + sum(enabled cost_ms))`.
Base FPS is that save's ordinary-state p10, capped by the declared ceiling.
The additive table deliberately overestimates overlapping diagnostic work:

| Key | Additional ms/frame | Evidence / bound |
| --- | ---: | --- |
| rt_stats | 100 | Camp VALID diagnostic p10 7.562 FPS: 132.2 ms/frame, about 99 ms above 30 FPS; rounded up. |
| rt_primary_check | 80 | Dialogue1 VALID diagnostics about 9.59 FPS; combined stats/check overhead about 71 ms; round up to 80. |
| OMM check | 80 | User's observed check cost (~80 ms/frame); su1 214253 confirms diagnostic slowdown. |
| rt_omm_reference | 80 | Conservatively charge the same extra reference traversal as the OMM check; no isolated VALID reference state. |
| GPU-AV | 200 | hv4-gpuav.1.211903: post-ready app-counter pairs reach 128.1 ms/frame; about 95 ms over 30 FPS, rounded to 100 and doubled for absent VALID window evidence. |

Fallback bounds are planning estimates, not measured isolated costs or rendering
qualification. The two failed runs are timing witnesses only, never VALID corpus
samples. GPU-AV and reference fallback remain explicitly identifiable in budgets.
Known frame-based producer cadences take precedence over stale period_s estimates
(cp4's 9 s estimate cannot override the 120-frame pak producer).

Regenerate active artifacts without queuing:
`python tools/live_batches/live_flow.py --retime --batches smoke_ultra,cov_paks_compact,hair_validation,hair_crowd,water_surface_fresnel,post_temporal,combined_20261103 --replay-only --report captures/offline/fps_windows_replay.json`.
Batch generators use the same retime path. Combined manifest timing copies are
updated with their plans. No gates or gate counts are changed. lean_dryrun calls
live_flow --preflight and uses its duration/readiness totals; it retains camera,
role, trace, disk and fake-queue checks, with no 5-25 s or fixed +240 s rule.

`--replay-only` checks every expect and expect_every against the newest readable
VALID log of the same save, with target lengths derived by the duration model.
Each target diagnostic state is mapped to the newest historical window in that
state. Real timestamps/settings boundaries are retained: longer target windows
cannot borrow samples after an old toggle. Missing states, producers, complete
samples and attributable trend windows are reported as unproven. Replay exits 1
when any job lacks evidence. This is an offline coverage check, never a new live
PASS. See fps_windows_report.md for before/after lengths and remaining blockers.

Changed rendering settings require a pipeline readiness wait in every changed
window. The shared retime hook adds known key signals plus a fresh zero-build
watch interval after the current settings acknowledgement. The signal table is
in `pipeline_readiness.py`: VALID `pt-dialogue3.1.210703.log` supplies both temporal
pipeline-built records; `su1-ultra-s8-fg2.1.004527.log` supplies the cascade
specialization records. No current VALID corpus entry contains a Fresnel state
record: its signature is taken from the supplied failed wsf1 log and the
producer in layer/water.cpp, and remains unqualified timing evidence.
Surface state must be fresh,
so an earlier enabled record cannot release a later toggle. Cached immutable
pipeline records remain reusable, but outstanding asynchronous build-start
records must have matching completion records before release. Unknown keys
without an explicit key-specific wait fail lint; a quiet driver alone is not
proof of completion. Non-pipeline capture/log controls are listed separately.
Final gates attribute samples after `[motion-ready]`; fail-fast defers them
while readiness is pending. A closed window without its marker is invalid.

Regenerate the active queue with `python -m tools.live_batches.regenerate_active`.
Every generator reaches `pipeline_readiness.apply` through the shared retime
hook; combine also applies it after inserting settings resets. Active jobs are
explicitly marked in `duration_lint.json`; historical artifacts remain inactive.
`python tools/live_batches/live_flow.py --active --preflight` and the
`batch_jobs_lint_test` CTest gate the current active inventory. CTest uses the
checked-in `lint_corpus_snapshot.json`, not mutable queue results or log files.
After regenerating jobs against current live evidence, explicitly refresh with
`python -m tools.live_batches.lint_snapshot`. Review that snapshot alongside the
job changes. It stores FPS by save/diagnostic signature, trace-export p95,
readiness estimates by save/pattern/key transition, and regex match counts;
it retains no logs. New readiness queries or regexes require a refresh.
Historical replay stays a live-evidence audit, outside this deterministic test.
Runtime preflight continues to use the live corpus. Direct preflight
of an inactive job still checks all rules; inactive does not authorize a launch.
`readiness_report.json` records before/after scheduled seconds, median readiness
seconds and their preflight sum. Missing VALID readiness samples remain unproven.

Additional mappings use VALID `su1-ultra-s8-fg2.1.004527.log` completion records:
GI AO -> gi_ao_apply, strength -> gi_composite, shadows -> rt_shadow,
roughness -> rt_reflections, local sample/shadow policy -> rt_local_shadows,
CBT -> rr_post resolve, alpha -> GI query families, water -> compose/reflect.
`core/gi.cpp` push-constant construction and refresh calls, `layer/rrpost.cpp`
CBT use, and `core/water.cpp` pipeline selection identify their consumers.
These scalar controls reuse pipelines; they do not each select a shader define.

The explicit `CONSERVATIVE` table uses successful generic `[shaders]` compiled,
cache-read or prefetch completion plus fresh quiet for fibre, primary ordering,
crowd LOD, pak publication, water geometry, RR, FG, preset and A/B reset.
Fibre selects `gi/hair_fibre.hlsl` in `core/gi.cpp`; primary ordering is CPU
publication in `layer/hair_primary.cpp`, crowd is host LOD, paks/geometry are
host data, and RR/FG include vendor pipelines. No authoritative VALID pipeline
witness establishes those transitions here. This fallback is deliberately
conservative preparation evidence, not a key-specific completion or timing
qualification. Unlisted unknown keys still fail closed. See the report's
`conservative_keys`; no rendering gate or expected sample count is relaxed.
