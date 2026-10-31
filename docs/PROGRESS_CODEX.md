# BG3RTX current progress — 2026-10-31

## Latest user steering

User reports good results with FG 2.0 and about 85 fps without diagnostics.
Rotation artifacts were attributed to cascade capacity. Do not reopen camera/TLAS
lag or add selectors without new contradictory evidence. The 85 fps is the user's
reported baseline, not a timing result for the new offline candidates below.

Fix these two visible regressions first:
1. Dark rectangles around camp rugs and strange shadows on the wooden walkway.
2. Astarion's fixed, stepped dark band along the hairline.

Use one hypothesis at a time, automatic A/B in one run, then a user trial build.
New logic belongs in core unless native capture/resource ownership requires layer.
Keep public ABI 21. Merge once per verified milestone, not after every commit.
Defer unrelated feature expansion until the two regressions are addressed.

## Camp rugs and planks

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

Next: core-only diagnostic switch using unused Push.ls_flags bit 21, with unchanged
push size and ABI. Keep physical origins ON in both phases; compare only guard
OFF/ON/OFF. Toggling physical origins themselves would confound this hypothesis.
After the same-run image result, prepare an isolated user trial and explicit flags.

## Live boundary and trial

Earlier steering forbids game launches while the user uses the PC/another game.
The new request defines the required A/B workflow; confirm the available test
window when the concrete candidate and batch are ready. No game was launched.
Use scripts/test-queue.ps1 only. Keep unapproved drafts outside pending inboxes.
Never interrupt an unowned game, erase USER_PLAYING or manufacture USER_AWAY.

The last recorded personal trial is hair-origin-live 047e105, ABI 19. It has native
Hair/Scalp/physical origins ON, camera/cascade budgets 1408/587 MiB, OMM/current Sun
OFF and dev 1. It remains unchanged. Do not mix its DLLs with ABI 21 components.
The user reports improved foliage flicker and hair color; these new fixes are untested.
Do not ask the user to edit INI files or perform test toggles.

## Verified source state

Main efc27e7 is pushed. merge-green passed both DLLs/launcher, 60 CTests,
1232 runtime shader files and QA. All new experimental defaults remain OFF.
It includes native STI materials, ST depth coverage, TileBlend camera coverage,
the receiver guard and alpha spill. Source checks are not in-game qualification.

Development: codex/omm-bake in C:/Users/quali/.codex/worktrees/omm-bake/BG3RTX.
Reviewed current-Sun-only shadow recipe 2e01c3c/92f9995 is beyond main.
It reads seven native floats (28 bytes), preserves camera/legacy classification,
and reuses two views and owned sampler 19. Five combined CTests pass.
The separate legacy bridge is under review; keep its source generation, ranges,
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
| M1 | Scoped Hair evidence and offline cutout fixes; current two regressions first. |
| M2 | 27 native-matched components, 24506 triangles; room closure unproven. |
| M3 | Supported hit materials; persistent materials, world lights and state open. |
| M4 | GI descriptor fix implemented; corrected image recheck still pending. |
| M5 | OMM OFF and unqualified; water/environment selected by measured impact. |
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

Next decisions: complete the core guard switch and prepare the two isolated A/B
comparisons; establish the PC test window; keep or reject each hypothesis from
its own result; then ship one reviewed user trial. Promote no default from proxies.
Preserve saves, backup files, snapshot tags and shared junction targets.

History: [pre-trim progress archive](archive/PROGRESS_CODEX_2026-10-31.md).
