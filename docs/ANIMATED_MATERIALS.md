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

`common/animated_material.h` contains shared CPU/HLSL arithmetic:

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

180 independent CPU cases cover scroll, lava flow/bounce and sprite atlas math,
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
constant 1021 selects evaluation. ABI and shader request deltas are zero. VT
ray-cone gradients scale separately by X/Y tiling. Existing rows retain their
previous decode. Baseline hit-module byte identity FAILED: 0/4 identical after
adding specialization constant 1021. Three unaffected temporal requests remain
byte-identical. This candidate does not satisfy the key-off identity requirement;
default-path isolation or equivalent approved validation remains outstanding.

Lava is not published, even as colour/emission only. ENV lava needs current WPO
geometry/basis, native flow texture/sampler and its separate emissive mapping.
FLUID_Lava_Plane_Flowmap uses COLOR Z/W, multiple flow/normal layers, distance
blending and blackbody heat. ENV_Lava forward is a third interface. Native COLOR
recovery and constant D-EM lava heat cannot supply those missing inputs. Sprite
publication also needs its per-instance hash and geometry path. Helpers must not
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
