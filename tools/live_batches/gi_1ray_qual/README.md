# One-ray GI Ultra qualification

Prepared job: `jobs/gi1-ultra-1ray.json`. No live authorization and no preset change.
The task's git metadata is read-only; no branch creation, merge or commit was attempted.

S8 (`Water Queens House`) is the worst *evidenced* one-ray flicker case among these
two saves: sh1/sh2 already exposed a failing OFF-repeat flicker null on its shaded
stone/roof transitions. Camp's higher GI trace cost (7.36 versus S8 4.31 ms offline)
does not establish worse flicker. There is no comparative camp flicker receipt.
This choice targets the demonstrated acceptance gap rather than claiming a new ranking.

Read `predicted.json` before running. The generator reuses the sh2 GI A/B job,
the shared `live_flow.tighten_files` duration/readiness path and the compact K8
transport from `edge_flicker`. It starts with the current release `preset=ultra`,
RR, FG 2 and shipped two-ray GI. It does not carry sh2's historical feature-disable
overrides. Shader request delta: 0; no settings, ABI or shader edits.

One launch has four blocks: **OFF / ON / OFF / ON = 2 / 1 / 2 / 1 rays**.
Each block has capture-free GI trace timing, three static PNG/stat traces, a K8
burst during a three-second held Q turn, and a three-second held E return.
All other rendering settings stay on the release profile. Both ray counts are
warmed before measurement; exposure is held throughout measurement and released
at restore. The queue also restores its prior automatic INI on exit.

The analyzer gates both ON repeats, not a selected best window:

- Existing 300-frame GI trace GPU medians in every cost window, with correct ray
  count/full resolution/RR participation; report each state and both savings.
- Float pre-post `5c3fdabc` scene-colour means from `dumps.json`, normalized by
  trace-frame exposure and averaged over all three samples per state. Existing
  `m4_promotion_gate.energy(pool='run')` derives the Student-t band from this run's
  OFF repeat. Both ON means must preserve energy within that band.
- `scripts.qa.detectors.temporal` on static final PQ frames and registered pan
  final PQ frames. Reuse `sharc_far/rays_flicker.py`'s directed OFF-repeat floor
  rule. Both ON sets are tested against both OFF sets. INCONCLUSIVE is INVALID.
- Every shot has three completed traces; every pan has one complete K8 transport.
  Scene-wait and capacity checks cover every cost/shot window. A zero-tolerance
  event gate checks all reported scene waits in every measured window, including
  the short pan windows; those need no invented periodic sample guarantee.

Pan registration reuses `edge_flicker.drift.register_window` (integer translation,
no interpolation). All eight frames, cross-window rigid consistency, finite common
support and measured camera displacement are required. Albedo endpoints use QA's
animation mask. Rotation/parallax that cannot be registered makes the job INVALID;
it is never accepted as lower flicker. K8 does not contain normal/sun endpoints;
static traces retain QA's full albedo/normal/sun animation mask. No residual or
new temporal metric substitutes for the named flicker detector.

The current corpus gives 225.9 s scheduled + 0.027 s expected readiness + 120 s
load = **345.927 s (5.77 minutes)**. The runner's `max_minutes=5.9` caps actual
execution at 354 s including load. Its larger generated `timeout` reserves cold
readiness for lint; `test-queue.ps1` clamps it to that cap. A slow load/build aborts
instead of extending the launch. Captures: 12 traces x 60 MB + 4 K8 x 165 MB +
10 MB logs = **1390 MB**. No full K16 bursts or presented FG captures.

Offline commands (none launches the game):

```powershell
python -m tools.live_batches.gi_1ray_qual.make_job
python tools/live_batches/live_flow.py tools/live_batches/gi_1ray_qual --preflight
python -m unittest tools.live_batches.gi_1ray_qual.test_analyze tests.batch_jobs_lint_test
python tests/qa_detectors_test.py
powershell -NoProfile -ExecutionPolicy Bypass -File tools/live_batches/gi_1ray_qual/dryrun.ps1
```

The job's gated post command runs `python -m tools.live_batches.gi_1ray_qual.analyze
<log>`. It writes sibling `.gi1.json` and `.gi1.md` receipts with PASS/FAIL/INVALID,
all temporal comparisons, energy band, float samples and per-state GPU savings.
For relocated captures, use `--captures-root <directory>` and optionally `--out`.
Historical replay is unproven for this new four-state plan; offline validation
does not prove rendering quality. If it passes live, promotion remains a separate
settings change. Review shaded stone and roof transitions in this save for stippling.

Final outcome (2026-11-04): one-ray Ultra GI rejected. GI trace saved 3.27/3.50 ms; energy -7.8%/+3.9% stayed inside OFF spread 11.6%, but ON-b static flicker failed (PQ excess 0.0032/0.0022 > 0.00098). Ultra keeps two rays. See [GI outcomes](../../../docs/TRACK_C_GI.md) and [backlog](../../../docs/BACKLOG_20261104.md).
