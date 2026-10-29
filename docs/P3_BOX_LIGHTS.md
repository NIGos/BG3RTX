# Box lights — integration candidate

Branch codex/p3-box starts at main ee7dc56. Checkout currently lives at the
managed p2-pending/BG3RTX path; the completed P2 branch and evidence are retained.
P1 remains the first priority; this branch now has scoped runtime signal evidence,
with accuracy and production shadow gates still open.

## Implemented, not yet approved

Selectively ported the shader foundation from a28a4c7 as d088cdd, preserving
main's handoff and the corrected point/spot reference. Host wiring is 385d1fe.
The ls_box key defaults to 0 in development, generated defaults and release.
It selects separate default/full pipelines, binding the exact native lighting
descriptor set as set 3 with its recorded dynamic offsets. Missing box buffer,
set, layout or pipeline retains the existing point/spot path. ls_check and
ls_impl=0 also retain their existing reference paths.

Layout changes rebuild the box pipelines; release defers their destruction.
The stats allocation is 152 counters, covering shader indices 136–150, including
projector-only validation counters that also run in the ls_box=0 baseline.
The inherited 136-counter allocation was too small for those extra writes.
Projector validation selects every pixel whose froxel lists a projector in both
off/on modes; requiring an evaluated light would exclude box-only pixels from
the off baseline and compare different populations. Ordinary default and check
shader binaries are byte-identical to main with boxes disabled (DXC SHA-256).
Logs report actual box activation, evaluations, shadows, unknown queries,
cookies and projector-pixel recompute errors. GPU timing identifies box mode.

Build, all 3 CTests, generated-default agreement and 34 shader files pass.
These checks do not prove native descriptor compatibility, physical agreement
or visual quality in game. Runtime details below distinguish default reference, activation, signal and
accuracy evidence. No main merge or default activation is approved.

## Deciding queue work

After P1's runnable candidate has priority, use serial S6, S7 and S5 sessions:

1. ls_box=0, ls_check=1, hold 90: every reference window must have zero any-bit,
   relative and one-writer differences. Keep current point/spot behavior.
2. ls_box=0 and 1 with ls_debug=4, ls_stats=1, hold 90 each: inspect complete
   pure windows, projector coverage, game/recomputed mean and errors. Derive
   accuracy gates from the same input/recompute contract, not an arbitrary
   brightness allowance. A missing projector population is inconclusive.
3. ls_debug=0 off/on: inspect geometry, shadow edges, leaks, finite output and
   temporal behavior. Compare the selected native shadow policy; unknown rays
   retain the game's atlas term. Quality has priority over frame rate.

Require activation and complete log windows, not only a requested key. Do not
waive failures because the rest of the frame agrees or because cost is low.
Before every runner/game start, check USER_PLAYING and actual BG3 processes.
Use the shared queue with expect patterns. Source and paired binaries stay
frozen while a job runs on this checkout. Merge only through merge-green after
reading the deciding numbers, then push main and this branch.

The cookie gradients approximate neighboring quad samples; verify their
agreement. Box lights have no fixture/owner join. No source-angle parameter is
invented for these directional emitters. The water and OMM phases are separate.

Queue specifications are saved in tests/fixtures/p3_box_s6_reference.json and
p3_box_s6_validation.json. Run the reference first and inspect every hold-90
window. The second job uses p3_box_validation_phases.json: one session with
100 s key-off then 100 s key-on, ls_debug=4 throughout. Inspect only pure
120-frame windows, identified by the mode switch and configuration markers;
discard a window spanning the change. Nonzero projector coverage and actual
activation are required. The expect patterns prove execution only: they do not
certify recomputation accuracy. Do not enqueue these behind an active P1 game;
recheck user-playing, actual processes and the queue before each launch.

## Reference fixture correction after250

250 on8d40fdc is execution/expect INVALID:21 windows,6,861,577,296 reference
pixels,12,304 any-bit differences, zero >1e-5-relative/one-writer differences,
zero driver faults/dumps. The fixture accidentally set ls_stats=1, selecting
the FULL variant. Earlier accepted P2 controls used the DEFAULT variant.
The reference fixture now explicitly pins ls_stats=0,ls_samples=0,ls_debug=0,
with ls_check=1 still allocating/reporting all reference-check counters.
No threshold is relaxed and every zero-difference window remains required.
This compares the actual default point/spot path to its accepted reference.
It does NOT resolve or waive FULL-variant bit differences; that separate
limitation remains open, including any mode that requires exact FULL agreement.
Projector native-light validation still requires actual coverage/accuracy in
its own full/debug path. Re-run baseline before activating any boxes.
P1 retains queue priority; fixture change alone is not runtime evidence.

264 /ec68966 DEFAULT-path reference: valid165s total/104s session, zero driver
faults/dumps/build changes. All24 complete120-frame windows have ZERO any-bit,
relative, one-writer and joined-result differences over7,844,924,963 reference
pixels. Default point/spot compatibility passes. This does not waive250's
FULL-variant bit differences or prove boxes; first box-on native projector
validation remains next once P1's runnable test has had queue priority.
Evidence docs/evidence/p3-default-reference-264.json.

267 /c039f87: first native box activation executes (set3,2 dynamic offsets),
valid291s total/219s session, zero driver faults/dumps/build changes. Listed
projector population and evaluated boxes are nonzero (last1.319 evaluations/px),
but reported game/recomputed/error means are all0. Accuracy INCONCLUSIVE, not
a zero-error pass: fx rounds raw pre-exposed radiance in1/256 increments while
the native scale is0.00136865. Zero counters do NOT prove black rendering.
Upgrade projector-only sum precision or inspect existing raw data before deriving
accuracy; keep default off. Validation log/evidence retained. Debug4 does not
trace shadow rays, so actual shadowing/quality is still untested.

Projector QA now also uses exact uint64 sums with24 fractional bits, preserving
raw-radiance signals below the old1/256 quantum. Counts distinguish true zero
from quantized zero. New stats152..164 cover native/recomputed/error/overshoot/
box sums, nonzero populations and invalid values;165 counters are allocated.
The8x8 group's bin hash has coprime7/13 steps over64 bins, yielding an explicit
maximum bin population from ACTUAL render dimensions. A half-capacity uint64
bound rejects nonfinite/negative/overflow-risk values; it never silently clips.
Mean fixed-point rounding uncertainty is at most2^-25 raw units. No exposure
normalization or image-output expression changes. Default/check SPIRV remain
byte-identical to main; full build/3 CTests and affected shader variants pass.
Next repeat267 and inspect pure off/on windows with nonzero signal and invalid0;
keep old FULL bit differences and unresolved validation categories visible.

270 / 8cb61f0 precision repeat: INVALID positive-signal gate, 283 s total,
no driver faults/dumps/build changes. 52 windows cover 16,919,202,052 projector
samples, with native/recomputed nonzero counts BOTH 0 and invalid count 0.
This comparison still has no reported positive input; higher precision alone
cannot validate box accuracy. Runtime shaders compiled successfully. Next isolate
with GI disabled so the game's own AO reaches the lighting: current native
AO-on-direct flags are m0.x=1,m0.y=1, so a folded zero GI visibility could erase
the very direct-light signal being compared. This is a hypothesis, not proof of
black final rendering or an accepted recompute. Keep quality/shadow gates open.

The next isolated signal probe is prepared in
 tests/fixtures/p3_box_s6_native_ao_probe.json: box1/debug4, GI off, native AO
requested (but debug4 prep itself forces AO0), 35 s hold. It still requires actual activation, positive native AND
recomputed populations and invalid0. This is a native-lighting recompute control,
not a GI-off quality workaround or approval of production GI/box interaction.
Run it only after runnable P1 tests have had queue priority.

273 GI-off signal control: INVALID positive-signal gate,108s total/48s session,
no faults/dumps/build changes. Positive native/recompute counts remain0;
GI folding is not required to produce the zero-signal comparison. Code audit
finds validation prep itself forces AO0 for debug4, independently of GI. Native
flags m0.x=1,m0.y=1 then suppress direct diffuse/specular too. The earlier phrase
native AO untouched was wrong for this fixture; preserve273 as negative evidence.
Next existing debug6 forces AO1 and removes the sun; it should reveal positive
native/recomputed values, but native ambient is also present. Therefore it is
ONLY a positive-signal/one-sided overshoot control, never total recompute parity.
No new rendering code or counter is necessary to distinguish this hypothesis.

275 AO1/debug6 positive control PASS: valid110s/48s session, no faults/dumps/
build changes. Native and recompute inputs are positive in every120-frame
window; invalid0. Last nativeY0.038576367, recompute0.029535810,
absolute difference0.009043053, overshoot0.000001248, boxY0.018699073.
This supports debug4's AO0/direct-suppression explanation and proves the fine
counters carry real signal. Native ambient remains, so the difference is not
pure direct-light error. No total-parity, shadow-ray or quality pass follows.
Next short ONE-run AO1 box0/1 control measures the marginal projector term
against its positive native+ambient envelope; no cross-run image comparison.

277 ONE AO1 box0/1 comparison valid150s/88s session, no faults/dumps/build
changes. Excluding warmup/configuration-straddling/first mixed activation
windows leaves7 pure off and9 pure on windows (see exact weighted evidence).
Absolute native/recomputed difference falls about67.4% when boxes are included;
the missing source contribution is now exercised with real positive signal.
Native ambient/in-scatter remains in the comparison; nonzero overshoot is
retained, not waived against an invented tolerance. This is marginal-light
coverage evidence, not total recompute parity. Default/FULL compatibility,
shadow rays/unknown fallback, S7/S5 cookies and temporal/visual quality remain
independent requirements. Keep ls_box0 in both development and release.

278 normal lighting/GI, debug0: actual box shadow execution PASS, valid111s/
49s session, no driver faults/dumps/build changes. Last evaluated/traced1.319/px,
occluded0.738/px, unknown0.000/px, cookie0.000/px; box native light46.054% of
lit luminance, removed19.789%. Complete window ranges and capture metadata are
retained in evidence. Final/lighting dumps have no reported nonfinite values;
a D32F depth dump reports4590, preserved without a global finite-output claim.
Next forced candidate-cap1 should exercise actual unknown box queries; source
sets t=sh for unknown before both default/FULL deltas c*(t-sh). No visual,
energy, cookies or moving-quality approval follows from smoke execution.

279 forced-cap1 unknown-coverage attempt INVALID: valid execution117s/49s
session, no faults/dumps/build changes, but all reported box unknown rates
remain0.000/px. Actual opaque shadow hits can end traversal before the cap
aborts nonopaque candidates; lowering the cap alone did not exercise the
required branch in S6. Do not claim fallback verification or relax the expect.
Final nonfinite0; the D32F-only nonfinite dump remains retained. Next use other
required scenes/alpha coverage, rather than repeating this unchanged control.

S7/S5 next fixtures use normal production GI/debug0 and require actual nonzero
projector evaluations and shadow rays. They do not require a guessed cookie
population: inspect actual cookie counts and keep that requirement open if
absent. Both saves are recorded loadable in main captures/scene-sweep.json;
no mods/saves are changed. P1 existing-key AO-source isolation runs first.

281 S7 INVALID at startup: game exits2s after launch, before any new layer log
or scene-ready marker; zero driver faults/dumps. No box/lighting/cookie result.
Historical loadability is not current runtime proof. Keep the raw startup output;
no save/mod change or feature-bug attribution. Try S5 after runnable P1 work;
a bounded S7 retry may distinguish transient startup from a persistent case.

283 S5 normal-GI shadow coverage PASS: valid113s/49s session, no driver faults,
dumps/build changes. Last evaluated/traced0.079/px, occluded0.029/px,
unknown/cookie/shadow-mapped0.000/px. Native box light3.351% of lit luminance,
removed0.664%. Final finite; raw dump nonfinite records retained in evidence.
This adds actual ray coverage in the lava scene, not cookie or native-accuracy
proof. S7 needs one bounded startup retry after runnable P1 attribution.
