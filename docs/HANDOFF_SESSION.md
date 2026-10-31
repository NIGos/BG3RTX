# Session handoff - 2026-10-31: fix the experimental GI descriptor contract

Read PROGRESS_CODEX.md next. Full RENDERING_EXECUTION_PLAN M0-M6 remains active,
not complete. User wants pragmatic scoped checks, autonomous progress and useful
parallel offline work. Latest steering: user is at the PC; keep windows behind
other windows. NO BG3/game launches now. Standalone GPU tests are allowed (latest clarification);
keep CPU/GPU loads short/low priority while the user plays another game.
Existing foreground batch receipts are terminal, not authority for another run.

## Trees and immediate next action

Main d31633b, ABI 21: M1 coverage/capture budget merged through merge-green and
pushed; 58 CTests, 1231 shader files and QA pass. Root development:
C:/Users/quali/.codex/worktrees/omm-bake/BG3RTX, codex/omm-bake, 2ad5b4e.
Pending integration: M3 STI material producer/glue (309b9dd), ST depth extension
(35f837d), existing windowless GPU fixture extension (2ad5b4e). Both DLLs link;
785 CPU cases / 816 with native VS; 96 key-OFF identities, 96 key-ON compilations,
80 interfaces pass. Family counters 287/288 and capacity 289 are wired.
M1 has 639 old-entry identities, 604 new validated entries, 160 interfaces.
Windowless RTX 5090 fixture passed 32 cases, four real queries, zero validation
errors. Native graphics-XFB, runtime participation, field agreement, quality and
GPU pass cost are NOT proved. Material GPU fixture owner: omm_contract_review.
ST depth coverage has its own bindings; ST material decoder is unsupported.
All experimental defaults remain OFF. Private native-hit-provider-live stays
frozen d894b06; personal Hair trial stays 047e105. No game launches now.

CRITICAL: old experimental GI used filtered LS set0, omitting GI 8/11/13/17;
R2 ON images have dark stipple. Source FIXED in 9d455ca (b231eb8) with a dedicated
layout/write contract; rebase references separately fixed in 8fb805a (1ca79d5) for
missing camera 48. Eighty actual compiled interfaces and three focused tests pass,
independent review clear. Image recheck pending; keep both keys OFF. Existing shader
compiler now checks these host interfaces after a normal build. Include Hair/physical-origin/OMM/SHaRC variants and actual descriptor count
<=32. Do not merely add four bindings to the filtered LS mask: extra-image masks
also differ for GI versus LS. No shader math, blur, bias or proof relaxation.
Sun independently found both defects and reviewed both fixes clear.
No source edits in the frozen private tree; future fixed image A/B needs new
appropriate live steering. Do not close visual quality from positive counters.

M1/M3 source and ST extension owners have completed their source work. Root owns
main integration and concise shared docs; omm_contract_review extends the existing
compute-only GPU fixture for actual material fields, not a new renderer/harness.
Sun optional native graphics-XFB fixture exceeded a small extension and stopped;
this is a fixture scope limit, not a rendering impossibility. No Ponytail.

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
