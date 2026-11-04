## hv9 visual (prepared only)
Generate gate-only changes with `python tools/live_batches/hair_validation/make_job.py --job-names hv9-visual --gates-only`.
This preserves the verified timing/readiness plan and frozen lint corpus.
Test Dialogue1, one visible_unfocused_fg launch; primary latched 1, groom 1, order 1.
Plan 66.6 s after load; with a 120 s load allowance, about 186.6 s (3.1 min).
Warm plus/base, then 60-frame reset windows: a cards (passes 0), b strands (15), c +tips/shadow_t.
Static a/b/c each export one 960-wide PNG and dumps.json pre-post float scene mean; energy is ungated.
d strands/cards repeat slow pulsed Q input (E return between), K8 pan_98900/98901; no presented-FG claim.
Use edge_flicker burst/registered_run/masks/energy with a fixed Dialogue1 hair ROI and rigid control.
Report both residuals, ungated: its full analyzer requires other windows/presented captures; reject ambiguous registration.
Expected strands <= cards: unknown, first measurement. Require swaps, zero depth orphans/refusals per strand window.
Both orphan gates set `fail_fast: false`: final per-window rejection keeps the PNG captures.
Refusal and all other gates retain immediate rejection. `check_patterns.py --logs <main>/captures/queue/logs`
audits historical patterns using queue_gates matching; positives absent from history must match rendered source emitter strings.
`hair_patterns_test` checks those six regexes in CTest; this is syntax evidence, not live qualification.
c also requires shadow transmittance participation and strand GPU ms. Final phase restores startup live keys; runner restores INI.
M2.2 e-cascade PNG/mean window is reserved in make_visual, disabled by `HV9_CASCADE = False` until its branch merges.

# Current offline batches: hv5 and hv6

No live is authorized. Generate with `py -3 tools/live_batches/hair_validation/make_job.py`.
Predictions are written before running. A commit was explicitly prohibited for this task;
commit predicted.json with the candidate before any subsequently authorized live run.
Historical hv1-hv4 remain reference artifacts and are excluded from the current dry run.

- hv5-reload-bisect: Test Dialogue1, four cumulative STAGES, one F8 each, survival
  windows only. 488.3 s plan + 180 s deduplicated readiness = **668.3 s**.
  Each stage survives 60 s or longer; strands need positive complete draw periods
  after the first quiet survival window. Header-order A/B moved to hv6.
- hv6-strand-diagnostic: Test Dialogue1, visible_unfocused_fg, primary=1 latched and
  groom=1 at startup. 433.3 s + 180 s readiness = **613.3 s**. No F8.
  Warm order 0, order 1/tips 1 and cards, then cards-a, order-prepass-1 (0),
  order-gbuffer-1 (1), cards-b, order-prepass-2 (0), order-gbuffer-2 (1/tips 0),
  tips-on (1/tips 1), cards-c, restore. Windows use the corpus floor 9.72 fps,
  rather than assuming unfocused 30 fps. Same saved player-head close-up as hair_groom;
  no rotation or zoom between captures. Review the cards trace to identify the player's
  head; a missing/occluded baseline ROI is INVALID, never silently another character.

hv6 gates: first-image load announcement before any reported strand draws, first
300-frame draw period zero, device loss/faults/refusals forbidden; per-draw coverage
in every judged strands-on window; every active item ratio >= the logged bound.
Below-bound items with zero swapped frames pass: their native cards must remain.
An empty inventory passes only with an explicit 0 draws probed, 0 swapped line.
Missing coverage still fails. Warmups are not judged quality/cost windows.
Order 1 requires zero adjacent-tag frames in every complete counted period.
Both positive G-buffer/velocity participation and GPU cost are required only when
complete coverage periods report per-draw swapped frames > 0. Honest no-swap remains
valid as a fallback observation, with no claim that strands or their cost qualified.
Eight K8 rendered bursts (serial 98600-98607), three PNG traces and scene-wait zero
in every capture window are required; the plan must finish and settings restore.

The shared queue_gates items and required_if rules retain the normal straddle rule.
A complete bad coverage line returns the existing `line(s) fail /` failure, which
scripts/live_guard.py recognizes as irreversible: auto-run aborts before the window
ends. Missing evidence fails at window end plus settling margin. The job enables
fail_fast; forbids (including first-period early swaps) abort even during loading.
The startup proof is at period resolution: it cannot identify individual draw frames
inside a period without a new producer. No layer or runner change is made here.

Offline verification (fake processes only; scratch queue names are synthetic):
`powershell -NoProfile -ExecutionPolicy Bypass -File tools\live_batches\hair_validation\dryrun.ps1 -Out <scratch>`.
It reuses lean_dryrun.ps1 with job filtering, scratch-only cache, the 900 s per-launch
preflight, 31 min total including two conservative 120 s loads, and a 2 GB budget.
Estimate: 921.6 s plans + <=360 s readiness + 240 s load = 25.4 min, ~1,430 MB.

## hv6 rendered edge diagnostic

`py -3 -m tools.live_batches.hair_validation.analyze RUN.json --out <report>`.
Manifest example (all eight captures required, unique directories, same run_id):

```json
{"kind":"hv6", "run_id":"queue-run-id", "hair_roi":[100,40,200,160],
 "rigid_roi":[200,180,300,280], "code_quantum":0.0009775171065493646,
 "captures":[{"window":"cards-a", "run_id":"queue-run-id", "directory":"pan_98600"}]}
```

The example ROI coordinates are placeholders: select the player head from cards-a,
keep it fixed for every mode, and select stationary non-hair structure for rigid_roi.
Coordinates are decoded burst pixels, not PNG preview pixels. code_quantum must match
capture encoding (the example is 10-bit code precision, 1/1023); do not infer it from
preview width. The existing burst decoder proves eight consecutive record frames and
complete K8 transport. Shared drift registration handles camera translations, requires
all frames, applies intra- and cross-window shifts, and rejects clipped hair support.
edge_flicker.analyze.energy measures the fixed head ROI in each burst. Three separate
cards bursts provide independent OFF metric repeats to scripts.qa.hair.envelope;
its Student prediction bound includes a 2*quantum floor from the second-difference
coefficients. Every order/tips candidate receives a scripts.qa.hair.verdict.
Use only stationary same-view bursts; do not claim full MV/trailing or presented-FG
qualification from this diagnostic. Existing full-suite temporal checks remain below.

## Producer findings from hc2 (2026-11-04)

00:29:52 hc2 recorded 300 G-buffer + 300 velocity strand draws per period and no
strand GPU cost line, so an all-struck/no-swap explanation is excluded for that run.
hair_primary.cpp:580 times only if g_qp exists and g_slot_frame[slot] == now+1:
CPU recording of G-buffer before this frame's publish/reset (lines 638-655) is
untimed even when the draw later executes on the GPU. No available timestamp pair
means g_ms_frames stays zero and lines 715-718 emit no cost. Order 1 applies the
header in the G-buffer but does not reset the timing ring there. No swaps (strikes,
load quiet window, coverage fallback) likewise produces no sample. Query-pool creation
failure/unavailable results are also silent: existing logs cannot prove which timing
guard failed. Smallest fix: own reset/timestamp pairs in the ordered G-buffer command
buffer, with slot readback/reuse fenced, independently of publish recording order.
This is a layer-owner fix; no layer code changed here.

Scene waits: the 00:29:52 log has 125 actual 60-frame scene-wait lines; its saved
scene-wait gate passes all reached windows, with cost-cards-b absent after the cost-gate abort.
The 21:29:59 run aborted on a silent strand producer at the first shot; subsequent
scene-wait failures are unvisited windows, not evidence of scene-wait timeouts.

---

Historical full-suite contract follows (hv1-hv4; not the current two-job dry run).

# Hair validation evidence contract

This is a fail-closed offline analyzer and prepared live regression batch. It
does not claim the current dump transport can supply every requested observable.
See `docs/HAIR_VALIDATION.md` for the missing runtime-owned data. No game launch
is authorized by generating jobs or by passing a fake-queue dryrun.

The strand participation producer logs every 300 rendered frames
(`layer/hair_primary.cpp:560-564`, `now % 300 == 0`). Gates declare
`period_frames: 300`. At visible-unfocused 30 FPS, each 750-frame cost window
(25 s in the runner schema) permits a complete interval after the skipped first
line and settings latency. Before any measurement, both primary values run for
300 frames each (cards, then strands), preserving the 600-frame warmup budget.
Mesh-pool-full lines describe a normal capped pool and remain collected in
`patterns`; they are not global job forbids.

`RUN.json` uses paths relative to itself and these fields:

```json
{
  "run_id": "unique queue run ID",
  "game_log": "run.bg3rtx.log", "runner_log": "run.runner.log",
  "validation_log": "validation.log",
  "pipeline_identifiers": ["hair_primary", "our exact debug pipeline handle"],
  "required_windows": ["shot-strands-a", "shot-strands-b"],
  "observed": {"characters_enter_leave": false, "lod_transitions": false,
               "repeated_toggles": false, "smoke_ultra": false},
  "windows": [], "performance": []
}
```

The required inventory comes from the predicted job, including both ON windows,
all observed LODs and post-reload frames. `observed` entries require reviewed log
and capture provenance; merely having a phase named zoom is not evidence of LOD.
No pass is possible with the example's empty arrays/false observations.

Each `windows` entry has `name`, `run_id`, `registered`, `arrays` (NPZ path),
`depth_precision_m`, `linear_quantum`, `code_quantum` and `repeats`.
Repeats contain >=3 independent stationary OFF/OFF metric samples for
`coverage_loss`, `iou_loss`, `energy_delta`, optional `pt_error`, and separate
`rendered`/`presented` dictionaries each with `reprojection`, `trailing`, `flicker`.
Noise is assumed stationary Gaussian for Student-t bounds; animation/camera cuts
must be registered/excluded before measuring repeats. Do not use three copies of
one image or correlated adjacent pixels as independent samples.

NPZ arrays: baseline `character_ids` (positive stable IDs; zero background),
`cards` (baseline hair support from material class 5/hair mask), `strands`
(visible candidate strand mask), `visible_cards` (candidate native card mask),
`strand_depth`, `scalp_depth`, `footprint_m` (depth tolerance contribution),
`cards_linear`, `strands_linear` (pre-post scene luminance), optional `pt`.
Masks must have identical view/resolution; exclude occlusion identically.
Never derive strands solely from G-buffer class 5: native cards share that class.
Missing player hair must remain in the baseline inventory.

For each of `rendered` and `presented`, provide `_frames` (N,H,W,3 PQ codes),
`_masks` (N,H,W hair support), `_character_ids` (H,W), `_mv` (N-1,H,W,2),
`_disoccluded` (N-1,H,W), `_fresh` (fresh current reference, no history),
`_registered` (registered frames for residuals). N >=3, no cuts, same-run only.
MV units are **backward render/output pixels**, respectively: sample previous
at current pixel plus MV. Decode scale/Y sign explicitly. Out-of-image support
is excluded, never wrapped. Generated presents need their own proven MV mapping.
Extract mask/depth/material from `dumps.json`/pan samples using `scripts/qa/decode.py`;
retain raw precision and material bits. Previews alone cannot certify this input.

PT images should come from a matched replay pack using `frame_replay --pt-reference`
(`tools/frame_replay.md`, `tools/pt_reference.hlsl`), with camera, materials,
lighting, hair geometry/BSDF and exposure identical. The generic replay PT does
not automatically certify the new strand BSDF; absent matched hair PT is INVALID.

Performance entries: `characters` (observed 1/5/10), `window`, `exclusive`,
`diagnostics`, `contention`, `frame_ms` (>=100 actual rendered samples),
`frame_budget_ms` (=1000/declared target Hz), `hair_gpu_ms` from existing hair
cost lines, `off_gpu_ms` (>=3 repeat samples), `timer_quantum_ms`, `vram_mb`,
`device_budget_mb`. Cost gates permit no statistically significant regression
against OFF repeats; brightness gates must also pass. Amortized GPU ms/count
is explicitly labelled, never presented as isolated per-character timing.

The budget is <=2 GB: 15 compact traces at estimated 30 MB, six bounded edge
K8/P16 pairs at <=165 MB each, plus logs. Inspect measured size before staging.
Half-size edge captures/960-wide previews cannot qualify subpixel silhouettes.
Keep summaries and inputs still in use; do not silently discard failed windows.
