# Offline live-job design checks

Run `python tools/live_batches/live_flow.py` to check every checked-in job, or
pass a batch folder or a single job JSON. `lean_dryrun.ps1` runs this check before
its reuse cache and fake queue. No command in this helper launches the game.
The current per-job findings are in [live_flow_audit.md](live_flow_audit.md).

The corpus reads `captures/queue/results.jsonl` from this checkout, falling back
to the main checkout for a linked worktree. Only rows with `valid: true` qualify.
It selects the newest 3 readable logs per save; old rows resolve save names from
the exact job name in prepared jobs and queue `done` jobs. Unknown saves are
reported and excluded. `--results`, `--newest` and `--cache` override these choices.
The cache lives in this checkout's `captures/.cache`; result, job and log file
size/mtime signatures invalidate it. Regex hits are cached against that corpus.
Forbids matching any VALID log fail with a quoted line. Expect patterns are also
compiled and matched, with hit counts in a JSON `--report`; no historic match
is required for an expectation about a new effect. Missing corpus evidence fails.

Window gates use the producer cadence, the worst alignment of the next line,
and the runner's skipped first line after settings apply. The window endpoint is
exclusive. Every required line must fit; `when_present` does not excuse an
insufficient window when its pass runs. Known periodic events also have their
cadence checked. One-off trace/burst events retain event semantics.

Jobs can declare these planning assumptions in a top-level `live_flow` object:

```json
{
  "expected_fps": 18,
  "settings_latency_s": 1,
  "trace_stall_s": 3,
  "startup_only": ["example_startup_key"]
}
```

Foreground jobs must declare expected rendered FPS. Visible unfocused jobs use
30 rendered FPS. Trace stall bounds must be declared for periodic gates in trace
windows. Unknown producers must declare `period_frames` or `period_s` on their
gate, with the source of the cadence recorded in the batch's documentation.
The lint reports the minimum window length and guaranteed complete-line count.
These are planning assumptions, not a promise about an arbitrarily stalled GPU.

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
modes, runner args, startup presets, declared startup keys and timing assumptions.
Only single MovePlan jobs are supported. It never writes a pending inbox.

A reset before each later batch sets every key used by any batch to the next
batch's startup value or its shipped preset/default value. Unknown keys without
explicit baselines in every input are rejected. Each source batch keeps its own
warmup requirements. Labels, trace phase numbers, every-window and trend gates
are prefixed/rebased; patterns and expectations are merged. Only common forbids
apply globally; each batch's full forbid list is checked again on its own slice.

The combined job's gated post command extracts each batch automatically. To
extract offline, use `--extract <manifest> --log <log> --runner <runner> --out <dir>`;
add `--run-post` to execute original post commands. Incomplete runs and plan drift
fail closed. Original labels, indices, queue log basenames and sibling runner
files are restored. Each slice contains the common launch preamble plus its own
game lines; trace paths still point to the original captures. Each original post
command receives only that slice's log, runner and captures. `results.json` records
per-batch validity and post exit codes. Keep the source jobs, captures and manifest
until their post gates have finished reading them.
