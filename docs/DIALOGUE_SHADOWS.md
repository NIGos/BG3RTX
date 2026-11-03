# Dialogue shadows and distant casters - 2026-10-30

## Moving band isolated to legacy cascades (2026-11-03)

New decisive user A/B: disabling Roof sun shadows (`rt_cascade_casters=0`) stops
the band across a still character in port/camp dialogue, with RT sun still on.
This supersedes the earlier lack of a causal A/B for this temporal symptom.
Code admits skinned opaque cascade draws when the camera copy has a different
placement-buffer offset: full draw-key dedupe misses; mesh-only dedupe applies to
discarding draws only. The admitted copy is F-1/F-2, so it can shadow the current pose.
Native dumped cascade VS 1cf36b97d3067b6c fails the affine-instance test (skinned);
646a930e41f002bb passes (static), and wind VS 50fbfceb06192473 fails.
These fixtures verify the filter's classification, not which draw hit the face.

Candidate `rt_cascade_static_only=0`, off in presets: exclude dynamic/unclassified
legacy copies; preserve camera geometry and raw Current Sun. No shader change.
Design/limits: [TRACK_D_SUN.md](TRACK_D_SUN.md#dialogue-legacy-cascade-filter-2026-11-03-ready_for_live).
Status READY_FOR_LIVE, not a visible-quality pass. Planned port/camp A/B, counters,
roof protection and cost checks: [progress/fix-cascade-dialogue.md](progress/fix-cascade-dialogue.md).
No game launch; the attribution branch and its prepared jobs were not edited.

## Allocation mismatch follow-up (fix-cascade-dialogue2, 2026-11-03)

The 03:09 ds3 run disproved the previous default-path claim: mesh-offset
matches stayed 0.0 in all three saves; Dialogue1 stayed at 335 seen / 60 duplicates /
113 added. At the port a still chest changed by -0.073 block luminance; disabling
cascade casters lit both characters (+5.8% whole frame).

The relaxed key required an equal material buffer/offset/range, vertex output
buffer/offset and an audited camera VS model layout. Per-draw allocation breaks
the first two; an unlisted VS yields key 0. No captured compute-skinning source
lineage proves that different output regions belong to the same character.
Index range alone cannot distinguish two placed instances of the same mesh.

Use the existing off-by-default `rt_cascade_static_only=1` fallback for skinned /
dynamic / unclassified legacy copies. Its affine, known non-storage position
filter already excludes the audited skinned VS and storage-backed positions.
Opposite-policy F-1/F-2 soups are rejected after toggles. Camera-pass geometry
still casts the current pose; unknown off-camera static
roofs may be lost. The default value 0 still permits unmatched skinned copies.
The added key-0 counter measures identity coverage before admission filtering.
Identity can ignore material allocation while keeping proven geometry,
model, placement and pulled-instance identity. It cannot prove a skinned match
when vertex output regions differ. No live quality or GPU cost is claimed.

Revised ds1-ds3 jobs compare static_only 0/1 and motion pairs in one run, outside
the watched queue. Pair analysis requires exact (0,0) registration against OFF
neighbours; the every-window static-camera log gate is removed. Port character
motion must stay within RT-sun-off noise (0.002 block luminance), with the
nocascade gap falling to the camp level. See the batch predictions and
[progress/fix-cascade-dialogue2.md](progress/fix-cascade-dialogue2.md).

## Earlier investigation

The user separates two new symptoms from the resolved UI blue-block issue:
- STATIC face shadows, hairstyle-dependent; not face flicker.
- Distant shadow changes, suspected trees near cull/LOD distance. Camp and other
  dialogues also show strange shadows. The user is collecting more F10 captures.
Save reference: Test1. No save files were read or changed. Live tests will be
coordinated in the root conversation; no game/settings/binary/shader changes were made.

## Confirmed capture/source evidence

Completed F10 frames 57830/58438/60034/64487: captures/20261030-161807,
-161818, -161845, -162008. All have zero reported non-finite dump components.
Around these frames scene wait has zero timeouts/skips; logged RT windows use
current TLAS 300/300, previous/none 0. No evidence here for the old whole-scene lag.

The Shadowheart polygonal cheek/neck shadow is already in post-lighting diffuse
-161818/d471; Lae'zel's face/neck shadow is already in -161845/d694. They precede
compose, DoF, RR and FG, so post reconstruction is not their first visible stage.
These are separate single frames, not a same-pose A/B or consecutive-frame flicker gate.

Actual depth-prepass shader 5a03e5b678b2d040 occurs in both close-ups (1 and 3 draws).
Local asset DB identifies Characters/CHAR_Hair:StaticDepth, mask s1b4
ID_Depth_Root_Alpha_MSKA. Shader 9e77575a9f377ec5, Characters/CHAR_Scalp:StaticDepth,
mask s1b2, additionally occurs in Shadowheart's prepass. Both conditionally discard
pixels; both are absent from shaders/rt/alpha_fs.txt. All 15 CHAR_Hair fragment
variants are currently absent; supported scalp shadow entries are only surrogates.

geocapture.cpp:1874 filters always-discard FS only. At line 1956 alpha capture requires
alpha_lookup to succeed. These capturable conditional hair/scalp draws therefore
follow the opaque geometry path, which can make transparent cards cast solid
polygonal shadows. This is a concrete unsupported-coverage defect consistent with
the user's hairstyle dependence; exact ray/caster attribution still needs a live gate.

The native hair recipe cannot be represented by a plain texture-alpha cutoff:
- Hair: noise s1b3 shifts mask UV, combines two s1b4 samples, root/depth channels
  and material parameters, front-facing tangent/view coverage and frame/pixel dither.
  The depth prepass also displaces gl_FragDepth; XFB retains vertex geometry instead.
- Scalp: smoothstep of mask.a times root/density attenuation using mask.b, then
  threshold 1/3 or screen/frame dither. Existing raw-A shadow entries are not exact.

Sources: captures/alpha_cache/{5a03e5b678b2d040,9e77575a9f377ec5}.glsl,
layer/geocapture.cpp:1874/1956, layer/alphatest.cpp, shaders/rt/alpha_fs.txt;
asset identities verified with python -m tools.assets shader HASH.

## Separate distant-caster risk

Current base/user settings: rt_cascade_casters 1, rt_cascade_alpha 2, dedupe 1.
At geocapture.cpp:1109 mode 2 skips known alpha-table cutouts, but inserts OTHER
conditional-discard shaders as opaque, assuming they are only LOD/camera fades.
That assumption is not generally true: unsupported texture-cutout materials exist
in the user's actual D16 cascade passes. The inventory below establishes that the assumption is false.

| Missing FS | Asset DB family | Native D16 inventory |
| --- | --- | --- |
| 378c2a63a11d4076 | ENV_Tileset_MultipleTextures, three sampled alphas then discard < 0.5 | 30 draws in 162008; 22 in 162506 |
| 5c37b87299d86ce1 | ENV_Base_TileBlend_Test_Alphatest_2s_Translucency, two textures feed discard | 4 draws each in 163004/163010 |
| ccf72d548a0b69fd | VFX_Model Skydome Mountains, sampled red times vertex alpha | 5 draws in 162506/163004/163010 |
| 34951669ed4b3b0f | CHAR_Hair, three texture reads feed coverage/discard | 11 draws in 162008; 3 in 162506 |

All four are absent from the alpha table. Counts cover ALL native D16 passes,
not proven post-dedupe RT captures in selected cascade 2. Environment template
names do not identify a particular tree; skydome mountains are not foliage.
Some unknown FS are genuinely nontexture dither/fade and need a different policy.

The dedupe rule skips a discarding cascade mesh while its camera-pass key is
present; after it is absent, mode 2 may capture the unsupported texture cutout
as solid. This is a concrete transition mechanism. F10 events lack VB/IB/range/
placement keys, actual dedupe outcomes and depth-view layer, so they do not prove
which mesh entered the RT BLAS or which distant shadow pixel it affected.

A cull/LOD transition, cascade copy and the camera-pass dedupe can therefore alter
occluder coverage; a single F10 does not establish the exact distant tree or flicker.

## Solar softness question

Current rt_sun_radius 0.00459 is a half-angle, approximately 0.526 degrees full
angular diameter. rt_shadow.hlsl122 samples a uniform finite disc, one ray per pixel;
core/gi.cpp1243 passes the configured value. It is not a zero-radius hard light.
Characteristic penumbra width is about distance*2*tan(radius), or 9.18 mm per metre
of caster-receiver separation. Hair a few centimetres from skin can have subpixel
penumbrae. RR reconstructs the 1-spp result; untrusted regions retain native masks.
A hard edge alone is not proof of a wrong sun radius. Do not enlarge the sun to hide
unsupported hair geometry or force a blur/bias as a claimed physical correction.
The prism's appearance alone does not establish a runtime light; a native placed
light is still possible, even though our separate emissive-capture feature is off.

## Next correction / later live discrimination

### Native shader inventory before implementation

Bulk catalog audit covers 1,149 templates / 17,235 variants / 15,363 material
modules. Hair class/flags and hair/scalp/beard/fur names select 17 families with
234 distinct fragment programs. Independent eyelash-name review adds three:
CHAR_LashesShadow, CHAR_LashesShadow_Dithered and the cinematic LashShadow effect.
The combined 20 families contain 273 distinct fragment programs, 83 with retained
SPIR-V/GLSL and only 22 listed in alpha_fs.txt. Presence in that table does NOT
certify exact native coverage. Hair has 15 fragment programs / zero table entries;
Scalp has 14 distinct programs / four shadow-only entries. Counts overlap between
families and do not count distinct hairstyles or objects.

Full per-hash/pass/cache/table/F10 inventory is retained in
captures/dialogue-shadow-review-20261030/hair-inventory.json. Native cached lash
depth/shadow shaders also contain nonlinear sampled-mask coverage, so eyelashes
must not be silently left outside a hair-adjacent audit. Their presence in a
forward draw does not prove that their depth variant entered the captured BLAS.

All nine Hair/Scalp fragment programs missing from the runtime cache were recovered
from read-only Materials.pak, hash-verified and CPU-decompiled into the report-only
native-hair/ subdirectory. All 29 distinct Hair/Scalp fragment programs therefore
have native GLSL available. This establishes source availability, not an exact RT
implementation or equivalence gate. Use all SREF-to-MVAR variant references for
pass membership: SHDR's first/canonical pass alone can hide shared-hash variants.
Concise versioned evidence: docs/evidence/hair-native-inventory-20261030.json.
Native archive verification found all 273/273 selected fragment hashes in 300
matching BSHD containers, with zero missing bodies or read errors. The remaining
task is faithful coverage support; no selected native binary was unavailable.

Scope: manifest built 2026-10-26 from 27 packages in the game's Data directory,
including Hotfix 9/10. The catalog is not a census of separate Mods directories,
loose overrides or future runtime variants. Its extractor accepts exact-case Vulkan
ST/STI DEF/FOR/EMI/DEP/DEPS/DEPST/VEL/BAKE names; other names can be omitted. The
reported zero unreadable BSHD inputs does not certify unmatched inputs or malformed
containers returning zero modules. Missing retained source must be searched in
native package bodies before calling a shader unavailable.

User steering: exhaust native shader discovery and coverage mapping BEFORE choosing
an unknown-shader workaround. "Unknown to alpha_lookup" does not mean unidentified
or unavailable native code. scripts/alpha_table.py scans retained runtime dumps and
recognises two limited cutout families; it is not an exhaustive native-shader audit.
Inventory package/catalog variants, exact hashes, parameter bindings and native
coverage formulas, including depth and shadow passes. Distinguish missing runtime
cache files from missing packaged SPIR-V. Check observed F10 shaders against this
inventory; catalog completeness alone cannot prove coverage of future/mod shaders.

Then model hair/scalp coverage faithfully in every visibility, closest-hit and
ray-origin query. Any unavoidable fallback needs a documented remaining limitation
and is not the default solution to a missing table entry. Do not skip all hair,
label it opaque, or add a simple alpha>=cut entry as an exact implementation.
OMM must not accelerate
the existing wrong coverage; its preparation remains in an isolated worktree.

Later user-led, SAME-pose diagnostics: RT sun off/on, local shadows off/on, then
box lights off/on if needed. For distant casters separately compare cascade casters
off/on, then retention 0/4 if justified. These tests were NOT run and no settings
were changed during the user's play. Unsupported coverage is established, but its
share of the observed face shadow and the distant-caster transition is not quantified.

Artifact index and current log snapshot: captures/dialogue-shadow-review-20261030/.

## User follow-up - 2026-10-31

User reports OMM/Current Cascade Sun look encouraging, but a slight dialogue
shadow flicker remains and may depend on environment. This is a temporal symptom,
distinct from the older fixed hairstyle shadows above. No causal A/B is available.
The full 12:14-12:18 personal-trial log has one window with 368956 actual Current
Sun samples, then later scene-unready fallback windows with zero samples.
Changing views and aggregate windows do not prove a flicker-causing transition.
Separate native-mask visibility from added RT visibility before changing shaders.
See docs/evidence/dialogue-flicker-user-feedback-20261031.json for all eight windows.

Independent follow-up: no demonstrated guard defect. The 5005 combined epoch/
late-write rejects include initial capture/epoch/camera/cascade/receiver failures;
all 41 prepared builds were inserted. These counts do not prove late writes.
Positive GPU scene-unready counters come from the Current Sun shader itself,
not just ordinary host fallback. Exact failed readiness conditions are not logged.
No guard relaxation or renderer fix was made. User paused further work; temporal
dialogue evidence remains necessary. See current HANDOFF_SESSION.md.

## Attribution batch 2026-11-02 (prepared, not run)

User report: intrusive shadows on the characters themselves in dialogues; saves `Test Dialogue1-3`.
The user's session that made them (bg3rtx.log 06:24-06:32): preset=ultra + ls_box=2, hit materials,
rt_alpha_spill=1; stride 64, so native Hair/Scalp OFF (hair cards opaque); alpha made opaque 0.00 and
scene wait 599/599 this frame's scene. Code reading per hypothesis, most plausible first:

1. **H2 cinematic lights shadowed by D-LS.** ls_owners=31 traces character-owned lights (15 left them
   unshadowed), ls_noshadow=1 traces lights whose template sets neither Shadow nor CastShadow (only the
   darkvision light is skipped), ls_box=2 traces every box light (rt_local_shadows.hlsl:146-158,
   core/gi.cpp:1895-1896). The owner and art rules act only on lights joined to per-light data;
   unmatched lights and box lights are always traced. Dialogue rig lights near the face, which the art
   leaves unshadowed, get the head, hair and shoulders as casters.
2. **H1 hair/scalp cards opaque.** Hair/Scalp FS are not in the alpha table; without rt_native_hair they
   take the opaque path (geocapture.cpp:2330-2332, alphatest.cpp:551-565). Same defect as the 10-30
   static face shadows above. Compounds H2 (opaque cards also block rig lights).
3. **H5 cascade casters (Ultra rt_cascade_casters=1).** The sun-only cascade soup is frame F-1/F-2's
   (rtscene.cpp:931-965). An opaque cascade draw is a duplicate only on an equal draw_key that includes the
   per-draw UBO / instance-stream offset (cascade_casters.h:46-54, 72-77; geocapture.cpp:1088-1104); the
   user log dedupes 22 of 264 cascade draws per frame. An animated character can be in the sun TLAS twice,
   the copy 1-2 frames old and possibly a shadow LOD: a self-shadow offset on skin. Hair DepthShadow FS
   34951669 is not in the alpha table: with rt_cascade_alpha=2 it goes in solid unless its mesh key
   matches the camera pass (geocapture.cpp:1270-1290).
4. **H3 stale pose, held draws.** The camera soup is this frame's (scene wait), but with a static camera
   rt_dyn_hold=4 keeps absent draws of at most 8192 triangles for up to 4 frames (bvhsplit.cpp:1333-1356,
   common/dyn_hold.h). The draw identity includes the vb[0] buffer and offset (bvhsplit.cpp:1037-1043): a
   character draw whose identity changes per frame leaves 1-4 older poses in the TLAS (also a flicker
   candidate for P4_OMM sec 10).
5. **H4 AO.** gi_ao (core/gi.cpp:1857) is 1-ray RT AO on characters: soft crease darkening, not a hard
   shape. Contact shadows run only when rt_shadows=0 (core/gi.cpp:2816): ruled out in Ultra.
6. **H6 lashes/beards opaque.** Unlisted cutout FS (lashes) are opaque as in H1; a full alpha region also
   makes listed ones opaque (geocapture.cpp:2364-2381). User session 0.00 per frame; the 10-31 trial
   build (stride 88 at a 384 MB buffer, before 64c0f5d) had 28-31 per frame (cb7 log).
   Also noted: skin and hair sun rays are traced from the far side when back-lit (rt_shadow.hlsl:87-93).

7. **H7 wind-animated foliage (user: moving shadows in dialogue, none at the same spot in the isometric
   view).** Every camera-prepass draw is captured from its own VS output each frame, wind included
   (geocapture.cpp:5-11); wind VS never match the instancing pattern (spirv_xfb.h:312-320), so they are
   never cached (bvhsplit.cpp:29). The RT sun therefore sees the current wind pose of every tree the
   dialogue camera draws; the ground-level camera draws canopies the isometric one does not. Off-screen
   trees come only from the F-1/F-2 cascade soup. The game's own sun shadow comes from its cascade passes;
   their caster set, LOD and whether their VS applies wind are not verified here. RT uses the game's
   lighting sun direction from the camera UBO (rt_shadow.hlsl:80), so a dialogue rig that moves the sun moves
   RT and lighting together. No key removes foliage casters only (none in common/settings.h): the batch
   uses PNG pairs 0.5 s apart with RT on and with the game's map only, rt_cascade_casters=0, and rt_debug=2.

### ds1 result (Test Dialogue1, 2026-11-02 10:52, ran to the end, INVALID only on gates)

Outdoor camp, daylight, two characters in close-up; the dialogue camera was static in every shot (it still moved
until 41 s after the load: a settle-cam phase now precedes shot-off-a). Character ROI = G-buffer class skin or hair
(5.2% of the frame); preview linearised; OFF shots a-e spread 1.9% whole frame, 12.9% ROI (dialogue animation).

| shot | ROI vs OFF mean | whole frame | look (crops) |
|---|---|---|---|
| noao (gi_ao=0) | +35.1% (neighbours -5.5 / -2.8) | +4.9% | hair and the face under the hair clearly brighter: the only obvious change |
| nosun | +15.1% (neighbours +7.4 / +2.1) | +3.0% | slight; rt_debug=2 shows RT-only sun shadow (red) on the hair |
| nocascade | +10.3% (-5.5 / -2.8) | +1.2% | slight |
| noart / nobox / nolocal | +8.6 / +7.8 / +7.5% | +1.7 / +0.9 / +2.1% | within the ROI spread |
| charlights / nohold / hair / hairorigin | +1.0 / -4.1 / -7.7 / +4.5% | within spread | no visible change |

Verdict for this save: the character darkening is mostly the RT AO (gi_ao, 1-ray ambient visibility; hair cards
are opaque in it) on hair and the face under it, beyond the OFF spread. The RT sun adds a smaller hair shadow.
Local, box, art-unshadowed, held-draw and native-hair toggles stay inside the ROI noise here (no rig light dominates:
ls_debug=9 shows unknown visual lights on the face). H7 moving shadows: the PNG pairs were taken 3 frames apart
(the trace queue), too close to show motion. The R8 sun mask in the trace is the game's own, dumped before the RT
fold, so the folded mask is not in the trace: PNG/ROI numbers stay coarse. A per-pixel occluder diagnostic is the
next step (planned, not built). Gates fixed for ds2/ds3 (see the batch README).

### ds1-ds3 result (batch dsh-20261102h, 13:46-13:58; INVALID only on the static-camera gate)

Shots whose window had a `camera moving` scene build are discarded; each toggle against its two nearest static OFF
shots; ROI = skin + hair class px. OFF ROI spread: ds1 12.9%, ds2 15.1%, ds3 12.5%.
- ds1 camp: noao +42% (frame +5.9%); everything else within the spread (nohold +10.5%, hairorigin +13.3% borderline).
- ds2 camp: noao +31.6% (frame +13.6%); hair +19.5%, hair+origin +30.9%; nosun +13.8% at the spread.
- ds3 port, under a roof: hair+origin +26.9% (hair alone +32.5% but its window moved); noao +18.3%; nocascade,
  noart +9% within the spread.
Verdict: the character darkening is the RT AO (1-ray ambient visibility at gi_ao=1) and, under the port roof, the
opaque hair cards (native hair coverage brightens the characters by about 20-30%). No local-light, box,
art-unshadowed, held-draw or cascade toggle exceeds the spread. The camp "moving shadow" is not resolved by these
shots (the PNG pairs are 3 frames apart). Next: rt_occ_stats per occluder kind for AO and sun on character px.

### Occluder-kind diagnostic `rt_occ_stats` (built 2026-11-02, READY_FOR_LIVE)

Key `rt_occ_stats` (0, live, diag windows only). Layout and log line: common/occ_stats.h (unit test
tests/occ_stats_test.cpp). Receivers: character px (G-buffer class 3 skin / 5 hair). One "occ stats (N frames)" line
per 60 frames read back: per source (sun, local point/spot, box, AO) the px evaluated per frame, darkened %, mean
darkening %, rays / occluded / unknown, on-screen hair/skin share of the occluded rays (the hit is the visible surface
at its pixel), per kind `rays % [near %] dark %`, and the occluder distance histogram (<0.05 ... >=5 m, dark % and rays).
- Kinds: unknown (aborted), cascade copy (id 0xFFFFFC..), static (pool, opaque), moving (soup, opaque: characters,
  skinned, wind-animated opaque), cutout-static (pool + alpha record), cutout-moving (soup + alpha record: wind foliage),
  scalp (alpha kind 3), hair (alpha kind 4 or native procedural), held (rt_dyn_hold copy: InstInfo meta bit 31,
  layer/bvhsplit.cpp kInfoHeld; layer change, restart, no ABI change). "near" = t <= 0.5 m (self-shadow proxy).
- With diagnostic constant 1011 on, shadow rays find the CLOSEST occluder (no accept-first; same visibility).
- Sun and AO: gi/rt_occ_stats.hlsl, its own pass after the timed RT region (so no rt_shadow/GI variant doubles). Sun:
  the darkening is the real pass's (rtVis g x (1 - r)), the kind from a replica of rt_shadow's ray (same seed, disc
  sample, origin; mismatches counted); not valid with rt_sun_cascade_current=1. AO: the darkening is the real
  1 - giRaw.a, the kinds from replica rays (same seed, directions, origin, gi_rt_radius fade; after the first ray the
  hit-material passes draw other random numbers, so same distribution, not the same rays; gi_far not replicated).
- Local/box: the every-mode box pass with RT_SPEC_OCC_STATS (SpecId 1011, its own Pipe g_ls_full_box_occ);
  ls_box=0 gives no local numbers. Per ray darkening = removed light / the pixel's lit diffuse + specular.
- Counters: device-local g_occ_buf at set 4 binding 1 (the box statistics set), groupshared reduce, one atomic per
  counter and group, copied into a host ring after the D-LS pass; read 7 frames later (host atomics lint classified).
- Migration: set 4 binding 1 is always bound. RT_OCC_STATS is retained as the checker reference and in the
  standalone sun/AO diagnostic; the full-box production request uses constant 1011 for both values.
  Key-off equivalence is checked after specialization, freezing and optimization (spec_cascade_check --key occ).
- Found while wiring it: the box D-LS variants (rt_local_shadows*_box*.hlsl) are not in shader_variant.h
  cascade_entry, so with rt_cascade_casters=1 they trace 0xFF and the F-1/F-2 cascade copy shadows local and box
  lights (rt_visibility g_rt_class 1: its unknown alpha is solid). FIXED 2026-11-02: the five box files are cascade
  entries (constant 1000: trace 0x01). Both mask values share the migrated module. Live A/B: ls_box_cascade_old=1 restores the old 0xFF box passes (diag-occ-boxold, shot-boxold-on);
  the 'cascade' kind under local/box must be > 0 there and 0 in diag-occ-base.
- Float energy of the character mask per shot: dump_preview_f32=1 (layer/dump.cpp) writes the 5c3fdabc scene colour
  as float32 at the preview grid (~10 MB per trace); tools/live_batches/dialogue_shadows/char_energy.py takes its mean
  over the RT2 class mask (tests/char_energy_test.py).
- Cost: an extra full-res pass (1 sun ray + gi rays per character px) and the every-mode D-LS box pass; GPU ms not
  measured yet (diag windows only, never in cost windows).
- Scenes (save_index.py, read-only): Dialogue1/2 WLD_Main_A (camp, moving shadow), Dialogue3 CTY_Main_A (port, very
  dark under a roof). Predictions per save: tools/live_batches/dialogue_shadows/predicted.json ("occ").

### Port moving band (user screens docs/dialogue_user_screens/7-9.webp, Test Dialogue3, 2026-11-03)

A dark band moves over the left character's face/torso between frames in bright sun; the other character does not
move. Ranked (predicted.json ds3 "port-moving-band"): held/stale or mismatched dynamic geometry (rt_dyn_hold pose, a
per-draw BLAS of another animation frame or of the previous camera cut), moving foliage/cloth/rope/crane in the sun
path, his own animated hair/cape/strap, then the static other character. Windows in the first held shot:
`diag-view-occ-first`, then `diag-occ-frames-hold4` / `-hold0` with `rt_occ_stats_frames=4` (a line per 4 frames:
kind and distance changing line to line = a moving occluder; 'held' vanishing with hold 0 = stale geometry).
Not yet: fixed face/chest probe pixels (shader change, after the spec-constant migration).
If a static character-on-character sun shadow turns out to be the cause (physically right, absent from the game's
cascade in dialogue framing), options: keep RT (correct) but soften by the true penumbra (sun_radius already does);
take character-on-character sun occlusion from the game's own cascade (min with the game's mask for character
receivers hit by 'moving' occluders); or exclude character casters from character receivers in dialogue only. None
chosen; the A/B decides.

User 2026-11-02: the saves load already in dialogue; the shadow is on all characters; other dialogues
show wrong hair shadows (run without the native hair keys).

Batch: tools/live_batches/dialogue_shadows (README, predicted.json): one visible_unfocused_fg job per
save, 260 s of plan, 22 PNG traces at 1024 px (about 1.9 GB for the batch), every toggle inside one run at
a static dialogue shot; a `camera moving` line flags that shot (excluded from comparisons, counted; no longer a gate).
