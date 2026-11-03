# Animated material coverage (anim-materials, 2026-11-03)

Status: partial implementation, not ready for live qualification. No game was
launched. Git metadata, paks, LSLib and shared asset DB files were not modified.

Full inventory: `captures/anim-materials/census-final.json`. Reproduce with
`python -m tools.assets.animated_materials --catalogue <mesh_materials_v2.json>
--compact <compact_v4.bin> --out <NEW.json> --cache <workspace-cache>`.
All 1,063 catalogue templates are scanned, both static and instanced variants of
Deferred/Forward/Emissive/Depth/Velocity, checking actual SPIR-V loads from the time
UBO. Shader SHA-256 caches avoid re-parsing unchanged SPIR-V. Full template scalar
and vector controls come from the installed paks; the existing role extractor
omits most animation controls. Every catalogue material UUID is retained.

Counts are overlapping capabilities of time-reading templates, not proof that
every instance enables its switch or has nonzero speed. A time load is not a
complete backwards slice to a visible output. Four missing template files and
73 time-reading templates without a named class remain explicitly unknown.
Instance overrides and shader hashes are retained for further audit.

| Class | Full templates / instances | Compact rows / instances / templates | Camp instances | S6 instances | S8 instances | Inn instances |
|---|---:|---:|---:|---:|---:|---:|
| UV scrolling / panners | 459 / 5,532 | 5 / 3 / 2 | 3 | 43 | 4 | 5 |
| Flowmap | 62 / 378 | 44 / 21 / 4 | 1 | 28 | 2 | 1 |
| Sprite / flipbook | 47 / 700 | 0 / 0 / 0 | 0 | 0 | 0 | 0 |
| Vertex wind / cloth | 88 / 729 | 0 / 0 / 0 | 25 | 45 | 27 | 21 |
| Emissive pulse | 68 / 958 | 5 / 3 / 2 | 2 | 26 | 2 | 3 |
| Displacement / WPO / bounce | 182 / 2,403 | 4 / 2 / 1 | 2 | 40 | 2 | 1 |
| Water (overlaps other classes) | 20 / 412 | 0 / 0 / 0 | 2 | 39 | 2 | 1 |

Full catalogue: 32,696 UUIDs; 630 time-reading templates / 7,172 instances.
Compact v4: 40,983 rows / 17,295 UUIDs; no unresolved UUID. It excludes many
animated mesh families by design; zero wind/water is not world coverage.
Level counts resolve LOD 0 VisualBank materials through the existing root-template
resolver for Scenery/Items and globals. They count distinct material UUIDs, not
draws, placements, hit weight or visible pixels. Scopes are explicit:

- Camp: WLD_Campfire_E, CMP_Shrubbery_A/B (1,155 objects, 5 unresolved visuals).
- S6: WLD_Underdark_C, WLD_UnderdarkSubs_C, PLT_UND_AdamantineForge/Anvil
  (20,719 objects, 263 unresolved); includes the surrounding Underdark.
- S8: BGH_WaterQueenHouse_B_ART (856 objects, 3 unresolved).
- Inn means S7 Blushing Mermaid and its cellar (1,551 objects, 3 unresolved).

Existing handling is family-specific. Native wind XFB carries animated positions:
Wind RG, AlphaTest 2S and Wind RGB use audited instanced pairs
(`rt_hit_material_wind_rgb`); IP grass remains experimental. This does not cover
every wind/cloth template. D-EM pulse uses the draw's game time, with a documented
average when unreadable. Water retains the game's animated forward shading and
captured geometry; it is not a generic hidden-hit flowmap evaluator.

## Native time and formula evidence

Scrolling FS `4b52d62ee893d3b6`, lava FS `8e6584bfaa46d6f3`, lava VS
`88d26a9941a51911` and pulse use set 0 binding 0, std140 member 0, byte 0, float
game time. This is not a push constant, frame number or wall clock. The descriptor
is the same one resolved by `common/emissive_policy.h::family_fixup`.

`common/animated_material.h` contains shared CPU/HLSL arithmetic and a CPU-only
audit of the unpublished native sprite instance/frame path:

- Base_VT_Scrolling: UV * tiling + speed * time. Material 1:1: tiling at 0,
  speed at 8, reflectance at 16. Native VT views are 1:3..6.
- ENV_Lava_AlphaTest_Flowmap_Bounce_VT: sample flow at UV * material vec2 at 8,
  then mix(UV, UV + speed * time + (flow.xy - 0.5) * strength, flow.a).
  Speed is 1:1 at 0 and strength at 32. This is not water's two-phase flow.
- Its WPO uses a smoothed triangle wave of time * speed plus world/object-height
  and COLOR.B phase, amplitude COLOR.G along native N, optionally noise-weighted.
- BillboardCamera_Flipbook_GradientMap sprite VS uses game time plus an
  instance-derived random phase/rate and truncating atlas remainder. The helper
  does not replace its random hash with a global frame counter. Extracted VS/FS
  evidence is under `captures/anim-materials/sprite*`.

720 independent CPU cases cover scroll, lava flow/bounce and sprite atlas math,
including negative time/UV/rates and alpha endpoints. These certify arithmetic
primitives, not complete native sampling or qualified RT recipes.

## Published candidate and missing work

`rt_hit_material_animated=0` in every preset; GUI: Animated surface materials.
Only Base_VT_Scrolling is published. It retains the verified pool geometry,
basis, native VT views/sampler and reflectance gates. Each frame re-observes the
draw's mapped time UBO and controls. Missing/non-finite time and stale-frame
snapshots decline. Different time/controls on duplicate draws conflict. Hidden
meshes without a current draw retain fallback. A GPU execution-time time/control
comparison remains required for qualification.

Layout 6 stores tiling.xy, speed.xy and time at row bytes 228..244. Specialization
constant 1102 selects evaluation (1021..1024 and 1100..1101 reserved). ABI and shader request deltas are zero. VT
ray-cone gradients scale separately by X/Y tiling. Existing rows retain their
previous decode. The animation branch now has an outer specialization guard: with 1102 = 0 it
neither reads animation words nor rejects a layout 6 row. Layout 6 publication is
also off on the host. `tools/spec_cascade_check.py --key animated --all --baseline
ab7d1df7eea4 --jobs 4` compares every hit request with literal OFF/ON controls and
key-off with the pre-WIP revision. It covers instancing off, instancing on, Wind
RGB on and Wind RGB + IP grass on; references preserve those existing constants.
The ON/OFF control must differ, so unused animation code cannot give a false pass.
Key-off passes all 45 requests / 180 baseline cases (zero differences), and the
OFF/ON control differs by 76,091 normalized lines. All 360 literal-reference
comparisons pass, across the same four instancing/family configurations. Raw unspecialized byte identity is not expected; the gate compares optimized,
frozen programs with normalized IDs. No request variants or ABI changes are added.

Lava is not published, even as colour/emission only. ENV lava needs current WPO
geometry/basis, native flow texture/sampler and its separate emissive mapping.
FLUID_Lava_Plane_Flowmap uses COLOR Z/W, multiple flow/normal layers, distance
blending and blackbody heat. ENV_Lava forward is a third interface. Native COLOR
recovery and constant D-EM lava heat cannot supply those missing inputs. Sprite
publication also needs its geometry and per-instance inputs. The CPU audit now
reproduces VS _209/_210/_218/_341: effective instance ID (including BaseInstance),
modulo 2047.1416015625, nested golden-ratio fract hash plus engine 1:0 _m0.x seed,
round-to-even 0.01 quantization, then phase = random * 128 and advance =
((0.899999976 + random * 0.200000048) * time) * material 1:1 _m6 (byte 28).
The CPU helper preserves the native multiply order and fused final multiply-add.
Atlas dimensions are 1:1 _m0.xy, time is 0:0 _m0 (byte 0). VS _360 uses fused
multiply-add and _370 truncating remainder. 192 additional cases compare these
steps against an independent scalar translation of the extracted template.
The same VS _187 adds wind/billboarding and atlas-texture vertex displacement;
a phase-correct atlas on the static pool mesh would still shade the wrong surface. Helpers must not
bypass these gates.

S6 pack `replay-20261102-035919-1` and camp/S8 GI packs capture RT pass inputs,
not original lava time, UBO, flow view/sampler or shader-pair/COLOR provenance.
The compact audit found zero unique S6 UV candidates / 1,015 meshes. No valid
captured-time lava/G-buffer comparison is available. See
`captures/anim-materials/replay-evidence.json` and the cov-paks compact report.

`tools/live_batches/anim_materials` contains unqueued S6/camp OFF/ON/OFF drafts,
separate cost windows, predictions and restore. The draft is blocked on missing
implementation and in-frame sampling/float-energy reference. Its fake-runner pass
is structural only. GPU ms and visible gain are unmeasured. Do not request live
authorization for this partial candidate as a lava fix.

## Publication boundary after the follow-up

| Class | Time evidence | Publication decision |
|---|---|---|
| Base_VT_Scrolling | FS 4b52d62e, 0:0 byte 0 | Current-frame native draw rows only; off by default. |
| ENV lava bounce VT | VS 88d26a99 + FS 8e6584bf, 0:0 byte 0 | Off: no audited depth/G-buffer geometry pair or current COLOR/noise/basis witness in packs. Flow/scroll/bounce math is tested; full native surface/emission is not published. |
| Billboard flipbook | Extracted sprite VS/FS, 0:0 byte 0 | Off: native hash/frame audited; no captured instance seed, native atlas binding, or displaced billboard geometry publication. |
| FLUID lava / ENV_Lava | Census verifies time loads in deferred/forward/emissive FS | Off: separate heat/flow/normal/emission interfaces are not the ENV bounce recipe. Existing constant D-EM heat remains an explicitly approximate independent path. |
| Missing or unclassified templates | Four missing files; 73 unclassified time users | Off: no generic clock or class formula is assumed. |

No missing native clock was found for the named candidate templates. The blocker
is the per-draw resources and current geometry, not an invented wall-clock time.
The present replay packs cannot supply them, so no lava/sprite publication or
quality pass is claimed. The unqueued S6/camp drafts and predictions remain blocked.
Both drafts now require the exposure-hold log as well as setting its key; the full
CTest suite caught the missing expectation even though the old local lint passed.
