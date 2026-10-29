# BG3RTX progress

## Resumed — 2026-10-29

266 geometric static12 FAILS off/on noise parity: +0.46644%, CI
[+0.000041845,+0.000119520]Y. Pooled PRE target includes0 but cannot override
that failure. Valid204s, maxshift0.08px, finite/pose/alpha pass, no faults/dumps/
build changes. No reconstruction or threshold change is justified yet.
P1 bc71cb3 adds diagnostic self-test mode2: reserved pose row12 measures frozen
B1-ray versus native B2 projection at two render-relative pixels and classifies
agreement with known current/previous jitter. PRE AND POST checkpoints compare
the same frame; packed24-bit integers survive float exports exactly. Existing
bindings5/48/50/51, Push256, actual-submit pins, no new camera payload or image
change. Build11 CTests, shader/24 ABI checks pass.268 LIVE three static captures,
gate0; do not infer which matrix is jittered or MV sign from an offset alone.

267 first P3 activation valid291s/219s session, zero driver faults/dumps/build
changes; native set3 binds with2 dynamic offsets, projector evaluations nonzero
(last1.319/px). Accuracy INCONCLUSIVE: reported game/recompute means0, while fx
quantizes raw radiance at1/256 and native scale0.00136865. Zero counters do not
prove black rendering or agreement. VVL full log retained; contains native/SDK
format/event/layout categories, no set3-naming message; no clean certificate.
Debug4 did not exercise shadow rays. P3 f63da03 pushed/default off; next recover
sufficient projector-stat precision or raw signal before assessing accuracy.

265 geometric-normal smoke valid111s, no driver faults/dumps/build changes.
BOTH GPU self-test rows[32767,32767,15,1]:15/15 pass. Static shift0.06px,
finite GI, PRE alpha exact. Off Y0.017494613 vsPRE0.017495760;45/2,731,520
RGB pixels differ in this diagnostic run (do not call control bit-exact).
On Y0.017680915 vsPRE0.017518661 (+0.926%); two captures are not a noise pass.
One on source window:39,157,093 queries,25,108,223 history,14,040,564 fresh,
8,306 original-unavailable. VVL2667 existing-category sync messages; zero
compute-dispatch/private-source/new-metadata messages, scoped only.49/49 shader
files compile/validate after sharing the origin declaration. P1 head82b8528
pushed.266 LIVE frozen twelve captures without self-test or VVL overhead.
If depth rejection/energy bias persists, compare the predicate's plain ray_at
position with the pan metric's existing native-camera round-trip correction;
reuse existing matrices, no new camera payload/empirical threshold/brightness fix.

264 P3 DEFAULT-path baseline PASS: valid165s/104s session,24 complete windows,
7,844,924,963 reference pixels, ZERO any-bit/relative/one-writer/join differences,
no faults/dumps/build changes. c039f87 evidence pushed; FULL-variant250 still
open, no box-on run or merge. Queue was used while preparing the next GI fix.

262 exact-source static: valid203s, maxshift0.0601px, finite/pose/AO pass. ALL
six off frames have zero PRE/POST RGB differences (2,731,520px each). Off/on
mean-0.20675% is within paired noise, but original-PRE reference FAILS pooled
+0.015598%, CI[+0.0000009073,+0.0000044876]Y; on consistently+0.0311..0.0314%.
No brightness correction or gate relaxation. Exploratory263 source-toggle pan
valid112s/52s, no faults: Y residual+2.596%, RGB+2.539%, sampleY+3.942%, motion
76.57/79.55px, coverage5.033%/6.675%; no gain or exact-source promotion.

P1 caae9b0 fixes the original availability cohort in BOTH modes: fresh fallback
replaces existing colour rejected by surface checks, without adding edge samples
where the original had no finite colour. b6a1c84 adds default-off geometric
normal identity using the existing owned rtOrigin image40, only matched TLAS
hits. Shading detail can vary on static geometry below a point footprint; depth
AND geometric normal still must match, with propagated fp16/oct16/fp32 bounds,
no angular fudge/neighbour rescue/new camera data. Metadata certifies normal
mode on the GPU. Full build/11 CTests, nine shaders/24 ABI checks, ordinary
seven shaders identical;15 GPU cases await runtime proof. 265 LIVE unlimited
validation smoke with self-test and static gate off/on; all other knobs isolated.
Specs/evidence on P1, keys0, no main rendering merge. Next runnable GI test keeps
queue priority; P3 box-on validation is ready after its passing baseline.

261 exact-diffuse smoke valid117s: zero driver faults/dumps/build changes,
finite/GPU-pose/PRE-alpha pass, static shift0.12px. Off/on Y0.017292882/
0.017589541, PRE0.017282156/0.017571717; two samples are not an energy pass.
Unlimited sync validation reports2647 existing renderpass/NGX-category hazards,
zero dispatch hazards or messages naming21 private source handles/new metadata.
Scoped evidence, not a clean VVL certificate; raw log retained on P1.
066e59b fixes a control confound: gate0 no longer substitutes fresh lighting
when original history is unavailable; gate1 still tries fresh on every rejection.
Exact-source key can now be isolated without adding edge samples. Static exports
also report PRE/POST RGB bit differences. 262 is the frozen twelve-capture
exact-source static run, surface gate0, original-PRE target retained.
P3 ec68966 (offline): reference fixture now explicitly selects default path
(ls_stats=0,ls_samples=0,ls_debug=0), matching accepted P2 controls. FULL-variant
bit differences from250 remain open; no threshold relaxed/runtime pass claimed.

260 four-ray diagnostic: valid118s/52s session, zero faults/dumps/build changes.
Within-run off/on residual0.281396/0.260002 (-7.60%), but cohort GI Y0.155257/
0.015238 (-90.19%), motion74.61/80.65px; off has strong radiance spikes. No
quality win. Both-zero normalized residual is not a monotone variance estimate
as ray count changes; no cross-run image or noise-reduction inference.

P1 bd4215c adds an INDEPENDENT default-off gi_history_diffuse_exact hypothesis:
legacy RGB*(diffuse_Y/total_Y) preserves source Y but mixes view-dependent
reflection colour into GI. Exact complete diffuse RGB now has its own metadata
record, retaining complete radiance for RF/control and fresh same-source fallback.
Requires gi_history_fresh=1; surface gate0 isolates it. gi_pan_metric=2 buckets
this source key; same-cohort RGB residual complements Y. No extra ray/descriptor,
weight/count change or camera transport. Metadata stride3 stays fixed across
source toggles; range guard remains. Full build/11 CTests, eight affected shader
variants, 24 ABI checks, five energy regressions and seven byte-identical ordinary
shaders pass. 261 is the frozen two-capture unlimited-sync-validation smoke;
then original-PRE/static-energy and ONE moving source-key test if valid. All keys0.
Spec/evidence on P1: docs/GI_DIFFUSE_EXACT.md; no main rendering merge.

259 ONE-run balanced moving test: valid112s/52s session, zero faults/dumps/
build changes. Off/on residual0.134896214/0.144665732 (+7.242%); mean GI
Y0.015967731/0.016094788 (+0.796%); motion68.822/71.812 render px. Cohort
3,532,908/4,600,302 samples, grid coverage4.869%/6.822%; compatible-moving
coverage7.461%/8.514%. The fresh complete source avoids massive darkening,
but increases raw residual: NO pan-quality win, main merge or default change.
Unequal cohorts/motion and fresh sample noise limit interpretation; raw GI
is not resolved RR output. The small static conditional bias remains open;
run258's wide CI does not erase run256 or its other +0.50..2.06% samples.
All evidence committed/pushed on P1; queue terminal. Next separate fresh noise
from resolved-image instability using same-run evidence, no new transport or
cross-run picture comparison. One authorized usage reset remains unused.

258 / af093ae completed valid149s: all twelve exports, stationary max0.18px,
finite/pose/AO pass and zero faults/dumps/build changes. Both energy CIs include
zero: POST/PRE -0.0292%, CI [-0.000282538,+0.000272094]Y; off/on +6.975%, CI
[-0.001305235,+0.003713217]Y. POWER IS LOW: one true light pulse broadens off/on
relative CI to roughly [-7.56%,+21.51%]. Other on captures remain +0.50..2.06%
versus PRE; do not claim the smaller bias is solved. 257 was incomplete (five
late POST exports exceeded 2048MiB), not an energy pass; fixture now4096MiB.
Job259 results are above: balanced AB/BA toggles/directions every4s, private/AO
diagnostics off; no defaults/merge approval. Full evidence remains on the P1 branch.

Complete-source run256 execution valid149s, stationary max0.06 render px,
finite/pose/alpha pass, zero driver faults/dumps/build changes. Energy checks
FAIL: on/off +1.0583%, paired CI [0.000003457,+0.000362817]Y; POST versus
original PRE +0.5418%, CI [0.000011949,+0.000175550]Y. The large darkening is
gone, but this is not noise parity or a P1 quality pass.
P1 ccb1f65 additionally mirrors original composite spectral diffuse-share
reconstruction and fp16 storage; fresh replay now follows RF so its specular
source is complete. No empirical energy multiplier. Full build/11 CTests and
two changed trace shaders pass. Job257/258 results are above. Balanced moving fixture is ready; keep all keys off.

Run255 completed valid in 200 s: all twelve captures stationary (maximum shift
0.16 render px), finite GI, valid GPU pose, unchanged PRE alpha and zero driver
faults/dumps/build changes. Direct-only off/on parity was -0.807%, paired 95%
CI [-0.000601951, +0.000329959] Y. This is PARTIAL evidence: both controls used
a different integrand than the original complete composite source.

P1 d53f3e1 restores original complete history in the control/accepted branch;
rejection now combines fresh native diffuse with the immutable PRE GI at the
source, matching material/strength/skin conventions. No dropped weight/count.
The energy gate additionally checks same-frame POST versus original PRE; equal
loss in both modes cannot pass. Five regressions, 11 CTests, four changed shaders
and 24 ABI checks pass. Metadata shrinks to surface/optional metric records.
Job256 results are above. Only a pass of both energy checks permits the next
ONE-run moving toggle measurement.
All research keys stay off; no P1 main merge or pan-quality claim.

Static fixture254 / 8ec52c4 now proves stationary capture: albedo shifts only
-0.04 / 0.0200 render px, GI finite, pose valid, PRE/POST alpha identical;
99 s, zero driver events/dumps/build changes. The fix initializes the engine's
event-owned cursor with a centred SDL event per present, forcing an empty poll
after it so event batches terminate. 253 still moved despite polling/input
neutralization; keep it INVALID. Only SDL_GetGlobalMouseState is statically
imported by the game; event/poll consistency is necessary, not just coordinates.
254 off/on Y 0.017001871 / 0.016828934; two captures do not pass the noise gate.
Historical fixture proof at 7b6e82a is preserved; job255 results are above.

Static energy run252 execution-valid189s with zero driver events/dumps/build
changes, but the quality check correctly rejects it: camera moves from camp
to woods, about -318 render pixels x / -42 y between the first two captures.
No noise/energy pass is claimed. The initial fixture neutralized global mouse
state but left local/relative mouse and keyboard paths live. P1 e33f356 now
uses SDL's own window coordinate space consistently, blocks physical keyboard,
buttons/wheel and relative motion only in opt-in StaticCamera, and logs hooked
APIs. Job253 is running to prove stationary captures before another full run.
The frozen normal-error bound also now uses the same frozen basis as its normals.

User resumed and made P1 energy preservation first priority. The rejection
branch drops L*w then divides by the unchanged ray count: lost bounce, not a
quality improvement. New default-off gi_history_fresh (P1 d2173c7, current
evidence head bc8cea0) replays RGB after native lighting with existing frozen
PRE pose. Both gate modes use paired direct native source, with fresh current
source on rejection. It preserves PRE visibility and fixed ray weights/counts;
it does not compare recursive composite radiance to a different fresh integrand.
Legacy PRE energy is reported separately. Initial scope RR full-res/diffuse-only.

Offline: 11 CTests, 49 shaders, 24 ABI checks, 4 energy-check regressions and
36 script checks pass; seven ordinary GI/RF binaries remain byte-identical.
Smoke251 valid113s, no driver events/dumps/build changes. Off/on raw Y
0.016625201 / 0.017001902, PRE legacy 0.017214335 / 0.017579911; finite, GPU
pose valid, identical PRE/POST alpha, static albedo within raster jitter.
One source window proves 27,078,193 cached and 12,110,691 fresh reads, only 3
unavailable. Two captures are not enough for the measured-noise energy gate.

Unlimited VVL reports 2,723 sync hazards in existing renderpass/NGX categories;
no dispatch sync hazard or message naming 21 private exported handles/new
metadata. Earlier gate-only smoke has the same three categories. This is
scoped evidence, not a clean validation certificate. Raw VVL log and evidence
JSON retained on P1; docs/GI_FRESH_FALLBACK.md explains bounds and limitations.

Job252 running: twelve static captures in ONE run, six balanced AB/BA pairs,
owned centred pointer, raw checkpoints for actual gate mode and unchanged AO.
The check compares mean GI with its paired Student-t 95% noise interval and
rejects black output, invalid pose, camera motion or missing pairs. Report CI
width/power; only after this passes repeat the production moving pan. The
residual cohort already widened to 13.4%/7.15% in run248; no cross-run images.

P3 baseline250 completed INVALID: 21 windows, 6,861,577,296 reference pixels,
12,304 any-bit differences (41–1533/window), zero >1e-5-relative or one-writer
differences; zero driver events/dumps. ls_stats=1 chooses the full variant,
unlike the old golden default control. No new box-light activation or merge.
P4 8d902d5 adds the conservative interval classifier and corrected alpha-domain
contract; full build/4 CTests, 197,621 independent-oracle checks pass. GPU bake,
domain provider and hardware attachment remain unimplemented; no main merge.
P6 minimum water design remains in main. One usage reset is authorized if we
reach the limit; that authorization has not been consumed.

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
