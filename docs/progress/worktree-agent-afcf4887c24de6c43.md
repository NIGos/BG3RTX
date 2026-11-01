## Long tools monitorable and cached; capacity gate lint; smoke_ultra job (branch worktree-agent-afcf4887c24de6c43, offline, kept, 2026-11-01)

- Shared helpers: `tools/longrun.py` (Progress every 30 s to stderr: done/total, rate, elapsed, ETA, failures;
  FileCache keyed by mtime+size in the main checkout's `captures/.cache/`) and `scripts/longrun.ps1` (step
  start/end lines with durations, a heartbeat for captured output, a step table, content-hash reuse).
- Before -> after (typical input, BelowNormal):
  - merge-green: silent until each step ended -> step start/end/duration lines, a heartbeat every 30 s of output,
    each step's lines live in `build\merge-green-<step>.log`, a step table at the end.
  - build.ps1 -Test (4 min 56 s fresh: build ~3 min, ctest 122 s): ctest streamed with build/ctest durations and
    the 5 slowest tests (pt_reference_test 52 s, frame_replay_test 23 s, transport_bsdf_test 6.6 s).
  - frame_replay PT / native ambient / --repeat: silent -> `[replay] progress` every 30 s (1000-iteration PT on a
    synthetic pack: 156 s, five lines with ETA). post_replay.ps1 shows those lines live and times both runs.
  - lean_dryrun (box_lights 14 s, consolidation 37 s): run A/B start/end and per-job record lines; unchanged
    inputs (content hash of the batch, scripts\*.ps1/*.py, parameters) reuse the last pass: 14.3 s -> 0.8 s
    (`-Force` reruns). Output lines identical.
  - m4_promotion_gate (all m4_promotion logs): 5.1 s -> 2.7 s cold / 1.0 s warm; git show/log memoised and cached
    (144 git calls were most of it). Stdout byte-identical. m5 gate and queue_results.py: about 1 s, unchanged.
  - material_families: 70 s -> 3.7 s on a rerun (world count cached by the .pak signatures; per-trace frame
    evidence cached). Rows identical; their tie order varied run to run before this change too.
  - scripts.qa: the game run (minutes, output captured) prints a heartbeat every 30 s with traces written;
    analyze prints scene n/N with ETA. Not run here (needs the game; no QA captures on disk).
- queue_gates.py (on top of the merged scene-wait lint, branch worktree-agent-a498dc85e3606abe9): CAPACITY_GATES
  (RT capture opaque drops 0.00, cascade buffer full 0.0 where rt_cascade_casters=1) and `--lint-capacity`, judged
  like the scene-wait lint over traced cost windows; unit tests in queue_gates_test. 39 job problems are listed, not
  fixed (job files left alone to avoid conflicts): apply after the scene-wait branch merges, then make it a dryrun Check.
- New batch `tools/live_batches/smoke_ultra` (su1, S8, Ultra + every qualified experimental key, static / pan /
  rotation, water traced > 0 in every window): dryrun passes; expected to FAIL live on the water gate until water
  runs with rt_cascade_casters=1. READY_FOR_LIVE, not queued.
