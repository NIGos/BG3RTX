# BG3RTX current progress — 2026-10-31

## Paused at the user's request - latest checkpoint

No further development or live jobs until the user resumes. Test game is closed;
private queue pending/running are empty and its finite authorization is consumed.
User cannot currently reproduce rug/plank rectangles or Astarion hairline band.
Personal trial 047e105 ABI 19 is unchanged. OMM/Current Sun user smoke test positive.
A slight dialogue shadow flicker remains; its source is not established.
Current Sun full log has one window with 368956 actual samples, then GPU scene
readiness failures. The combined epoch/late-write counter does not prove lag.

GI R3 d19ecd7: VALID 580 s, FG 2 real presentation, no focus activation, no
NVIDIA 153/dumps/build drift. Three K16 bursts, 176 samples each; FG active all 48.
565595 stable pixels; GI negative-envelope pixels 0, positive 21517.
Previous stipple ROI: 14/12812 final mean-low pixels; old R2 was 1144/19043.
Different runs/denominators: descriptive reduction, not a direct cross-run A/B.
Final full-image review, motion and visible gain remain unqualified; keep GI OFF.
Whole GI median OFF A/ON/OFF B: 2.097/2.288/2.048 ms. Not isolated bounce cost.
Evidence/contact sheets: docs/evidence/gi-descriptor-r3-20261031/.
New logic in core where possible. ABI 21. Merge once per verified milestone.

## Camp rugs and planks

Status: not reproducible since 10-31, kept as a watch item.
Symptoms are recorded in four user screenshots. Their precise mesh/shader identity
and the responsible RT effect have not yet been established. Do not call them fixed.
A rectangular shadow alone does not establish a cutout or identify its caster.

First hypothesis: alpha capture partition overflow produces false solid geometry.
The preserved 10:28–10:38 trial log has 77 windows; 11 contain 0.01–0.92 alpha
substitutions per frame. The last three windows have none; opaque drops are zero.
These counts demonstrate the defect, not its attribution to the pictured pixels.

The default-off rt_alpha_spill key is implemented in main. It uses free opaque
storage while preserving direct AlphaRec ownership, alpha flags and Hair bounds.
It changes neither total memory limits nor shader coverage. Recipe failures and
both storage ranges being full remain unsupported; they must not pass a gate.

Next: same-run spill OFF/ON/OFF at the same camp view, all other effects unchanged.
Require actual rescued draws and matching source geometry; a zero-participation
comparison is inconclusive. Check final image, visibility and primary-depth data.
If it does not explain the rectangles, reject that attribution before testing the
separate legacy-cascade cutout hypothesis. Do not enable several fixes together.

A prepared legacy bridge addresses one exact native TileBlend shadow family.
It is not yet merged, deployed or linked to the pictured rugs/planks. Its layer
work is necessary for native draw inputs, immutable geometry, BLAS and lifetime.
No new public ABI is required. Keep this candidate separate until attribution.

## Astarion hairline

Status: not reproducible since 10-31, kept as a watch item.
Confirmed source defect: native Hair, Pubes and Scalp all write material class 5.
Class 5 alone cannot identify a Hair receiver for physical-origin rebasing.
A nearby Hair primitive may depth-match a Scalp pixel at the boundary.
This is a real identity gap; screenshot causality remains unproven.

Main has a conservative competing-Scalp guard. It preserves the virtual origin
for ambiguous, unreadable or capped queries. It does not change alpha, occluders,
ray bias or the existing depth tolerance. The guard runs only in the existing
rt_hair_physical_origin variant, which is OFF by default.

Offline evidence: 31 CPU checks; 16 ordinary shader identities and 16 enabled
validations; 22 standalone GPU cases with zero validation errors. The GPU test
includes nonzero rebasing and a negative control, not real native raster pixels.

Prepared: core-only diagnostic switch using unused Push.ls_flags bit 21, with unchanged
push size and ABI. Keep physical origins ON in both phases; compare only guard
OFF/ON/OFF. Toggling physical origins themselves would confound this hypothesis.
Candidate 09fc3eb builds; all 60 CTests pass. Image A/B remains unmeasured.

## Live boundary and trial

User paused the goal and requested safe shutdown plus complete handoff updates.
Latest background-test permission was used for the completed GI batch; do not
reuse its consumed receipt or launch pending drafts on resume without steering.
Only scripts/test-queue.ps1 may launch; preserve markers, saves and unowned games.
Personal hair-origin-live remains 047e105 ABI 19; never mix its DLLs with ABI 21.
No new user trial was installed. Camp and Hair A/B remain unqualified/nonreproduced.

## Verified source state

Main d3e06e1 is pushed. merge-green passed both DLLs/launcher, 60 CTests,
1232 runtime shader files and QA. All new experimental defaults remain OFF.
It includes native STI materials, ST depth coverage, TileBlend camera coverage,
the receiver guard and alpha spill. Source checks are not in-game qualification.

Development: codex/omm-bake in C:/Users/quali/.codex/worktrees/omm-bake/BG3RTX.
Reviewed current-Sun-only shadow recipe 2e01c3c/92f9995 is beyond main.
It reads seven native floats (28 bytes), preserves camera/legacy classification,
and reuses two views and owned sampler 19. Five combined CTests pass.
The separate legacy bridge is reviewed but parked; keep its source generation, ranges,
opaque complements, pool/BLAS retirement and per-scene completeness explicit.

Standalone GPU coverage proofs passed with zero validation errors:
- Native tileset: 32 cases through all four query entry points.
- TileBlend: 53 cases, including 18 UNKNOWN cases, through all four queries.
- Material fields: 14 cases through closest_hit plus actual gather/join.
- Hair receiver guard: 22 cases through the actual guard helper.
These do not establish native graphics-XFB execution, image quality or game cost.
Alpha spill has 105 budget checks; central settings/schema have 335 checks.

## Remaining rendering program

The full RENDERING_EXECUTION_PLAN remains active; it has not been narrowed.
| Milestone | Current state and remaining work |
|---|---|
| M0 | Queue authorization/ownership implemented; respect the live boundary. |
| M1 | Scoped Hair evidence and offline cutout fixes. Camp rug rectangles and Astarion hairline band: not reproducible since 10-31, kept as watch items. |
| M2 | 27 native-matched components, 24506 triangles; room closure unproven. |
| M3 | Supported hit materials; persistent materials, world lights and state open. |
| M4 | GI descriptor fix implemented; corrected image recheck still pending. |
| M5 | OMM user smoke test positive, quality unqualified; water next after M3/M4. |
| M6 | Shared direct/two-surface reference; opaque-room PT and feasibility undecided. |

Persistent terrain has 13512 triangles and 738 holes; its actual texture identity,
normal projection and runtime state remain unresolved. POSITION agreement alone
does not certify authored UV, tangent frames, material bindings or opacity.
RTX Hair has no proven groom/radius/deformation provider; native cards remain.
Sun half-angle 0.00459 radians is correct. Do not enlarge it or blur missing casters.
Old experimental GI R2 images are rejected despite positive contribution counters.
The dedicated GI/reference descriptor fixes are in main; both effects stay OFF.

## Evidence, costs and next decisions

Preserved regression log/config/screenshots:
captures/user-camp-regression-20261031 in the development tree.
Manifest: docs/evidence/user-camp-regression-20261031.json.
Guard, spill, TileBlend and GPU evidence are under docs/evidence/.
GPU pass costs of the new fixes are unmeasured. Fixture wall times are not costs.
Measure timing separately from diagnostics, capture and validation overhead.
Keep rendered and displayed FG rates separate; missing RT never passes a test.

Resume order: reproduce dialogue flicker with temporal native/RT evidence;
review GI R3 final images, then motion/visible gain before any default promotion.
Camp/Hair candidates remain ready only if the original defect returns.
No automatic queued continuation. Full program remains incomplete and paused.
Preserve saves, backup files, snapshot tags and shared junction targets.

History: [pre-trim progress archive](archive/PROGRESS_CODEX_2026-10-31.md).
