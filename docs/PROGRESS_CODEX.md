# BG3RTX progress

## Current checkpoint - 2026-10-29 06:45 Europe/Rome

- Main rendering remains 6dba0a8; only the P6 water design is additionally merged.
- P1 static energy consistency and moving quality remain unclosed; every prototype stays off.
- Invalid geometry runs 265/266 are excluded; corrected bindings pass scoped runtime audits.
- P1 attribution 284 fails the strict loss gate: unlit losses are real but far smaller than positive source drift.
- Build 12 CTests and 24 shader ABI checks pass; ordinary GI/RF shaders are unchanged.
- P3 actual shadow rays run in S6 and S5, with finite final output and no driver faults.
- P1 held exposure293 still fails PRE energy; isolated legal QA-hold fix is under validation294.
- P2 local-light reference is merged; EN2/D-EM failures keep those features off.
- P4 has a tested CPU classifier, P5 depends on EN2, and P6 runtime water remains open.
- The one authorized reset was consumed at weekly100%; credit balance0, no second authorization.

## Resumed — 2026-10-29

293 valid execution210s/142s scene session confirms legal exposure writes:
0 hold usage errors/refusals; held EV13.0522 matches both probes after472/1072
successful writes. All12 captures/dumps finite, saved alpha exact, pose valid,
static<=0.54px. All SIX gate-off controls are bit-identical RGB to original PRE.
Original PRE consistency still FAILS+0.595469%,95%Y CI[+0.0000262039,+0.000185806].
Paired gate on/off-0.0694147% has CI[-0.000774917,+0.000750046], half width4.26%
of offY: includes0 but NOT equivalence. Exposure variation alone is insufficient;
no new transport/compensation/brightness tuning or GI promotion. Full layout/
push/write categories absent; other VVL unresolved. P1 evidence/spec d9cb628 pushed.

The QA fix is isolated from ALL P1/P3 work on codex/qa-exposure-hold6523aaa
(stable main ABI8, worktree qa-exposure-hold/BG3RTX): cherry-pick4941657 of5f84d9b
plus smoke fixture. Full build/3 CTests/32 shader files/all QA detectors pass.
294 LIVE runner17296 at06:44:18 after fresh idle checks, two scene-only held
captures/full VVL. Freeze isolated tree; inspect actual held probes, no refusal/
usage errors and finite output before merge-green. This improves the test suite,
not production lighting. Temporary worktree should be archived after integration.


P1 292 is INVALID despite a superficial statistical pass. The pre-existing
qa_exposure_hold used CmdUpdateBuffer on a native STORAGE_BUFFER without
TRANSFER_DST: full VVL reports1262 dstBuffer-00034 errors. Never interpret its
apparent+1.4378% paired/+0.4944% original-PRE intervals (both broad/include0) as
valid exposure isolation. Six off controls were RGB-exact, saved alpha/raw/pose
passed, static<=0.4px and all dumps finite; these do not waive invalid usage.
Queue also missed a startup-only mouse line outside the scene slice; full log
proves actual SDL3840x2160/client centre1920,1080/static1. Evidence292 retained.

Separate fix5f84d9b uses a one-thread storage shader on the ACTUAL adaptation
dispatch binding1/1, including dynamic offset, storage type/24-byte range checks,
existing descriptor12/push/ring, compute dependencies and restored native bindings.
Missing input/pipeline REFUSED, successful writes only counted on dispatch. No
new native allocation/usage/ABI/transport or production exposure change.12 CTests
and11 post shader entries compile; ten previous entry binaries identical3d411aa.
bfb723b records invalid evidence and replaces the session's startup-only expect
with recurring PRE metadata.293 LIVE frozen P1 bfb723b, runner308 at06:40:14 after
fresh idle/playing checks. Require no hold refusal/usage error plus full audits
before interpreting the repeated12 static AB/BA captures; moving gate still open.


291 execution PASS139s/48s session, nine loaded-scene windows:2,950,041,600
receivers,2,947,407 failing (0.0999107%),17,700,249,600 bounded channels with
15,775,122 violations; invalid0/unbounded0. Mean outside-interval excess3.50383e-6Y.
Standard class2,918,571 failures/2,804,123,970 receivers, skin26,926/947,525;
remaining classes small. Raw diffuse diagnostic d156 shows sparse red points on
metallic rims/details and characters, not a cause proof. Raw projector residual
+0.599412% has a different cohort.62 modules/no refusals/faults/dumps/build changes.
Final finite; native D32F6402 nonfinite retained. Full scoped layout/push/write
categories absent, other VVL unresolved. Evidence291 and spec committed b913e8e.
Native accuracy, per-query unknown delta, cookie filtering/moving quality OPEN.

Return to P1 source-epoch isolation using EXISTING qa_exposure_hold1:3d411aa adds
only a fixture/spec, no rendering change or exposure payload. The hold is applied
after loaded-scene session readiness (not menu), with15s warmup,12 static AB/BA
traces, production gi_strength1/gi_ao1/shared stream/geometric gate. Require actual
held writes, finite/static/saved alpha and paired/original-PRE noise intervals.
292 LIVE runner3980 at06:32:25 after fresh idle/playing checks; P1 frozen until
terminal. A static isolation result cannot close P1's required moving gate.


P3 290 execution PASS,145s/49s session, eight scene windows. All shaded receiver
cohort2,622,259,200 pixels/15,733,555,200 bounded channels: invalid0/unbounded0,
2,605,305 failing pixels (0.0993535%),13,986,533 violating channels, mean excess
outside derived UFLOAT intervals3.48717637e-6Y. Raw projector-only residual is
+0.599347%; populations DIFFER, do not subtract their means to claim an exact
explained fraction.62 controlled modules, NO whole-log refusals/faults/dumps/
build changes. Final finite, D32F2481 nonfinite retained; full layout/push/write
categories absent, other VVL unresolved. Evidence290 on P3; quality still OPEN.
9beb1e1 adds material-class population/failure counts and debug4 violation map
through existing diffuse output (red above, blue below, green invalid, cyan
unbounded). No new ABI/resource/transport; all six ordinary shader binaries are
identical56bcc06, five CTests/two diagnostic shaders pass.291 LIVE runner37908
at06:26:41 after fresh idle checks. P3 source/binaries/HEAD frozen until terminal.


P3 289 is terminal, queue INVALID: a one-time box-active message occurred before
its scene slice. No result is retroactively passed. Nine actual scene windows
cover 2,932,082,331 projector receivers: native Y0.0292808848, recompute0.0294563717
(+0.599322%), abs error0.0001767432, invalid0. All62 native modules controlled;
whole log has NO refusals. Final finite, native D32F d130 has2604 nonfinite.
Full layout/push/write categories absent; other VVL categories remain unresolved.
Evidence is committed on P3. Initial reset refresh lag resolved: fresh weekly0%,
ordinary usage allowed; the single authorization is consumed, credit balance0.

8ec56ab tests the storage-format hypothesis without waiving the residual:
Vulkan allows either adjacent UFLOAT value, so each loaded R11/G11/B10 component
defines its own open neighbour interval. Diffuse/specular RGB checked separately
on ALL shaded receivers; finite maxima inconclusive, invalid values explicit.
No tuned percentage or model-error allowance. Shared HLSL/C++ code is exhaustively
checked against an independent decoder (2,974 bounded codes plus maxima), 5 CTests
pass, two diagnostic shaders compile; all six ordinary variants byte-identical.
Recurring scene box-active stats replace the startup-only queue expectation.
290 LIVE frozen P3 8ec56ab, runner33888 at06:21:53, after fresh idle/playing checks.
No native-accuracy/default/merge approval; wait terminal and inspect full audits.


Usage reset consumed once after fresh weekly100% reading, per explicit user
authorization. Tool outcome reset, credit balance0; idempotency key
8ce84295-c9a8-4ba8-bbfb-cf1f81dd0af7. Initial quota refresh lagged; do not retry
a successful logical reset or redeem another without fresh authorization.


P3 7adc91d adds startup-only native-direct validation (default/release off).
Recognized native compute scalar reads of set1/b0 flags become AO enabled,
direct-AO coefficient0 and sun-mask enabled. Native UBOs/descriptors/BRDF/cookie
code are untouched. Actual successful variant follows module -> pipeline ->
command state -> dispatch; core refuses missing/mixed controls and unsupported
modes. Separate owned validation full/full-box variants use matching direct
factors1; all six ordinary/default/FULL/check/box shader binaries are identical
to56bcc06. Paired host/core ABI9; no reuse of P1's ABI14 process.
Offline semantic/foreign-interface/unsupported/malformed tests pass with release
assertions enabled. All62 captured native lighting modules transform exactly
three scalar loads/components11 and pass spirv-val. Full build/4 CTests and both
new owned shaders compile. These are not native-accuracy or quality passes.
288 LIVE frozen7adc91d, standalone S6 two traces with full unlimited VVL. Runner
37748 started05:58:38. Require actual applied modules, positive signals, no REFUSED
lines and full layout/push/write audit before interpreting error numbers. Atmosphere
in-scatter/transmittance remains a possible residual, not a blanket waiver.


287 S7 cap1 exact-population control PASS: valid 115 s / 49 s session, seven
complete windows total 640,149,005 traced queries, 192,085,618 occlusions,
7,637,662 unknowns and 10,083,681 cookie evaluations. Final/all captured dumps
finite; no driver faults, dumps or build changes. This establishes actual path
coverage, not independent per-query delta identity, native cookie accuracy or
moving quality. P3 56bcc06 is pushed; shader transport is unchanged by integer
logging. Main rendering remains unchanged and every feature stays off.
Shared queue is idle. Further uncontrolled AO1 smoke repeats cannot close native
accuracy: the next P3 intervention must isolate native direct lighting without
both ambient contamination (debug6) and direct suppression (debug4/native flag1).
Inspect native flags/binding ownership before any replay/override implementation;
use owned inputs and actual-submit lifetime for any replay, and preserve the
unresolved validation categories. P1 source-epoch/conditional mismatch and unsupported unlit shading
remain unresolved; no candidate promotion, denominator or brightness tuning.


286 recursion-off isolation still FAILS original-PRE consistency: valid 198 s /
195 s exclusive, static <=0.16px, finite/pose/actual alpha pass, no faults/dumps/
build changes. Gate +0.8129% has broad paired Y CI [-0.000058595,+0.000336572]
including zero (half width about1.16% offY, not equivalence). Original POST/PRE
+0.4292% CI [+0.000014482,+0.000132254] excludes zero. Off mean changes remain
below0.0024%, with41..67 RGB differences explicitly retained. Recursion removal
alone is insufficient; keep complete production lighting. P1 725955f pushed.
Existing lagged native exposure probes were inspected before proposing another
payload: reported applied-k ranges271 0.999264..1.00135,280 0.999787..1.00011,
286 0.999477..1.00019 (28 probes each). They do not establish a dominant source
of drift and are not bounds on all history reads; no new transport added.
287 LIVE frozenP3 b97477b: S7 cap1 with EXACT integer unknown/cookie counts.
Runner20748 started05:31:48; no repeat from rounded0.000-rate assumptions.
EN2 offline context: its root HANDOFF.md is stale sun-replace history. Actual
EN2 source/spec is layer/hitmat.cpp, shaders/rt/rt_hitmat.hlsli and TRACK_D_RT13,
branch0916cff; no edits there. Existing blue/metalness/class mismatch failures
remain open, rather than being excused by join coverage or narrower cohorts.


285 bounded S7 retry PASS for ray/cookie coverage: valid116s/48s session,
seven complete windows; last traced0.295/px, occluded0.092/px, cookie0.004/px.
Final nonfinite0 and all captured dumps report0; no faults/dumps/build changes.
281 remains a separate startup failure, not evidence against the box shader.
Native cookie filtering accuracy and unknown fallback are not proved by this
smoke. P3 now appends exact traced/occluded/unknown/cookie counts from existing
counters:0.000 rates cannot prove count0. Host-only change, build/3 CTests pass;
S7 cap1 fixture requires positive integer unknown and cookie populations.
286 LIVE frozenP1 7bb43b5: gi_strength0 static12 isolates recursive-source
coupling. This changes the lighting composition and is not a production fix,
energy waiver, quality pass or permission to drop multibounce lighting.


284 strict positive-old-loss gate FAILS: stable 135 s / 133 s exclusive,
no driver faults, dumps or build changes. All fresh failures are unlit; up to
25/26 have positive old energy. Weighted failed-old Y reaches 6.7333e-9,
versus total positive replacement delta 1.40043e-4 in the same window. The
small loss is NOT waived, but cannot explain the much larger positive drift.
Reasons close exactly; invalid sums 0. Full layout/push/write VVL categories
absent; other categories unresolved. Raw GI finite/alpha exact; off controls
have 45/54 RGB differences (-0.00206/+0.00299% mean), explicitly retained.
On captures are +0.859/+0.688% versus PRE. No denominator reset or lost bounce
explains the main excess. P1 7bb43b5 pushes evidence and prepares existing-key
gi_strength0 static12 isolation of recursive/source-epoch coupling. A changed
lighting model is diagnostic only, never a fix; unsupported unlit shading stays
open and must not be replaced by trusted rejected history or an altered gate.
285 S7 bounded startup retry LIVE on frozen P3 b97f4d4. Unlike attempt281,
actual game37436 started05:22:01, creates a fresh layer log and is loading the
requested scene. Runner39080 started05:21:59. Do not relaunch it or infer success
from startup alone. Next runnable P1 control follows this job's terminal result.


283 S5 shadow coverage PASS: valid 113 s total / 49 s session, ten complete
windows, evaluated/traced 0.079 per pixel and occluded 0.029. Final nonfinite 0;
a D32F dump reports 2,015, retained without a global finite-output claim. No
faults, dumps or build changes; cookies, shadow-mapped and unknown rates remain
0.000, so those cases are not proved. P3 b97f4d4 is pushed.
284 LIVE on frozen P1 c3b924f, four static off/on/on/off captures and full VVL.
Require reason/count closure, invalid 0 and no failed POSITIVE old energy;
inspect every window, actual alpha and off-control parity before interpreting
the weighted means. Main handoff is condensed to 144 lines with exact live-job
ownership and the remaining quality limits. Next bounded S7 startup retry.


282 weighted attribution is execution/expect INVALID:9 fresh failures among
9,094,425 replacements, invalid sums0; reason totals close exactly. The mixed
window old/fresh weighted Y0.004275577/0.004360368 (delta+0.000084791).
Full layout/push/write VVL categories absent; other categories remain unresolved.
Raw finite/pose/alpha pass; off capture has53 RGB differences/2.732M pixels
(+0.00353% mean), so no exact diagnostic-control parity claim. On+1.908% PRE.
P1 c3b924f keeps the original selection condition and adds failed legacy-energy,
strict failed-positive counts, and unlit/other failure classes (201 counters).
No return value, fallback, source or output changed to meet an expect. Next
four captures allow pure windows; require invalid0 and ZERO failed-positive
energy, rather than assuming nine failed samples either matter or do not.
Build12 CTests/24ABI pass; candidate ready after current P3 job.
283 P3 S5 normal shadow coverage LIVE on frozene56cf88. S7 startup281 remains
unverified; try one bounded retry later, without touching saves/mods.
Usage checked:93% weekly used, one reset available but NOT consumed. User
condition is exhaustion; preserve that authorization for one use at the limit.


280 native-AO isolation FAIL: valid188s/185s exclusive, static<=0.16px,
finite/pose/actual alpha pass; all six off captures match PRE RGB exactly.
Gate on/off+1.1697%, pairedY95%CI [+0.000041581,+0.000373752]; original
POST/PRE+0.8004%, CI [+0.000040715,+0.000242889]. Both exclude0. Removing
the native GI-AO fold alone does not explain source drift; no AO/cap change.
P1 aaa3754 adds diagnostic mode4 for old versus fresh Y*w/rays on the SAME
rejected ray, including fresh failure, rejection reasons and all receiver pixels.
Q24 carries/bounds use receiver bins and actual width/height/rays,196 counters;
no output/weight/visibility/source/camera change. Exact source/independent rays/
adaptive fallback2 are refused. Build12 CTests/24ABI pass, seven ordinary GI/RF
shaders byte-identical. 282 LIVE frozenaaa3754, two static off/on traces with full
VVL; inspect invalid/failed/count closure and all layout/push/write categories.
281 S7 INVALID at startup: exits2s before new layer log or scene-ready; no driver
fault/dump or box result. P3 state/evidence is pushed; try S5 after P1 and a
bounded S7 retry without save/mod changes. It was historically loadable, not
proved currently loadable. P3 branch3075e38 at attempted launch.


278 actual box shadow execution PASS: normal GI/debug0, valid111s/49s session,
ten complete120-frame windows; evaluated/traced1.319/px, occluded0.738/px,
unknown/cookie0.000/px. No faults/dumps/build changes. Final/lighting finite;
D32F dump reports4590 nonfinite values, retained without global-finite claim.
279 cap1 unknown-coverage attempt INVALID: execution117s/49s session is stable,
but reported unknown box population stays0.000/px. Opaque committed hits can
finish before the nonopaque-candidate cap; do not claim branch verification or
relax the expect. P3 prepares S7/S5 shadow coverage on known-loadable saves.
P1 raw271 visibility over32.778M samples: exact0 only0.0211%, but below the
0.001 give-back denominator covers3.2255%. Frequency only, not source-energy
attribution or proof of native flags at Camp. 280 LIVE frozenP1 2df23ae:
existing-key gi_ao0 static12 isolates native visibility/source coupling with
geometric1/exact0/independent0. Its changed ambient/bounce integrand makes any
pass diagnostic-only, never a production workaround. No safety-cap removal,
brightness scaling, new random-stream variants or camera transport.


271 corrected-binding, stream-0 baseline: valid 200 s, no faults/dumps/build
changes; static max shift 0.08 px, finite/pose/alpha pass. All six off frames
match original PRE RGB bit-for-bit. Off/on +1.1048% has a broad paired interval
including zero, but pooled POST/PRE +0.6630% FAILS the stricter target (95% CI
[+0.000035013,+0.000193181] Y); all on captures are +1.03..1.77% versus PRE.
This is VALID evidence after the binding correction, unlike 265/266.
272 independent stream3 on the SAME frozen 7433185 build: valid190s,
no faults/dumps/build changes, static0.10px and all saved alpha bits exact.
Gate on/off +1.4178% FAILS: paired95% CI [+0.000009341,+0.000483439] Y.
Original POST/PRE +0.8620% also FAILS, CI [+0.000037327,+0.000261842].
Paired intervals have half widths about1.2/1.4% off Y for271/272: no equivalence
or variance claim. Independence alone is rejected as a sufficient fix.
Both evidence files are committed on P1 a999f5d and pushed (transient GitHub500 resolved). No candidate promotion or brightness/tolerance adjustment.
273 GI-off/native-AO control INVALID positive-signal gate,108s/48s session,
no faults/dumps/build changes. Counts remain0, so GI folding is not required.
Source audit: debug4 prep itself forces AO0; native m0.x/y=1 then suppresses
both direct terms. The old claim native AO untouched was wrong for this mode.
P3 e9a73d5 records negative evidence and prepares existing debug6/AO1 control;
it includes native ambient, so positive signals cannot prove total parity.
274 native previous-camera/MV probe valid117s, no faults/dumps/build changes,
all15 cases/validity and PRE=POST pass; zero full layout/push/write categories.
Ground offsets agree with canonical jitter to~0.002px; animated tent point limits
joint inference (2/3 match). No camera/jitter correction justified. Other VVL
categories remain unresolved. P1 bd6f6d2 preserves results and PAUSED.md.
276 ONE corrected-interface moving AB/BA: valid109s/51s session, no faults,
dumps/build changes. Residual0.133864671/0.161903599 (+20.95%), RGB+20.78%,
cohortY+44.51%, motion29.58/52.66px (+78.03%). Grid coverage3.856/2.646%,
2.317/1.572M samples. Unequal motion/cohorts prevent causal regression claims;
there is no demonstrated gain, and static energy failures271/272 remain decisive.
Park this candidate temporarily per goal, not the overall goal. Next P1 inspect
native direct-AO/capped give-back at visibility0 before any correction; use
existing data/keys and keep changed-integrand diagnostics clearly separate.
275 P3 AO1/debug6 control PASS: valid110s/48s session;10 complete windows,
3.236B samples, native positive100%, recompute positive3.165B, invalid0,
no faults/dumps/build changes. Last nativeY0.0385764/recompute0.0295358,
boxY0.0186991, overshoot1.248e-6. Native ambient remains: no total-parity or
shadow/quality pass.
277 ONE short AO1 box0/1 control valid150s/88s session, zero faults/dumps/build
changes. After excluding configuration-straddling/mixed windows,7 pure off/9 on
cover2.281/2.932B pixels, invalid0. Absolute difference0.027741794/0.009043992
(-67.3994%), nativeY0.038576762/0.038576244, boxY0.018699574 on.
Overshoot1.33e-7/1.258e-6 remains nonzero and is NOT waived. Ambient/in-scatter
prevents total direct-light parity; no shadow rays were traced in debug6.
P3 8973a34 evidence pushed. Next short debug0 box-on test must exercise actual
shadow rays and inspect finite output/unknown fallback, then S7/S5 cookie cases;
FULL compatibility and native-accuracy gates remain open. Shared queue is idle.

269 corrected-binding validation passes its full layout/push/write audit: valid
112 s, zero driver faults/dumps/build changes, all 15 cases and current projection
probes valid PRE/POST. Other validation categories remain unresolved; this is not
a clean VVL certificate. Saved alpha is exact; on versus PRE is still positive
in two samples, which cannot establish energy parity. Never reuse invalid 265/266.
P1 7433185 adds default-off gi_history_fresh_independent: POST uses stream 3,
PRE lighting/GI stream 0. Reusing the same stochastic transport for nested fresh
lighting can correlate self-hits/short cycles; independence is a hypothesis to
test, not a proven cause. No extra rays, images, weights, camera payload or AO
change. Build 12 CTests/24 ABI checks pass; seven ordinary shaders are identical.
271 LIVE on the frozen build with independent=0, corrected origin binding and
12 static gate pairs. Next use independent=1 on the SAME build before any pan.

270 P3 precise repeat INVALID: 52 windows, 16.919 billion projector samples,
zero reported native/recomputed positive samples and invalid=0. No nonzero-signal
accuracy evidence. New precision is not a reason to accept a zero comparison.
A box-on/debug-4 GI-off probe is prepared to isolate native AO; no production
quality workaround. Shadow rays and FULL-variant exactness remain open.

Full validation audit invalidates geometry runs 265 and 266: base surface passes
lacked declared/bound origin image 40 (VUID 07988). Their energy/rejection numbers
are retained as INVALID observations, not algorithm evidence. The separate 15-case
math kernel and four-binding projection kernel do not use image 40. P1 7e91e6d
shares the base layout/write contract (20/40/48), with a compiled pointer/type test
and reflection derived from that header. All 12 CTests and 24 ABI checks pass.
269 is the frozen unlimited-VVL repeat; inspect ALL layout/binding/pipeline errors.
268 isolated probe: all three captures have zero B1/B2 offset within arithmetic
error, PRE equals POST despite nonzero jitter. No position correction is justified.
P3 8cb61f0 adds bounded exact uint64 projector sums with 24 fractional bits and
nonzero/invalid counts; default/check shaders stay byte-identical. Runtime pending.

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
prove black rendering or agreement. VVL full log retained; contains unresolved
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
