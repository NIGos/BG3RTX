# AGENTS.md: rules for any agent working on BG3RTX

Start each session with `docs/HANDOFF_SESSION.md`, then the top of `docs/PROGRESS_CODEX.md`.
These rules come from mistakes already made in this repo. They apply on every branch.

## Never
- Touch the user's game saves.
- Force-push, rewrite history, or move/delete the `snapshot-20261027/*` tags.
- Launch the game outside `scripts/test-queue.ps1`, or start the runner while the user plays
  (a bg3.exe the runner did not launch, or `captures/queue/USER_PLAYING` exists).
- Hold `captures/queue/results.md` open (`tail -f` included): it stopped the runner once.
- Archive, delete or clean a checkout that has NTFS junctions to shared folders
  (`external/`, `data/assetdb`). Cleanup followed them and emptied the SDKs once.
  Remove worktrees only with the main checkout's `scripts/remove-worktree.ps1 -Name <folder>` (unlinks junctions with
  `[IO.Directory]::Delete(path,$false)`, checks the shared file count before and after; `cmd /c rmdir` from bash
  once failed silently and emptied the SDKs again on 2026-10-31). Never a recursive delete; then check
  the targets still exist.
- Write to the backups in `D:\BG3_Tools` (read-only for recovery).

## Project map (read docs/ARCHITECTURE.md for the full picture)
- `layer/` = host layer `VkLayer_bg3rtx.dll`: stable, needs a game restart to change. Hooks,
  tracking, geometry capture, BVH (`rtscene`, `bvhsplit`, `alphatest`), NGX/Streamline, asset DB.
- `core/` = `bg3rtx_core.dll`: hot-reloaded on rebuild. All lighting/RT/GI logic. Put new logic
  here whenever possible: it is testable live without restarting the game.
- `common/host_api.h` = the plain C ABI between them (`BG3RTX_ABI_VERSION`). Change it only when
  the core truly needs new host data; bump the version, rebuild both DLLs, say so in the commit.
  Every branch that bumps it conflicts with the others: keep ABI changes few and merge them early.
- `shaders/` = HLSL compiled at runtime by DXC (edit, save, the pipeline rebuilds in game).
  Variants are defines in the request (`gi/x.hlsl?RT_A=1;RT_B=1`, `common/shader_variant.h`,
  all listed in `shaders/variants.txt`), not wrapper files. With a new key off, the ordinary
  shaders must compile to byte-identical SPIR-V (check it): that proves the default path is untouched.
- `common/settings.h` = the one table of settings. New key: add it to `bg3rtx.ini`, run
  `python scripts/gen_settings_defaults.py`, and the settings test must pass. A user knob's category is
  Lighting, Surfaces, Performance, Advanced or DLSS 5: the launcher renders no knobs in any other page.
- `data/assetdb` = offline game data (lights, materials, roofs, probes); `tools/assets` builds it.
- Offline work runs at BelowNormal priority (`scripts/low-priority.ps1`, dot-sourced by build and
  merge-green; children inherit it). The game stays Normal: below builds it starves its level load.
- Tests: `.\scripts\build.ps1 -Test` (build + all CTests); `tests/qa_detectors_test.py`; QA
  harness `python -m scripts.qa` (docs/QA.md). Add a unit test for any non-trivial CPU logic.

## Developing the project
- Stage order: `docs/GOAL_NEXT.md`; current queue: `docs/HANDOFF_SESSION.md`. Gaps ranked by
  screen impact: `docs/COVERAGE.md`. Per-effect design and history: `docs/TRACK_*.md`.
- Read the existing design section before implementing (many plans are already written, e.g.
  TRACK_D_LOCAL_LIGHTS.md 9.10 for box lights); update that section when the design changes.
- Branches: one per phase or fix, from current main, short-lived. Merge small verified steps
  often (off by default) instead of keeping long research branches; merge main into long ones daily.
- Quality before frame rate on Ultra, but always report the cost (GPU ms of the pass, from the
  existing `gpu` log lines) of a new effect or change. Budgets are optimisation targets, not hard stops:
  never drop a correct, visible effect only for its ms; optimise it and let the user decide with the numbers.
- Physical correctness is the judge: a correct result may be darker than the game (RT AO self-occlusion);
  only physically wrong artefacts get fixed.
- Reuse what exists: the `ls_check`-style in-frame reference comparisons, `rt_debug`/`ls_debug`
  views, `rt_stats`/`ls_stats` counters, `Ctrl+Shift+F9` A/B, the QA detectors.
- When a feature is done: its key, its gate numbers, its cost, and how the user checks it in game
  (what to look at, which save) go in the progress file and the effect's TRACK doc.
- Liveness (docs/REGRESSION_PROCESS.md): a key whose default or preset value becomes non-zero needs its row in
  `tools/live_batches/liveness.json` in the same branch. The row names the log line that proves the feature does work in
  that preset: a value > 0, a line that must appear, or a line that must not appear. The CTest `liveness_test`
  (merge-green) fails otherwise. Every queue run is judged with it; read its `DEAD` lines like a failed gate.

## Fix first, measure second
- The goal is a visible, physically correct improvement in game (QUALITY R0). Instrumentation is
  a means, never the deliverable. If the next step is a known fix, do the fix; don't park it.
- One hypothesis -> read the code -> the smallest change behind an ini key (off by default) ->
  A/B -> keep or revert. Before writing a new tool, check whether existing logs/QA answer it.
- Don't open new research threads (attribution, exposure, provenance, FG, HDR UI, swapchain)
  while a phase has a concrete pending fix.
- A failed or confounded measurement: note it in one line and fix the test, not the theory.

## Measuring correctly
- Tools that run more than a minute print progress at least every ~30 s (done/total, rate, ETA, failures
  so far, flushed) and cache what does not change (e.g. baseline SPIR-V hashes per commit; compile only
  requests whose sources or includes changed). A silent, recompute-everything tool is a bug: fix it first.
  One shared helper, not one copy per feature.
  Offline shader compiles and SPIR-V byte identity: `tools/shader_identity.py` (cached, progress; the
  `tests/*_shader_test.py` gates and merge-green's `tools/compile_shaders.py` use it). Don't call DXC directly.
- Shader compile time is the bottleneck (lessons of 2026-11-02: four ~2 h identity runs and a 3 h merge-green at once):
  - Variants multiply: a new on/off key goes in as a Vulkan specialization constant (ids
    registered in `common/shader_variant.h`, `kSpecId*`), not a define, unless it changes a layout or
    binding. Report the `shaders/variants.txt` request delta of every branch; +1000 needs a reason.
  - The cache keys on preprocessed text (`BG3RTX_SHADER_PP`): an edit that leaves a request's preprocessed text
    unchanged costs no compile. Only requests that really change are compiled and identity-checked.
  - DXC slots are machine-wide: parallel compile runs do not add throughput, they only delay each other.
    Run heavy compiles (identity, merge-green with new variants) one at a time, highest priority first.
  - Don't spend compile time that a merged or planned improvement makes unnecessary (user rule, 2026-11-02):
    finish the improvement first (e.g. the spec-constant migration), then compile and check once.
  - merge-green updates the test tree, so no live can run during it: queue a long merge-green after the lives
    that are ready, not before them. merge-green merges in the main checkout: if one is stopped, check
    `.git/MERGE_HEAD` and run `git merge --abort` before anything else.
- Chain scripts: `set -f` before word-splitting job lists (an unquoted `*` expanded to file names once), stop
  on `MERGE FAILED`, and stop children too (`taskkill /T`): stopping only the wrapper left a stale chain running.
- GPU statistics counters live in device-local memory, reduced per workgroup, copied to the host after
  the timed region: host-visible atomics cost 15 ms (water reflections) and 7 ms (water compose) in 2026-11.
  CTest `host_atomics_lint_test` fails on a new shader atomic into a host-visible buffer (classify it there).
- Sensible gates (user rule, 2026-11-14; `tools/live_batches/sane_gates.py`): a gate FAILS only beyond the run's own
  noise (OFF repeats) PLUS a stated materiality floor (cost: max of 0.1 ms and 1%; energy: 0.5%; equal counts: 1%). A p99
  takes the full OFF range as noise. Events in OFF windows are the baseline: only ON counts above them fail (counters the
  feature owns stay at 0). Within noise plus floor print INFO. An unpredicted drop of a cost, or an energy difference beyond noise under the floor, is
  EXPLAIN at the top of the report (it may be skipped work), never a silent pass. A window with too few periods is
  "not judged"; INVALID only when no decisive window is. Runner `expect` patterns check that the lines exist, no more.
- A gate must hold in every ON window, not in one: use the job's `expect_every` (select, match,
  plan `windows`; scripts/queue_gates.py) for per-window stats lines; plain `expect` is any-line.
- Compare A/B within ONE run (toggle the key during the same moving session), never images
  across runs: two pans never match.
- Energy: a reconstruction change (denoise, temporal, sampling, caching) must not change mean brightness.
  A shading-model change is gated against the PT reference instead (it may change the mean). Rejected temporal history falls
  back to the fresh current sample (same expected value, noisier, never darker). Check that
  key-on mean equals key-off within noise on a static camera before trusting any residual.
- A lower residual with lower energy is not an improvement.
- Every threshold is derived (encoding precision, footprint, noise of key-off repeats), never tuned.
- Background stats lines that need 120 frames need `"hold": 90` or more in the job.
- Give every queue job `patterns` and `expect`; a run without its expect lines is INVALID.

## Lean live tests
- One game launch per save: group jobs by scene, use session jobs where the queue supports them.
- A/B by toggling keys inside one run, never by relaunching.
- Size phases in frames (a few 120-frame windows), not long fixed seconds.
- Expensive diagnostics (`rt_stats` + `rt_primary_check` cost ~80 ms/frame, measured 2026-10-31)
  only in short dedicated windows, never in fps/cost windows.
- PNG pairs instead of float bursts, unless a flicker analysis needs bursts.
- Disk budget ~2 GB per batch; delete raw dumps after analysis, keep the summaries. More is fine when a
  one-off pack helps (replay, PT reference), as long as nothing dead accumulates.
- Target 2-5 minutes per scene; never one launch over 15 minutes. A live runs only for a decision-critical
  observable; the runner aborts as soon as the result cannot be valid (fail-fast). An INVALID run is mined for
  its numbers before any rerun.
- The window may take focus when the user allows it (removes the unfocused 30 fps cap).
- When the user is at the PC, run jobs with `"window_mode": "visible_unfocused_fg"`: the game never takes
  focus (PostMessage input, FG kept). A covered window is only counted (`unfocused_occluded_checks`), not
  fatal: DWM may throttle its presents, so judge GPU ms, energy and log lines there, never frame time or
  presented (FG) captures from occluded checks. Minimising is still discouraged (presents may stop).
- Don't run builds, shader compiles or GPU tests during a live batch's cost windows: CPU/GPU contention
  moved the inn frame time by 3.4 ms on 2026-11-01. If they overlap, judge only the GPU ms lines.
- Energy is the float mean of the pre-post scene colour in `dumps.json`, never PNG previews or bloom
  dumps: the bloom's darkest-of-four filter darkens noise (TRACK_D_LOCAL_LIGHTS 9.11, 2026-11-01).
- Keep what a test or comparison still reads (the current replay pack, gate baselines); delete superseded
  packs and analysed raw dumps. Superseded generated data (old catalogues, intermediate packs) may be
  deleted without asking once a newer verified version is in use; never junction targets or `D:\BG3_Tools`.
- Lint every job before queuing it (`tools/live_batches/live_flow.py`; the whole-tree run gates the active jobs and
  counts the historical ones apart): forbid patterns
  against recent valid logs, windows longer than the stats period, warm-up before measured windows.

## Coordinating agents (lessons of 2026-11-01)
- Work in 2-3 streams, one per area (files that change together). Inside a stream, one agent fixes the items
  in sequence: after the first fix it already knows the code. Resume a finished agent for a follow-up in its
  area instead of spawning a new one. Never one agent per tiny task.
- Briefs carry the evidence and exact file:line pointers, ask for the smallest fix that passes the gate,
  forbid new frameworks, and cap the report (~15 lines: commit, cause, fix, numbers, what is left).
- The coordinator does not implement: it merges, resolves conflicts, reads logs and assigns work.
- Bundle verified branches into one merge-green (merge them in a worktree first, resolve conflicts there).
- Before running a gate or a long tool, an agent runs `git merge main` in its tree: the tools on main are the
  latest and fastest (cache, progress, DXC slots); an old copy once ran a 40-minute uncached shader gate.
- Shared registries cause most conflicts: allocate rtStats counters only after the last index of the table
  next to kStatsCounters in core/gi.cpp; add Python tests to the single CMake foreach list; branches write
  docs/progress/<branch>.md, never PROGRESS_CODEX.md.
- Offline first: replay pack, PT reference, fixtures, unit tests. A live run confirms one named observable,
  with its predicted delta committed before the run (predicted.json).
- Every feature that may ship is also tested in the real preset with the other qualified keys on (the
  smoke_ultra job): isolated A/B runs missed water being skipped under rt_cascade_casters, buffer overflow
  with hit materials and the scene-wait timeouts they caused.
- When a live result disproves a hypothesis or a gate was wrong, write it down in the same batch.

## Merging
- Only `scripts/merge-green.ps1`, after reading the deciding log numbers yourself. Check its own
  `MERGED <branch> onto` line, not a log grep for a merge subject (an older merge once matched).
- Never commit on main while a merge-green runs (`.git/MERGE_HEAD` exists): the commit becomes the merge.
- Features stay off by default until their gates pass. Push main and the branch after a merge.

## Writing
- Plain English with normal spacing: always a space between words and numbers ("weekly 100%",
  not "weekly100%"). Short sentences. Keep handoff and progress files concise (handoff ~150 lines).
- Progress entries: what changed, the key numbers before/after, kept/rejected/paused. No log dumps.
- Branches never edit `docs/PROGRESS_CODEX.md`: write the entry to `docs/progress/<branch>.md`
  (format in docs/progress/README.md). The coordinator folds them after merges with
  `py -3 scripts/fold_progress.py` and keeps PROGRESS at ~150 lines.
- If a user decision is needed, put the question at the top of `docs/PROGRESS_CODEX.md`, take the
  conservative option or do other work, and keep going.

## Autonomous rendering program (user steering, 2026-10-30)
- Execution contract: `docs/RENDERING_EXECUTION_PLAN.md` (the old goal prompt is in
  `docs/archive/GOAL_RENDERING_AGENT.md`). Coordinate with existing owners before delegating.
- Offline implementation and independent verification may run in parallel in isolated trees.
- Live tests require explicit user steering for the batch. Old approvals, `USER_AWAY`, absent
  `USER_PLAYING`, background/window modes and old queue-policy exceptions do not grant it.
- Prepare unauthorized jobs outside every watched `captures/queue/pending` inbox. Use the
  existing serial queue after authorization; automate A/B, toggles, captures and restoration.
- Do not ask the user to edit INI or perform A/B. Expose meaningful experimental feature
  flags in the central settings model and GUI, off in presets until qualified.
- Finish the candidate's dependent offline checks before requesting a concrete live batch;
  do not wait for unrelated offline lanes. Pending live evidence is `READY_FOR_LIVE`, not a
  quality pass. Tools must answer a specific missing observable.
- Pending or declined live permission does not stop independent offline work. Reassign
  workers across useful remaining increments; stop only at actual input/resource blockers
  or explicit user pause/cancellation, without busywork or speculative scope expansion.
