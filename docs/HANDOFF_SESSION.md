# Session handoff - 2026-10-31: native coverage and user trial regressions

Read PROGRESS_CODEX.md next. Full RENDERING_EXECUTION_PLAN M0-M6 remains active,
not complete. User wants pragmatic checks, autonomous progress and useful parallel
work. NO BG3/game launches now. Standalone GPU tests are allowed; keep loads short
and low priority while user is at PC/playing another game. No focus/window changes.
Old receipts are terminal. Do not change the installed personal trial.

## Current source and checks

Main efc27e7, ABI 21, merged through merge-green and pushed. Both DLLs/launcher,
all 60 CTests, 1232/1232 runtime shader files and QA pass. Includes STI material
producer, exact ST depth extension, TileBlend camera coverage, Hair receiver guard,
and split alpha spill. All experimental defaults remain OFF. Integration session
45105 is TERMINAL success; no running merge. Test tree refreshed to efc27e7.
Root: C:/Users/quali/.codex/worktrees/omm-bake/BG3RTX, codex/omm-bake, df7ef89.
Root merged current main; pending source beyond main is current-Sun-only TileBlend
shadow 2e01c3c/92f9995 (a80fdea/b572489): host links and five combined CTests pass.
It reads only seven used floats/28 bytes, reuses the exact two-view predicate and
owned sampler19. Global camera/legacy lookup remains unchanged. Independent
source/remap review clear; no new HLSL/layout/ABI. GPU cost and game quality unknown.

Main evidence: M1 639 old SPV identities/604 ON entries; TileBlend camera 640 OFF
compiler-input identities/42 actual OFF SPV identities, 604 ON validations and
80 interfaces. Actual depth patches validate with frozen OFF identities. STI M3
785 CPU cases/816 native module, 96 OFF identities and 96 ON validations. Native
family counters 287/288 fit capacity 289; require nonzero runtime participation.
Three separate windowless GPU proofs: 32 coverage cases/four real queries;
14 material-field cases/closest_hit only; 22 receiver-helper cases including
nonzero rebase/negative control. All zero Vulkan validation errors. Root inspected
raw/results. These are synthetic, not native graphics-XFB, game image or cost proof.
SDK include failure was repaired by root external junction to main external;
248/248 SHaRC retry passed before integration. Never clean either junction tree.

## User regressions and active owners

Personal Hair Scalp trial hair-origin-live 047e105 ABI 19 stays untouched. Native
Hair/Scalp/physical origin ON, 1408/587 MiB, OMM/current-Sun OFF, dev1. User reports
foliage flicker improved and hair color more native, but rug/plank dark rectangles
and fixed Astarion hairline band remain. Do not call these screenshots resolved.
Preserved four screenshots/log/config: root captures/user-camp-regression-20261031;
manifest docs/evidence/user-camp-regression-20261031.json. 77 log windows: 11 residual
alpha substitutions (0.01-0.92 draws/frame), last three zero; opaque drops all zero.

532879c/c0aa8ec guard corrects class5 receiver ambiguity (Hair/Pubes AND Scalp),
retaining virtual origin for competing Scalp/unreadable/capped queries. No alpha,
occluder or bias change; 31 CPU checks/16 OFF identities/16 ON validations + GPU22.
7785ba1 rt_alpha_spill=0 preserves alpha records/procedural Hair when free opaque
storage rescues a full alpha partition. 105 budget/335 settings checks; schema and
Experimental GUI included. Recipe failure/two full ranges remain unresolved;
AO/GI UNKNOWN policies are conservative, not universally native fallback.
Neither code defect is yet causally attributed to the user's particular pixels.

M2 omm_alpha_bounds owns codex/native-tileblend-legacy (m2-foundry-mesh tree):
material-aware F1 bridge for exact shadow family, internal structs/existing pool
and descriptors. UVs survive conversion; only dual currentSun/legacy capture
compacts ranges. Preserve destination offsets, immutable source generations,
opaque complements, all F1 instances after Sun base, legacy TMin, mode provenance
and per-scene incomplete header12. Contract approved; no public ABI without root.
Sun is independent reviewer; completed agent must be woken with followup_task at
coherent checkpoint (send_message alone does not wake it). Contract owner implements
bounded TileBlend GPU mode in existing probe from e660cf8; one brief invocation.

## Experimental GI remains unqualified

Old R2 GI ON images have real dark stipple. Descriptor layout FIXED in main
9d455ca (dedicated GI19/20 layout/writes restore8/11/13/17); reference rebase fixed
8fb805a (camera48). Actual compiled interfaces are checked by the shader compiler,
including Hair/HPO/OMM/SHaRC. Never use the filtered LS set0 layout or qualify
quality from positive counters. gi_hit_direct/rt_hit_reference stay OFF; image
recheck draft remains outside pending inboxes. No focused FG/SE run now.

## Actual GI/reference evidence

R8 a0bd782: VALID 498 s, FG2/RR1 real presentation, seven static phases.
Primary OFF/material/direct/two/OFF: 99.525/99.524/99.524/99.522/99.527%.
Material median 2246464 qualified hits/window. Two-surface positive indirect
438-15389/window, 166766 total. Limited supported point/spot reference only,
not a complete room or unbiased mixed image. Stats-off whole GI direct 2.429 ms,
two-surface 2.442 ms; overlapping ranges, not isolated bounce cost or speedup.
Raw/derived evidence: root captures/native-hit-material-20261031/r8/.

GI R1 d894b06: VALID 504 s, no NVIDIA 153/dumps/build drift. Static primary
99.524/99.524/99.525%; 2941129 nonzero additions in 25 ON windows, OFF none.
Trace GPU median OFF 0.345 / ON 0.442 ms (+0.097 ms). Rendered SDK cadence
84.14 / 75.56 fps; total overhead attribution UNKNOWN (no phase CPU rows).
Whole GI 2.380 / 2.243 ms does not show a speedup: excludes native lighting,
producer work, CPU, NGX/FG/present. Rotation is CONFOUNDED by synthetic reset
jumps up to 78.884 deg/frame and 15.247 m/frame, baseline also affected.
53f2167 fixes emitted release/reset/press timing, 41 CPU checks pass; live pending.
051333f shares primary warmup/dispatch shader selection, stopping pipeline flip;
core links/two focused tests pass. R2 capture phases already disabled that churn.

GI R2 d894b06: terminal 386 s, no NVIDIA 153/dumps/build drift. Queue INVALID
solely because frozen expects said transport valid while actual output says
transport ok. Future draft corrected; never retroactively alter the receipt.
Existing QA independently validates three complete K16 bursts/176 samples each:
private captures/pan-20261031-080416-62101 OFF, 080526-62102 ON, 080636-62103 OFF.
Do not repeat 7 GiB solely for a regex typo. Actual captured FG active all frames.
769563 pixels stable across all 48 frames; GI mean +0.495%, but final crop has
1144 stable mean-low pixels, GI visibility alpha/diffuse also affected. IMAGE
QUALITY FAILS locally despite zero negative GI-radiance envelope pixels.
Contact root captures/gi-hit-direct-20261031/r2/gi-stipple-off-on-off.png.
Detailed streaming analysis in native-hit-material/captures/gi-hit-direct-20261031/
r2-analysis/. Preserve queue invalidity separately from valid capture transport.

## Material, terrain and light inputs

ABI 21 provider uses independent deferred snapshots, canonical actual SSBO/UV/draw
identity, full GPU clip/UV/parameter proof, actual submit pins/publication and
query-owned descriptors. R7 first qualified hits after fixing post Push stats.
128 material rows reuse exact resources within 16 sampler/64 cache limits.
Fresh opaque VT 940d/1999 maps to deferred 3812/8995; private InstInfo bit13,
rows 8192..8319, ordinary opaque visibility/FORCE_OPAQUE unchanged. Cached/held/
instanced/unknown shader families unsupported. Old a15 counters collide: exclude.
Stats 269..280 reference paths, 281 positive indirect, 282..286 GI contribution.

R5 terrain acquisition PASS: frame 4521, copied 1/declined 0/pending 0;
80/7168/16464/2048-byte packets, shader reads needed for usages 16/34.
Actual-submit lifetime/stages fixed. Real mask/scalar/LUT evaluator matches all
16464 painted bytes, 12 scalars/two RGB colors per brush. Base 0/255 is valid.
Runtime texture-to-DDS identity, derivatives and normal projection remain open.
Prepared metadata extension uses the EXISTING terrain request, exact family,
actual declared set2/binding32 range capped at 4096 with explicit missing/unknown
status; no GPU copies, DDS guesses, key or ABI. Ordinary trace caps unchanged.

M2 has 27 native-qualified cell components/24506 triangles, persistent terrain
13512 triangles/738 holes; nine authored components unknown. Low-vertex line
intersections do not prove opacity or room closure. No invented roof.
SE absence fix requires complete current membership; installed Lua unchanged.
SE native Blackbody read-only console helper is merged but NOT live-qualified.
Canonical receipts/markers before writes, fresh prompt/single-line failure safety
reviewed/tested. Uses transient Shift, no installed config/mod/profile edits.
Its console may activate itself; defer while the user is at PC. No Planck or
white/intensity-selector photometry adapter. Actual exports contain 124/89 lights.

## User trial and remaining program

Desktop Hair Scalp trial: hair-origin-live 047e105 ABI 19, untouched. Hair/Scalp
and physical-origin ON, camera/cascade 1408/587 MiB, OMM/current Sun OFF, dev 1.
User has NOT checked latest capacity/physical-origin updates or any new GI.
UI recycled descriptor fix user-confirmed. Hair origin preserves virtual primary/
history and starts secondary rays at the selected physical card; K16 verified.
True RTX strand audit decoded one real Shadowheart style: skinned triangle cards,
material presets and bone springs, no proven connected fiber endpoints/radii or
deformation mapping. Keep native cards; not universal proof that grooms cannot exist.
Sun half-angle 0.00459 rad is correct; native-mask guard retains hard edges until
actual caster coverage is established. Never enlarge/blur/bias to hide geometry.
OMM wired OFF, not game-qualified. M4 offscreen gains/full M6 remain incomplete.

Only test-queue launches games, with canonical USER_PLAYING/unowned-game guards.
Never parse/change saves, write recovery backups, force-push, change snapshot tags,
clean junction trees or hold results.md open. Main only merge-green; push main and
source branch afterward. All new experimental defaults remain OFF until qualified.
