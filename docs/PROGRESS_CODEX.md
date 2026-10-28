# BG3RTX progress

## Evening summary — 2026-10-28, 22:20 Europe/Rome

1. Main 6dba0a8 adds the corrected local-light reference and complete merge checks; P1 is not merged.
2. P1 remains open; history rejection is parked on its branch, default off, after decisive same-frame energy loss.
3. P1 uses one moving run with live toggles and per-run reprojection; independent trajectories are not image comparisons.
4. The point-footprint correction removes unused-neighbour rejection; pan coverage is 13.4% off / 7.15% on.
5. Paired point-gate raw bounce loses 70.7% / 88.0% energy, while captured alpha matches exactly; no quality win.
6. Latest checks: 36 helper checks, 6 surface shaders, 20 ABI checks; 7 ordinary shaders remain byte-identical.
7. P3 box-light reference job 250 is running, hold90; default-off integration passes offline build/3 CTests/34 shaders.
8. P4 OMM and P5 SHaRC/material consumers remain unapproved; EN2 and emissive failures are documented.
9. P2 reference merged after two fresh hold-90 passes; EN2 and one D-EM family still fail and remain excluded.
10. P6 water minimum design is merged at 441904a; forward-water rendering and coverage gap 1 remain open.

## Operating plan

Work until Saturday, 2026-10-31 evening. Use 20:00 Europe/Rome as the stop target;
finish with a summary here, push all resulting branches and main, then stop.
No user decision is currently needed. Never modify saves, rewrite history,
force-push, or move/delete any snapshot-20261027/* tag.

Before starting a runner or game, check both the USER_PLAYING marker and live
BG3 processes. An unowned game or the marker means offline work only. In-game
tests use the serial hidden queue and explicit expect patterns. Feature merges
use merge-green only after manually checking the deciding log numbers, followed
by pushes of main and the branch. A phase branch starts from current main and
imports useful old work selectively. New features remain off by default until
their quality gates pass.

## P1 — Pan instability

P1 temporarily parked, not completed: phase head 1a57286 has PAUSED.md with
evidence and next intervention; all research keys remain off, no main merge.
Run 249 is valid, 217 s, zero driver events/dumps/build changes. In the two
same-frame ordinary/point-gated raw pairs, GI Y falls 70.66% / 87.96%:
0.013646539 -> 0.004004193 and 0.016753490 -> 0.002016792. Alpha bits match
over 2,731,520 pixels per pair, all finite, pose valid and eleven GPU cases pass.
Normal rejects 52.35% and depth 22.46% of reported queries (startup included);
these counts are not an energy attribution. The low pan residual is not a
quality result. Next return: a physically valid fresh-source fallback on
rejection, plus spatial normal-footprint handling; no tuned match threshold.
P3 now receives its first runtime checks while the overall goal remains active.

Point-footprint run 248 / 409ffe1 is valid: 403 s total (344 s loading), 57 s
single moving session, zero driver events/dumps/build changes. Off/on residual
0.151383524 / 0.064525116 (-57.38%); sample GI Y 0.321177270 / 0.008689996
(-97.29%). Grid coverage 13.3877% / 7.1512%, or 14.5655% / 10.1579% of the
static-compatible moving samples. Motion 98.73 / 51.33 render px. Broader
coverage makes the measurement more useful; unequal cohorts and lower sample
energy still prevent a visual-quality claim. No cross-run image comparison.

Offline paired exports from jobs 242/243 answer the earlier energy concern:
within each individual frame, raw legacy/gated bounce Y falls 71.81%, 99.83%,
99.88%, 99.86% over the four captures. Alpha bits match exactly over 2,731,520
pixels each, both outputs finite, no brighter gated pixel. This is causal
same-frame evidence against the OLD five-texel candidate, not final-image
brightness or a comparison between runs. Phase evidence JSONs preserve inputs.

P1 head d8b204f is pushed. Job 249 is running: repeat the existing paired
private capture on the corrected point-footprint code, headless diagnostic,
two same-frame legacy/gated pairs. AO counter zero is not required. This test
isolates energy loss; it cannot close the moving-camera quality requirement.

Production repeat 247 (b5e0e93, diagnostic AO comparison off) is queue-valid:
130 s total, 56 s one moving session, zero driver events/dumps/build changes.
Residual off/on 0.127411757 / 0.046098715 (-63.82%); sample GI Y 0.019353448 /
0.005471711 (-71.73%). Coverage 0.11889% / 0.02111%, motion 88.65 / 41.56
render px. These unequal cohorts and lower energy do not establish quality.
Evidence: P1 branch docs/evidence/p1-surface-pan-production-247.json.

Job 246 is INVALID: no pan activation in the lighter loaded scene (876–1413
draws). The helper incorrectly required >2000 draws even after authoritative
post-load stability. b5e0e93 uses the layer's scene-ready or automatic-capture
signal, requires the saved-load transition and completed baseline, and retains
the no-Save fallback. All 36 offline helper checks pass; 247 proves actual pan
activation. No desktop-resolution or scene draw-count threshold for saved runs.

P1 409ffe1 corrects another concrete overrejection: all colour/history reads are
integer point loads, but the candidate validated four additional unused texels.
It now requires depth AND normal on the actual fetched texel, with no neighbour
rescue and unchanged tolerances. Six surface shaders and all 20 ABI/SPIR-V checks
pass; seven ordinary GI/RF binaries remain byte-identical to ac77150.
Job 248 finished with the one-session production toggle protocol above. No new
camera transport, AO diagnostics or cross-run image comparison in that pan.

Latest user steering: stop cross-run image comparisons. Use per-run GI
reprojection residual or live off/on alternation within one moving run. The AO
counter discrepancy is noted and compared with the same-build key-off control;
it does not block the first pan measurement.

First pan number: job 245 on bc15dd3 DLLs, valid, 149 s including startup / 65 s
session, key alternated every four seconds in ONE hidden moving game; zero
driver events, dumps or build changes. Sample-weighted normalized GI luminance
reprojection residual off/on is 0.123120933 / 0.044079878 (-64.20%); RMS is
0.347363809 / 0.209209448. Sample mean GI Y is 0.019496607 / 0.008939488 (-54.15%).
Only 29,136 / 59,473 samples pass the static-compatible/moving-surface filters,
0.07653% / 0.07900% of tested grid samples. Mean displacement differs too,
119.41 / 260.65 render pixels. This is a limited cohort and lower GI energy,
not an established visual improvement: keep the candidate off, do not merge.
AO counter differences occur in BOTH modes, 11.14% off / 9.37% on. The next
step is a production-metric repeat without the diagnostic, then investigate
normal-footprint overrejection and energy loss. No additional camera transport.
Branch codex/p1-pan e8ff955 is pushed. The metric uses native B2/B5 projection,
verified camera round-trip, paired previous GI, GPU mode buckets and a rotating
4x4 diagnostic grid; GI rendering stays full-resolution. Evidence and the
repeatable phase plan are committed under phase docs/evidence/ and tests/fixtures/.

Moving A/B now captures actual motion in both configurations. Jobs 240/241 use
identical d431702 host/core DLLs, gi_current_radiance off/on, headless=0: valid
85/74 s, zero driver events, dumps or build changes. In the last interval albedo
changes 83.38% / 79.68%; both cameras move. Their final views differ by 80.54%,
so this is not a matched-surface quality comparison: no GI improvement is
established and P1 remains open. P1 build/moving-current-ab-r11.json keeps numbers.
The -Move fixture isolates physical desktop input with a virtual pointer and
consistent SDL motion events, using the actual game client dimensions. This
addresses desktop-resolution dependence; normal play is unchanged. Moving QA
defaults to real presentation while remaining hidden by the serial queue.
All 10 CTests, six capture/resolution tests and 32 log-policy checks pass.
Earlier focus experiments had no effect and were reverted. Real presentation
alone was insufficient (237 moved off, 238 stayed static); do not blame headless
mode from those confounded controls. Branch codex/p1-pan ac77150 is pushed.
Implemented gi_history_surface_gate=0 on P1, independently of current-radiance
replay: accept bounce history only when previous depth AND world normal match.
Paired GPU metadata, conservative footprint intersection and derived precision
bounds apply to bounce RGB and half-res RGB accumulation; visibility keeps its
legacy predicate. 11 CTests, 45 shader files and 20 ABI checks pass; seven ordinary
GI/RF shader binaries are identical with the key off. No new camera transport.
Smoke 242 completed in 210 s without events/dumps, but is INVALID: aggregate AO
counters report 8–10% differences. Both exported same-frame alpha pairs instead
match exactly over 2,731,520 pixels each; both GPU predicate tests pass eleven
cases. Unlimited VVL names no logged private handles or the metadata buffer.
The discrepancy also occurs in repeat 243 with native geometry math (92 s,
zero events/dumps). Job 244 ended before the old 120-frame report window;
its invalid contract also expected a startup marker outside the session slice.
Job 245 uses short windows and the corrected session contract; see numbers above.
Details: phase docs/GI_HISTORY_SURFACE_GATE.md and build/surface-smoke.json.

The last production S3 result predates the temporal sample-coordinate safety
correction. It measured temporal excess areas of GI 118.672, local 55.052,
sun 42.088 and reflections 34.557 per mille, with 23.0% common motion excluded.
Those numbers remain the baseline, not proof about the current candidate.
The fresh phase branch codex/p1-pan at 03679ed excludes unrelated FG work.
Production diagnostics were off for p1-gi-coordinate-candidate-S3: queue-valid,
356 s, no device loss, dumps or build changes. GI excess area is 172.938 per mille,
mean temporal excess 0.001258 PQ against 0.000978. All-RT excess is 219.062 per
mille, mean 0.002334 PQ; 24.0% common scene motion is excluded. This fails and
does not establish a reduction. The older baseline is not a controlled same-build
A/B, so these numbers do not prove a regression either. No merge was made.

Coverage gap confirmed from scripts/qa/capture.py: --temporal-gi does not request
-Move, and the same_view gate rejects moved views. These S3 numbers measure
fixed-camera temporal instability, not a direct pan comparison. Keep this as an
anti-regression gate, then require separate moving-camera off/on captures with
actual camera-motion evidence before claiming a pan improvement or closing P1.

Owned velocity joined 6,485 copies, with 5,066 late resolutions and no recorded
late-path rejection reasons. The last GI window reserved 300/300 descriptors,
with zero unreserved frames. Correct transport alone did not resolve the failure.
Next hypothesis: previous-frame bounce radiance still carries previous visibility
and feedback. Assess a default-off current-frame diffuse source after native
lighting, preserving pre-lighting visibility and freezing its camera/settings.
The candidate is implemented at 3c7b411 with paired host/core ABI 14 and default
gi_current_radiance=0. Full build, 10 CTests, 39 shader files and 14 ABI/SPIR-V
checks pass. A follow-up at 9c68f9e keeps seven ordinary GI/RT shader binaries
byte-identical to bf170f8; P3's default/check local-light shaders are also
byte-identical to main with boxes off. These are compatibility checks, not proof
of visual improvement.

The first current-radiance smoke and same-build off control both stalled before
gameplay (82 s each, zero device-loss events/dumps, no build changes). No GPU
candidate evidence was produced. A concrete porting error held tracker.mu across
a helper that reacquired it: c428cde releases the lock before the metadata bind
observation, as the tested research branch did. All 10 CTests pass after the fix.
The direct-helper unit test missed this native callsite. The fresh off control
now completes validly in 85 s, with zero device-loss events/dumps/build changes.

Current-radiance smoke-r2 completes two captures in 89 s. Both export all nine
private writers and a GPU-valid pose header. Actual RGB changes on 323,905 /
326,814 pixels, with mean absolute differences 0.0079245 / 0.0078829; raw and
temporal alpha match PRE exactly and all outputs are finite. No VVL messages
name the 57 exported private handles, but the validation duplicate limit was
the default 3: suppressed IDs can hide later messages, so this is not a full
hazard clearance. Evidence: the P1 branch build/current-smoke-r2.json and saved
validation log, captures 20261028-191549 / 191600.

That smoke has one nvlddmkm event 153 at 19:16:02.671, before decode finishes;
rendering continues afterwards, with no VK_ERROR/device-fault log or dump. Its
cause is unresolved; do not attribute it to teardown or declare stability.
A production repeat without validation/private readback completes validly in
79 s with zero events/dumps/build changes, 1,200 accepted captures and no reject.
This proves execution, not visual improvement. No P1 merge has been made.

Same-build static A/B started at c428cde with diagnostics off. The first off
measurement is GI 144.405 per mille / mean 0.001571 PQ, still outside the gate;
all RT 210.329 / 0.002801 PQ. Its queue status is INVALID because the expectation
incorrectly searched for QA stdout in the kept game log, although all 28 traces
and result.json completed. Do not use it as the deciding queue pass. The repeat
checks the actual game-log completion marker and is queued/running as
p1-current-radiance-ab-off-S3-r2. It completes validly in 338 s, with GI excess
150.059 per mille / mean 0.000988 PQ (limit 0.000978), all RT 226.224 / 0.002580.
It also has one NV153 while the candidate is off; that still does not identify
the event's cause. No further fixed-camera on run is scheduled ahead of motion.

User steering: moving-camera off/on on the SAME build takes priority, and static
scores cannot close P1. QA --move is implemented at 3ff190f: it forwards -Move,
keeps each trajectory's settings constant, retains frame provenance/dimensions,
requires --capture-only and rejects static analysis. Six resolution/capture and
five temporal-plan tests pass. Moving off job 227 is running before the matching
on run. The next GI hypothesis is a default-off depth AND normal history
rejection at the reprojected previous pixel, before any more transport work.

The first moving pair is queue-valid on the same 3ff190f build (142 / 80 s),
three traces each, zero NV events/dumps/build changes. Actual albedo movement
is observed, but the trajectories do not match: off shifts about (-40,49) then
(-2,81) preview pixels; on shifts (-2.87,-2.20) then nearly zero near the scene
boundary. No numerical quality improvement is claimed from this pair.
a8c5db5 starts the pan only after a saved-view baseline trace, retaining the
gameplay-ready guard. All 32 log-policy and six capture/resolution checks pass.
Renderer DLLs are unchanged; their hashes are saved before the synchronized
repeat. Job 229 p1-moving-current-off-S3-r2 is running, followed by its on pair.

Next implementation proposal is gi_history_surface_gate=0: previous depth and
world normal metadata paired with the radiance writer, using existing B2/B5
camera data, with conservative precision/footprint checks. It must gate the GI
bounce source as well as temporal accumulation, since RR uses history weight 1.
It is not implemented yet. Missing/invalid writer data must reject, and any
dimming or excessive rejection must be measured rather than called a pan fix.

Owned-motion tests reject offscreen and nonfinite coordinates on the GPU.
In four NaN/Inf captures, temporal output equals the fresh sample exactly and
all ordinary outputs remain finite. This establishes numeric fallback safety;
it does not establish a visible pan improvement or correct previous visibility.

Research camera runs on 2026-10-28 established binary transport and actual
camera movement. The additional B1/B2/B3/B4 camera reads remain off pending
guarded implementation. A wider validation check found an introduced overlap:
copy/fill of metadata followed by a write to its last 48 bytes. Separate write
ranges fixed it. Static/moving repeats on 929be7a are queue-valid (116 / 126 s),
with no device-loss events, dumps or build changes, and no validation messages
naming the 3,255 / 3,345 logged private handles. Raw camera transport, checkpoint
completeness and paired depth checks still pass. The complete camera verdict
remains inconclusive because the additional payload is deliberately absent.

This research is retained, not merged. The phase branch will exclude unrelated
FG/HDR/swapchain work. Source recording identity and actual GPU history epochs
are different; no history acceptance is certified from a present counter.

User check when a P1 candidate is ready: pan across static building and ground
edges in the usual save, comparing the candidate's GI key off/on. Look for light
or shadows sliding away from those fixed edges. Ctrl+Shift+F9 switches all RT
effects off/on as the reference. Do not treat wind-blown grass alone as a failure
or use all-RT-off as a claimed fix. No new user-facing candidate key is ready yet.

## Next phases

- P2: the useful lscheck fix is merged at 6dba0a8 and pushed with codex/p2-pending. Fresh valid S6 hold-90 runs have 25 windows each and zero any-bit, relative or one-writer differences across 8,172,707,324 / 8,172,707,294 pixels; no faults, dumps or build changes. The first refresh pair was invalid because a startup-only expectation was incorrectly applied to session slices; valid repeats supersede it. Full startup independently confirms the real 8.0 MB asset DB. Details: docs/P2_PENDING_MERGES.md.
- EN2 pure S8 windows join 300/300 without waits but still fail blue/metalness and luminance bias. D-EM family 304be1c7 fails AF at ratio 0.361 / 0.362 (band 0.842–1.187); GF passes at 0.999. Both remain excluded.
- P3 offline preparation: codex/p3-box at 907857c is pushed, using the managed p2-pending checkout. The useful shader foundation is ported onto current main; host pipeline selection, native descriptor replay, cleanup, default-off ls_box key and projector validation logging are implemented. The stats allocation now covers added counters 136–150; the inherited 136-counter allocation was insufficient. Full build, 3/3 CTests, default agreement and 34/34 shader files pass. No runtime or merge yet; S6 / S7 / S5 recompute and shadow checks follow P1's runnable candidate. See that branch's docs/P3_BOX_LIGHTS.md.
- P4: implement and validate the conservative OMM foliage design.
- P4 design review found a correctness gap before implementation: the old mip-0/1 hard-cut bake can mark constant alpha 0.75 opaque, while alpha_eval at LOD 2 uses a stochastic threshold and accepts only about 75% of rays. This is bias, not equivalent noise. Known micromap states must respect all supported runtime thresholds/mips or remain unknown; the ignored P3 build/P4-alpha-review.md records the counterexamples and endpoint issue. No OMM source was changed.
- P5: SHaRC v2 after EN2, then the light tree and EN2 consumers.
- P6: at least a water design covering the docks gap.

If P1 stops yielding measurable progress, record state, evidence and the next
idea in PAUSED.md on its branch, move to P2, and return later. Apply the same
rule to later phases. Do not substitute more instrumentation for deciding tests.

## Small fixes and gate gaps

- Resolution checks now use actual source and preview dimensions; changes make comparisons inconclusive and retain captures.
- The window hider checks PID plus process start time and retains a process handle through UI use; 13 policy checks pass on PowerShell 5 and 7. The latest live queue game also verified matching ownership, hidden style and alpha zero.
- Fixed the incomplete merge gate: build.ps1 -Test runs all CTests and rejects an empty suite; merge-green checks the main branch, playing state and merge result, compiles all shader entries through the shared compiler, and aborts its own failed merge. Real disposable Git fixtures pass invalid-ref, build/shader/QA failure, successful merge and already-merged cases. Integration passed 3/3 CTests, 32/32 shader files and QA detector tests. The P1 branch separately passes 8/8 CTests before the in-progress GI changes.

Research evidence remains in the managed pan-motion-vectors worktree and main's
ignored queue logs. Snapshot tags and backups are unchanged.
