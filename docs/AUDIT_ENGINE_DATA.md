# Engine-data audit: what more can we get out of BG3? (2026-10-26)

Scope: the data that `AUDIT_SOTA.md` section 3 ("What is infeasible because we lack
engine access") and `AUDIT_RT.md` treated as missing. Each item is re-examined against
three sources the earlier audits did not use:
1. **The Vulkan frame, read harder.** Shaders of `captures/20261026-094651` (foundry, S1) and
   `captures/20261026-115620` (Water Queen's House, S8), plus `20261026-122703`; their
   `frame.json` per-draw bindings.
2. **The game's own asset files.** The `.pak` archives were parsed read-only with a small
   LSPK v18 + LSF v6/v7 reader written for this audit (same format as `scripts/save_index.py`).
3. **`bg3.exe` strings and RTTI**, plus the public BG3 Script Extender (bg3se) and LSLib
   sources.

No game run was made. No project code, save or game file was modified.

**Bottom line.** Most of the "infeasible" list is feasible. Two of the earlier "facts"
were wrong (see the corrections below), and both errors hid data we can use: the G-buffer's RT4 is the
virtual-texture feedback target, and the compute pass before the depth prepass is skinning,
not culling. The remaining hard limits are narrow, and they are listed in section 5.

## 0. Corrections to earlier docs

| Doc claim | What the evidence shows | Evidence |
|---|---|---|
| FRAME_MAP step 1: "GPU culling/skinning compute (`cs:718d8329`)" | **Pure linear-blend skinning.** It reads a bind-pose stream (b1, 20 B/vertex: float3 position + int16x4 tangent-frame quaternion), 4 bone indices and 4 weights (b2, 8 B/vertex) and a bone palette (b3, row-major `mat3x4`, base `_4._m2`). It writes the skinned position and the re-encoded quaternion to b4 at vertex `_4._m1 + i`. Nothing is culled. | `718d8329:52-57` (palette and weights), `:63` (bind pose), `:194` (output) |
| There is a "GPU culling pass" and per-instance cull buffers to exploit | **There is no GPU culling.** All 835 G-buffer draws are direct `vkCmdDrawIndexed` (TRACK_D 1). Culling is on the CPU: `bg3.exe` contains Intel's Masked Occlusion Culling (RTTI `?AVMaskedOcclusionCulling@@`, `MaskedOcclusionCullingSSE41`), plus ECS cull-distance components (`ls::VisualStreamCullDistanceChangeRequestSystem`). | `bg3.exe` strings; [Intel MOC](https://github.com/GameTechDev/MaskedOcclusionCulling) |
| FRAME_MAP G-buffer RT4: "per-object constant colour: object ID / material ID" | **It is Granite's virtual-texture feedback.** It packs the page x/y, the mip and the tile-set index per pixel. `cs:47d01608` then subsamples it 1:16 per axis, with a rotating offset, into the 160x67 RGBA8 image `i11369` for CPU readback. So it carries no object ID. | `14678345:179` (the RT4 write: page bits + `_m0[0][3].w` tile-set index); `47d01608:19-20`; `report.md` unit 165 |
| AUDIT_SOTA: VT gives base colour only; "normal maps and roughness at hits are infeasible" | A VT G-buffer shader reads **three layers** through the same page-table texel. `texelFetch(b3)` `.y`, `.x` and `.z` (one per layer) feed `textureGrad` on three `texture2DArray` caches (b4, b5, b6): normal map, base colour and the physical layer (roughness/reflectance). | `14678345:88, 112, 129, 146, 165, 176-177` |

## 1. Summary table

Feasibility is **YES** (data is reachable and complete for the item), **PARTIAL**
(reachable for a known subset) or **NO**.

| # | Data | Best route(s) | Feasibility | Unlocks | Effort | Risk |
|---|---|---|---|---|---|---|
| 1 | Hit materials (albedo, normal, roughness/metal) for secondary rays | (a) per-draw material table: VT draws, all 3 layers (74-82% of G-buffer draws); plain-texture draws via a per-FS dataflow table; (d) VT feedback injection for off-screen pages | **YES** for VT draws (coarser mips off-screen until injection); **PARTIAL** for plain-texture families | GI and reflection hit shading, D-RF tier (b), SHaRC updates at any hit | M (VT) / M-H (plain) | Low-medium |
| 2 | Emissive surfaces + emissive light list | (a) XFB of the game's **emissive draws** (separate passes into the FX target) + their UBO colour and mask; (c) offline: 4,949 of 15,157 materials carry glow/emissive params | **YES** (on-screen + retained); off-screen via offline | Emissive GI, emissive in reflections, emissive NEE / ReSTIR DI | L-M | Low |
| 3 | Complete light list incl. off-screen, shape, cookie, temperature | (c) offline level light placements (14,109) + light root templates, joined by position to the runtime array b25; (a) read the whole 4096-slot b25; (b) bg3se `ls::LightComponent` for dynamic lights | **YES** for placed lights; **PARTIAL** for gameplay-spawned lights (bg3se or last-seen); **IES: N/A** (the engine has none) | Off-screen light NEE, ReGIR/light BVH, correct shapes (box/projector), flicker | M | Medium (join) |
| 4 | Skinned mesh data in object space, motion | (a) skinning dispatch buffers (bind pose, bones, object-space output); MV pass gives **previous** skinned positions (set0 b14) and **previous model matrices** | **YES** | Persistent per-mesh BLAS refit, exact motion, OMM reuse | M | Low |
| 5 | Static vs dynamic, instance and object IDs | (a) membership in the game's MV pass = "dynamic this frame"; VS class (TRACK_D 1); per-draw UBO/instance stream; (c) level `MapKey` UUID via transform join | **YES** | BVH static split (AUDIT_SOTA rank 2), retention, stable keys | M | Low-medium |
| 6 | Off-screen and cut-away geometry | (c) offline level geometry: placements + VisualBank → GR2 (LSLib), **HLOD proxies** (19,037 GR2), `Fadeable`/`SeeThrough` flags for roofs; (a) cascade and light-shadow passes + XFB pre-clip; (b) relax CPU culling | **YES** offline (static world); **PARTIAL** runtime | Off-screen shadows, closed interiors for GI, reflections behind the camera | H | Medium-high |
| 7 | Camera, exposure, grade, tonemap, fog, time of day | (a) bind the game's own post resources (exposure SSBO, grade/tonemap UBO + LUTs, froxel fog volume); (c) AtmosphereBank gives semantic parameter names | **YES** (time of day: none exists; lighting is per-area presets) | RR pre-grade (AUDIT_SOTA #1), fog on RT terms | L-M | Low |
| 8 | Reflex and simulation timing | (b) hook `SDL_PollEvent` (imported from SDL2.dll) as SIMULATION_START + `ReflexSleep`; or bg3se's `ecl::EoCClient::Update` pattern | **YES** (proxy) | Real Reflex latency reduction under DLSS-G | L | Low-medium |
| 9 | Transparent / forward geometry (water, glass) | (a) XFB of the forward pass (99 draws in S8), UBO + texture records | **YES** | Water/glass in the TLAS: reflections of water surfaces, refraction (D-RFR) | L-M | Low |
| 10 | Light probes, decals, fog volumes (new) | (c) level `LightProbes/`, `Decals/`, `FogVolumes/` files; (a) b33/b45 at runtime | **YES** | Off-screen probe fallback, decals at hits, local fog | L-M | Low |
| 11 | Off-screen animated poses (new) | none: the engine does not skin characters it culls | **NO** (last-seen pose only) | — | — | — |

Effort: L ≤ 3 days, M ≈ 1-2 weeks, H ≥ 3 weeks (one developer, with the existing layer).

## 2. Per item

### 2.1 Material data at secondary hits

**Current state.**
- The TLAS is an XFB triangle soup. `rt_alpha=1` already captures UV (stride 24) and keeps
  per-draw records for **alpha-tested** draws only (`layer/alphatest.cpp:281-341`).
- `vt_alpha()` reads the albedo layer's `.w` (TRACK_D 7.1).
- Hit shading uses on-screen reprojection or the game's probe.

**Evidence.**
- **VT share.** G-buffer draws whose FS fetch a page table and sample `texture2DArray` caches:
  - foundry (`094651`): 617 of 835;
  - S8 (`115620`): 380 of 461;
  - `122703`: 881 of 1119.
  - The rest are plain-texture families: characters (`CHAR_Skin_Head_v3`, cloth) and some
    props.
- **The VT lookup** (`14678345:70-112`):
  - Per-draw UV transform: `_14._m0[0]` (set 1 b0).
  - Tile-set constants: `_13._m0[_14._m1]` (SSBO set 0 b12, two row-major mat4 per tile set).
  - Mip from the UV derivatives; then the page-table texel at that mip.
  - Entry bits: 0-3 = log2 scale to the **resident** page, 4-13 / 14-23 = cache tile y/x,
    24-30 = cache array layer.
  - The "scale to resident page" field is how Granite falls back to the nearest resident
    ancestor. An off-screen lookup therefore always returns valid, possibly blurrier, data
    and never garbage.
- **Layers.** In this shader b5 is base colour (→ RT1), b4 the normal map (→ RT0) and b6
  the physical map (→ RT2, with the reflectance constant `_15._m0` from set 1 b1). Layer
  order varies per family, so it must be read from each FS (as `alpha_table.py` already
  does for alpha).
- **Granite version.** `bg3.exe` embeds Granite 5.1 (path string
  `3rdParty\Granite\5.1\Granite\...`, `Graphine::Granite::*` RTTI).
- **Feedback path.** RT4 (`14678345:179`), then `cs:47d01608` (1:16 subsample with a
  per-frame rotating offset, `47d01608:19-20`), then `i11369` (160x67). That image has no GPU
  consumer, so it is read back by the CPU.
- **Offline material table.** `MaterialBank` resources (15,157 in Shared.pak) list:
  - `SourceFile`, the material template: top ones are `Base_VT` 1,445, `CHAR_BASE_VT` 508,
    `Base_AlphaTest_VT` 366;
  - `Texture2DParameters` (texture UUID by name: `basecolor`, `normalmap`, `MSK`, `glowmap`...);
  - `VirtualTextureParameters` (VT UUID);
  - scalar and vector params such as `Reflectance` (6,744).
- **Offline VT and mesh tables.** `VirtualTextureBank` maps the VT UUID to `GTexFileName`.
  `VisualBank` maps a visual to its GR2 `SourceFile` and per-object `MaterialID` and `LOD`.
  Formats follow LSLib ([lslib](https://github.com/Norbyte/lslib)) and the community wiki
  ([finding a VT](https://bg3.wiki/wiki/Modding:How_To_Find_A_Virtual_Texture)).

**Routes.**
- **(a1) Per-BLAS-primitive material table, VT families (recommended).**
  - Extend the alpha capture to every G-buffer/prepass draw:
    - stride 24 for `gl_Position` + UV;
    - stride 48 to add the normal and tangent varyings, which normal maps at hits need.
  - Per draw, record:
    - the VT tile-set index `_14._m1` and UV transform `_14._m0[0]`;
    - the per-family layer → binding map;
    - the page table (b3) and the three cache views (b4-b6);
    - the reflectance and other UBO constants.
  - The core then has `hit_material(draw, prim, bary, cone)` = base colour, tangent-space
    normal, roughness, F0. This is `vt_alpha()` generalised to three layers.
  - Draw lookup: a per-triangle u16 draw index written during the XFB conversion. That is
    1 B/tri more than binary-searching the draw ranges, and faster.
- **(a2) Plain-texture families.**
  - Generate `shaders/rt/material_fs.txt` the same way `alpha_table.py` builds
    `alpha_fs.txt`: decompile each G-buffer FS and slice the dataflow into outputs 1
    (albedo) and 2 (roughness/metal) back to `texture()` calls.
  - Families whose slice is "texture × constant" (most props) are decodable.
  - Gradient-mapped and skin shaders, whose albedo is a function of several textures and
    a gradient atlas, get a per-draw mean colour taken from the G-buffer where the draw is
    visible (the EN2 fallback).
- **(a3) Shader transplant (the complete answer, long term).**
  - Convert each G-buffer FS from SPIR-V into a callable/hit function:
    - inputs → function parameters filled from interpolated XFB varyings;
    - `dFdx`/`dFdy` → ray-cone gradients;
    - outputs → a return struct;
    - descriptor access → indexed into bindless arrays by the per-draw record.
  - This reproduces every material exactly, including gradient maps.
  - It is the same SPIR-V tooling as `spirv_xfb.h`, but much larger. Do it only after
    (a1) and (a2) show where the residual error is.
- **(d) Off-screen VT residency by feedback injection.**
  - After `cs:47d01608` and before the readback copy, write extra texels into `i11369`
    carrying the (page, mip, tile set) of secondary-ray hits. Same packing as RT4; up to
    10,720 requests per frame, and a few hundred per frame are enough.
  - Granite then streams those pages, so off-screen hits sharpen within a few frames.
  - Risk: we must not starve the game's own requests. Inject only into texels whose
    on-screen request repeats a page already requested.

**Feasibility: YES (VT, ~80% of draws), PARTIAL (plain texture).** Off-screen VT hits are
correct but blurry until (d).

**Unlocks:** D-RF tier (b), GI bounce at hidden and off-screen hits (11.7% + 2.2% of hits),
standard SHaRC updates, D-PT as a reference.

**Effort:** (a1) M, (a2) M, (d) M, (a3) H.

**Risk:**
- XFB memory (+24 B/tri for the varyings, about +75 MB at 3.1M triangles).
- Per-family layer mapping errors. Validate by tracing primary rays and comparing
  `hit_material` with RT1/RT2 per pixel (the same gate as the 99.09% primary-ray check).

### 2.2 Emissive surfaces and an emissive light list

**Current state.** `prevRadiance` misses the FX target (`i11680`, AUDIT_RT part 5).
AUDIT_SOTA rated the emissive triangles "unknown".

**Evidence: emissive materials are drawn as their own draws.**
- After the G-buffer, the game re-draws emissive meshes, depth-tested, into the B10G11R11
  FX target:
  - foundry, units 83-85: **29 draws**, FS `159a6211`, `20864dc6`, `b86501b6`, `48332370`,
    `f081b803`, `e0e3b441`, `87ea6f1c`, `56d5eaaf`;
  - S8, units 154-156: **43 draws**.
- Their shaders are tiny and uniform:
  - `20864dc6:17`: `out = UBO.rgb · tex(mask).x · UBO.w`;
  - `222986b8:22` (S8): `mix(UBO.rgb · tex.x, tex.rgb, flag) · UBO.m2`;
  - `48332370:13`: a constant `UBO.rgb`.
- So the per-draw emissive radiance is one UBO read plus one mask texture.

**Offline evidence.** 4,949 of the 15,157 Shared.pak materials carry emissive parameters:
`Glowmap`/`glowmap` 1,009, `GlowMultiplier` 510, `GlowColour` 388, `EmissiveMult` 367,
`Use_Emissive` 380, and others. Many are VFX (`VFX_*_Unlit_Emissive_*`).

**Route (a).**
- Add the FX-target pass family to the XFB capture: same VS families as the G-buffer
  (mostly class A/B), so the existing patch applies.
- Per draw, record the UBO colour and intensity plus the mask view.
- Emissive triangles go into BLAS geometry 2 (a mask bit "emissive").
- Build an emissive triangle list with power `area · mean(L)` (mean of the mask over the
  triangle, from a mip sample) for NEE/ReSTIR DI.
- Hits on these triangles add their radiance in GI and reflections.
- With EN3 retention they persist off-screen.

**Feasibility: YES** for on-screen and retained emissives.
- Off-screen emissives that were never seen need the offline route (material `glowmap`
  + `GlowColour` + GR2 geometry; item 2.6).
- Lava (S5/S6) and magic glyphs must be checked in their saves: some of their emission
  may come from VFX particles in the forward pass, which is a separate case.

**Unlocks:** emissive GI and colour bleed (lava, braziers, glyphs, screens), emissive in
reflections, emissive NEE. This turns D-DI (RTXDI) from "defer" into "worth it".

**Effort:** L-M. **Risk:** low.

### 2.3 Complete light list, off-screen lights, shapes, IES, colour temperature

**Current state.** EN1 snapshots the lighting's b23/b24/b25/b29/b40/b41 at the
tiled-lighting dispatch. The froxel lists only cover in-frustum points. b25 has 4096
slots of 80 B (TRACK_D 9.1); how many are filled, and by what CPU culling, is unknown.

**Offline evidence (parsed from `Gustav.pak`).**
- **Placements.** `Mods/<mod>/Levels/<level>/Lights/_merged.lsf` and
  `CombinedLights/_merged.lsf` exist for 232 + 96 levels: **12,991 + 1,118 placed lights**.
- **Fields.** Each record is `Type="light"` with `TemplateName`, a `Transform` (Position,
  RotationQuat, Scale), and per-instance overrides:
  - `Color`, `Intensity`, `Radius`, `Kelvin` + `UseTemperature`;
  - `Gain`, `ScatteringScale`;
  - `DirectionLightDimensions` and `DirectionLightAttenuationStart`/`End`/`Side`/`Function`;
  - `Angle` (spot inner/outer), `LightType` (0/1/2), `LightCookieTexture`/`Resource`;
  - `IsFlickering` + `Speed`/`Amount`, `IsMoving` + `MovementSpeed`/`Amount`;
  - `Shadow`, `VolumetricShadow`, `FlatFalloff`, `PreExpose`, `IsSunlight`, `LightChannelFlag`.
- **Templates.** Light root templates (e.g. `Public/GustavDev/RootTemplates/_merged.lsf`)
  carry the defaults for those fields, plus `CastShadow`.
- **The foundry (S1).** It lives in the world `CTY_Main_A`. Its lights are placed in the
  sub-level `CTY_LowerCity_A`, which has 924 lights: 104 box/projector (`DirectionLightDimensions`),
  36 with cookies, 5 flickering.
  - **65 of them lie within 60 m of the foundry camera** (absolute position −320.9, 19.9,
    −132.5, from TRACK_D 1), 16 within 25 m.
  - The foundry level itself (`BGH_SteelWatchFoundry_B`) has no `Lights/` folder.
- **IES.** No IES field exists in templates or placements, and `bg3.exe` has no IES-profile
  strings. **The engine has no IES profiles**; nothing is missing.
- **Shapes.** Point, spot, and the "directional" box light with dimensions, side
  attenuation and an optional cookie. The box light is the projector loop (b29) of
  `f469db78`.
- **Colour temperature.** `Kelvin` + `UseTemperature` (and `Blackbody` in the runtime
  component, below).

**bg3se evidence** (`BG3Extender/GameDefinitions/Render.h`,
[repo](https://github.com/Norbyte/bg3se)).
- `ls::LightComponent` holds `Color`, `Radius`, `Intensity`, `Kelvin`, `Blackbody`, `Gain`,
  `Flags`, `LightType`, `SpotLightInner`/`OuterAngle`, `Template`, `CullFlags`.
- `LightDesc` holds the cookie texture and the flicker and movement params.
- `bg3.exe` contains `ls::GetTypeName<struct ls::LightComponent>` and
  `TypeList<ls::CullComponent, ls::LightComponent>` strings, which is how bg3se binds
  components by name.

**Routes.**
- **(a) Read the whole b25 array**, not just the froxel-indexed entries.
  - Log the number of populated slots against the froxel union. If the CPU culls by a
    sphere or the level rather than the frustum, b25 is already the off-screen list within
    that radius.
  - One run with `light_debug` in a scene with lights behind the camera answers it.
- **(c) Offline light database, joined at runtime.**
  - At level load, load `Lights` + `CombinedLights` for the level and its sub-levels, and
    resolve templates.
  - Match each b25 entry (camera-relative `m0.xyz + _m6`) to the nearest offline light
    within 1-5 cm. That gives each runtime light its template, shape, cookie and flicker.
  - Calibrate the intensity mapping (Intensity × Kelvin colour × `b13` scale → b25 `m1`) from
    the matched pairs.
  - Unmatched offline lights within range = off-screen lights. Use their last-seen state
    (enabled, intensity) when they were seen, the file's state otherwise.
- **(b) bg3se as an optional provider.**
  - A Lua mod lists `Ext.Entity` lights (`LightComponent` + `TransformComponent`) every N
    ticks and writes them with `Ext.IO.SaveFile` (under the Script Extender user folder).
  - That covers gameplay-spawned lights: spells, carried torches, `ActiveCharacterLightID`
    character lights.
  - bg3se has no C API or IPC (`Exports.def` exports only `DWriteCreateFactory`), so a file
    is the interface. It is fine for 1-10 Hz.

**Feasibility:**
- **YES** for the placed lights (about 95% of the lights a level has).
- **PARTIAL** for spawned and dynamic lights: those come from b25 while on screen, or from
  bg3se.
- **IES: N/A.**

**Unlocks:** off-screen NEE for GI/reflection hits, a global light BVH or ReGIR grid
(D-DI), correct box-light shapes and cookies for D-LS, flicker-aware temporal handling.

**Effort:** (a) L; (c) M; (b) M (bg3se is the user's install, optional).

**Risk:** medium for the join.
- Lights toggled by Osiris story state or attached to moving items do not match positions.
- Templates with `ParentTemplateId` inheritance need resolving.
- Validate the join rate per scene (target ≥ 95% of b25 entries matched in S1/S7/S12).

### 2.4 Skinned mesh data in object space

**Current state.** AUDIT_SOTA lists "skinned mesh streaming in object space" as missing.
The plan is to refit from the skinning SSBO via XFB.

**Evidence.**
- **The skinning pass.** `cs:718d8329` (section 0) runs 70 dispatches in the foundry, one
  per skinned mesh section; e.g. `groups [28,1,1]` = 1,792 vertices.
- **The output is object space.** The class-B VS multiplies b13 by the per-draw model
  matrix `set1/b0._m0` (TRACK_D 1).
- **Previous frame.** The MV pass reads set 0 **b13 (current)** and **b14 (previous)**
  skinned positions at offsets `_10._m2` / `_10._m3`, and applies the **current and
  previous model matrices** `_10._m0` / `_10._m1` (`4ec2925d:68, 74, 81, 89`).
- **Blend shapes.** `bg3.exe` also ships `BlendShapeCompute.bshd` and
  `BlendShapeComputeNoMeshes.bshd` next to `SkinningCompute.bshd`: morph targets run as
  compute before skinning (not seen in the foundry frame; expected in dialogue close-ups).

**Route (a).**
- Hook `vkCmdDispatch` for `cs:718d8329` and read its set 1 b0 (count, output offset,
  bone base) from mapped memory, as `read_bound` does.
- Each dispatch gives (output range in b13, vertex count).
- With the draw that consumes that range (class-B `_m2`), we have a persistent
  **object-space** vertex range per mesh, plus its index buffer.
- That needs a `vkCmdBindIndexBuffer` hook, which is not intercepted today (the layer's
  hook list has no bind-index/vertex-buffer entries).
- Then: build each skinned BLAS once and refit it from b13 every frame
  (`ALLOW_UPDATE | PREFER_FAST_BUILD`). The TLAS instance transform is `_m0`, its previous
  transform `_m1`.
- Exact per-vertex motion comes from b14 vs b13.

**Feasibility: YES.**

**Unlocks:**
- AUDIT_SOTA rank 2 class B without XFB;
- exact motion for RT hits (temporal reuse of reflections and GI);
- OMM and BLAS compaction reuse across frames;
- removal of skinned meshes from the rebuild path.

**Effort:** M. **Risk:** low. The format is fixed: 20 B/vertex, float3 position at +0.

### 2.5 Static vs dynamic, instance and object IDs

**Evidence.**
- **The game's dynamic list.** The MV pass draws, after the full-screen camera pass, only
  objects that moved or deformed this frame. That is 134 draws in the foundry against 835
  in the G-buffer (`frame.json`; FRAME_MAP 6b).
- **VS class** (TRACK_D 1): A (instanced, matrix rows in a per-instance vertex stream),
  B (UBO matrix, optionally skinned), C, D (wind/cloth), E (terrain).
- **No object ID in the G-buffer.** RT4 is VT feedback (section 0).
- **Offline static flags.** Every placed object has a stable `MapKey` UUID and a `Transform`.
  - The foundry (`BGH_SteelWatchFoundry_B`) places 2,315 scenery objects, 627 tile
    constructions, 144 items, 86 decals, 1 terrain and 1 light probe.
  - The scenery flags `Fadeable` (837), `SeeThrough` (383) and `FadeIn` (619) mark the
    camera cut-away set.

**Routes.**
- **(a) Classification.**
  - Static = class A/E draws not in the MV pass for N frames.
  - Dynamic = in the MV pass, class B skinned, or class D.
- **(a) Instance keys.**
  - Class A: hash of the per-instance stream (needs a `vkCmdBindVertexBuffers` hook to
    know which buffer and offset), plus VB address + index count.
  - Class B: the UBO's `_m0` and the b13 range.
- **(c) Object ID.**
  - Match the class-A instance translation (camera-relative; `+ _m6` = absolute) and
    rotation to the level `Transform`.
  - Then `MapKey` is a stable object ID across frames and runs, with the template, visual
    and material chain behind it. It is also the join key for item 2.6.

**Feasibility: YES.**

**Unlocks:**
- the BVH static split (AUDIT_SOTA rank 2);
- EN3 retention with correct eviction;
- per-object material and emissive lookup from offline data.

**Effort:** M. **Risk:** low-medium. Instance streams may be in device-local memory: then
hash them on the GPU, or key on the draw tuple only.

### 2.6 Off-screen and cut-away geometry

**Current state.** The TLAS holds what the camera's G-buffer or prepass drew. Cascade
capture is planned (EN3). Roofs cut away indoors are missing.

**Runtime evidence.**
- **More geometry is already drawn in-frame:**
  - 4 cascades with 32 / 116 / 309 / 436 draws (foundry units 92-95);
  - in S8, 48 / 118 / 273 / 296.
- **XFB captures primitives before clipping.** In the Vulkan pipeline, transform feedback
  runs in vertex post-processing before primitive clipping
  ([Vulkan spec, vertex post-processing](https://registry.khronos.org/vulkan/specs/1.3-extensions/html/vkspec.html#vertexpostproc-transform-feedback)).
  Anything the CPU submits is captured whole, even if it is off-screen.
- **CPU culling.** It is Masked Occlusion Culling plus frustum and cull-distance
  components (section 0). bg3se maps `rf::Camera` with "10 culling planes" (`Camera.h`).

**Offline evidence.**
- **Placements.** Every placed object in a level (scenery, tile constructions, items,
  terrain) with its `Transform` and `TemplateName`.
- **The chain to geometry and materials.** Template → `VisualTemplate` → `VisualResource`
  → GR2 `SourceFile` and per-object `MaterialID`/LOD.
  - 131,204 GR2 files in Models.pak.
  - GR2 v7, sections compressed with Granny type 4 (BitKnit) in the sample read. LSLib
    decodes these natively (`LSLib/Granny/GR2/Reader.cs`).
- **HLOD proxies.** 19,037 HLOD GR2 files in Gustav.pak (716 for `CTY_LowerCity_A` alone)
  with their own VirtualTextureBank: merged, low-poly, baked-material proxies of whole
  level chunks. Each level also ships an `HLOD.lsf` (1 MB for the foundry).
- **Physics.** Per-tile `*_Physics/*.bin` and a PhysicsBank (16,755 resources) give
  collision-only proxies.

**Routes.**
- **(c) Offline static world (recommended for closure).**
  - Run LSLib offline once per level, on the user's own install (Divine CLI), to export
    GR2 to glTF.
  - Write a small baker that turns placements × meshes into a per-level cache: positions,
    indices, per-triangle material IDs, and an emissive table.
  - At runtime, build static BLASes at level load (`FAST_TRACE | ALLOW_COMPACTION`) in
    absolute world, with instance mask bit "offline".
  - Rays use offline geometry wherever the runtime soup has no instance for the same
    `MapKey`: off-screen, culled, or cut away.
  - **Roofs.** Objects flagged `Fadeable`/`SeeThrough` are exactly the cut-away set: keep
    them in the TLAS for shadow and GI rays, and mask them out for primary and reflection
    rays where the game faded them.
- **(c') HLOD-only variant (cheaper).** Use the HLOD GR2s as the far-field and off-screen
  proxy (geometry + baked VT colour) and keep runtime XFB for near geometry, like Lumen's
  far field
  ([Lumen technical details](https://dev.epicgames.com/documentation/en-us/unreal-engine/lumen-technical-details-in-unreal-engine)).
- **(a) Runtime only.** Cascade capture + retention (planned EN3). Add the **local-light
  shadow passes** to the capture too: they draw casters around each shadowed light,
  including behind the camera.
- **(b) Relax CPU culling.**
  - Widen the culling planes (bg3se `rf::Camera` layout), or no-op the MOC test via its
    vtable (RTTI-located).
  - Extra draws are clipped on screen but captured by XFB.
  - Fragile: needs offsets from bg3se per build. The build is frozen, which helps. Extra
    draws also cost vertex time.

**Feasibility: YES** offline for static geometry; **PARTIAL** at runtime.

**Unlocks:** off-screen shadow casters, closed interiors (GI misses stop leaking sky),
reflections behind the camera, and D-RF tier (c) shrinking toward zero.

**Effort:**
- (c) H: GR2 → cache baker, material join, VT for off-screen materials (see 2.1 (d), or
  offline `.gtp` decode with LSLib's VirtualTextures reader);
- (c') M; (a) M; (b) M-H.

**Risk:**
- Medium-high for (c): LOD choice, items moved by gameplay (use runtime for
  items/characters, offline for scenery/tiles only), sub-level composition.
- Legal: section 4.

### 2.7 Camera, exposure, tonemap and grade, fog, time of day

**Evidence.**
- **Camera.** Fully known (TRACK_D 1, FRAME_MAP).
- **Exposure.** `2cdd9f08` reads two float SSBOs (set 1 b0/b1, the GPU-computed exposure)
  plus a texture. It is GPU-resident, so bind the same buffers to our pass and no readback
  is needed.
- **Grade.** `0592680b`: UBO of 4 floats + 2D texture + **3D LUT** (b2).
- **Tonemap.** `d25b8086`: 2 floats + 2 ints + 2D + 3D LUT.
- **God rays.** `a7466e4e`: UBO set 1 b0.
- **Offline semantics.** `AtmosphereBank` (448 in Shared.pak) `PostProcessing` holds:
  - `ExposureSettings`: `ExposureMin`/`Max`, `Min`/`MaxPercentile`, `Compensation`,
    `MeteringMode`;
  - `NewColorCorrection`: `Contrast`, `Saturation`, `Hue`, `Shadows`, `Highlights`,
    `LutApplyMode`, `LutBlendFactors`, per-range `ExposureShift`/`Saturation`;
  - `GradingLut`, `WhiteBalanceTemperature`/`Tint`, `ToneMapHighlightsFixAmount`;
  - `Bloom` `Threshold`/`Amount`, the `GodRays*` parameters, DOF aperture and distance,
    vignette.
- **`LightingBank`.** Sun `SunColor`/`SunIntensity`/`Kelvin`/`Yaw`/`Pitch` and
  **`LightSize` 0.53°**. Half of that is 0.265° = 0.00462 rad, which cross-checks the
  0.00459 rad half-angle in AUDIT_RT part 4. Also two fog layers (density, height,
  albedo, noise), sky, moon, SSAO and volumetric clouds.
- **No time-of-day system.** No `TimeOfDay` string exists in `bg3.exe`. Lighting is a set
  of presets per area, blended by triggers (bg3se `CharacterLightViewData`
  `TargetAtmosphere`/`PreviousAtmosphere`/`SwapTime`). Night camp is a different preset.

**Route (a).**
- At the grade dispatch, take the bound UBO and LUT views with `query_binding`, and run
  the identical grade on the RR output. This is AUDIT_SOTA #1, now with no unknowns.
- Exposure stays the game's (pre-RR), because RR is fed after `2cdd9f08`. To move RR before
  exposure as well, read the exposure SSBO and pass it as `DLSS.Pre.Exposure`/ExposureTexture.
- Fog for RT terms: the integrated froxel volume (`8a4e0720` output) can be bound to
  reflection rays to attenuate the reflected radiance by the camera-to-surface segment,
  exactly as compose does.
- Offline atmosphere files are needed only to name the UBO floats (useful for debugging
  and per-area A/B), not for the replication itself.

**Feasibility: YES.** Unlocks RR pre-grade, and pre-exposure if wanted. Effort L-M. Risk low.

### 2.8 Reflex and simulation timing

**Current state.** PCL markers SIMULATION_START/END and RENDERSUBMIT_START are issued
back to back at present (`layer/streamline.cpp:743-745`). The markers exist in Streamline
2.14.16 (`external/streamline-sdk-v2.14.16/include/sl_pcl.h:62-68`).

**Evidence.**
- `bg3.exe` imports `SDL_PollEvent` from SDL2.dll (string found next to
  `SDL_GetGlobalMouseState`; `SDL2.dll` ships in `bin`).
- `bg3.exe` has no Reflex/NvAPI low-latency strings, so there is no native integration.
- bg3se maps `ecl::EoCClient::Update` and the client `EntityWorld::Update`
  (`BinaryMappings.xml`).

**Routes.**
- **(b1) Hook `SDL_PollEvent`** (IAT or export, stable across builds).
  - The first call of a frame = input sample = SIMULATION_START. Call
    `slReflexSleep` just before it, which is where Reflex wants it.
  - SIMULATION_END = the first `vkBeginCommandBuffer` of the frame on the render
    thread (or the first `vkQueueSubmit`), RENDERSUBMIT_START at the same time.
  - Validate by logging the thread IDs of `SDL_PollEvent` vs the submit thread.
- **(b2)** bg3se's pattern for `ecl::EoCClient::Update`, for exact begin/end. This is more
  precise but needs pattern scanning.

**Feasibility: YES** (proxy markers that are correct in order). **Unlocks:** the real
Reflex latency reduction under DLSS-G, which today degrades to present-only markers.
**Effort:** L. **Risk:** low-medium; SDL may be polled more than once per frame (take the first
after present).

### 2.9 Transparent and forward geometry

**Evidence.** The forward pass in S8 (unit 183) has 99 draws with 16 FS, plus 4 more
(unit 184). In the foundry it is the pass right after the colour copy for refraction (AUDIT_RT's unit 124). Its VS families are the same kind
(per-draw UBO, instance streams), so the XFB patch applies unchanged.

**Route (a).**
- XFB the forward pass into a separate BLAS geometry (mask "transparent").
- Record each draw's material UBO, so water can be told from glass by FS family.
- Use: reflection rays see water surfaces; refraction rays (D-RFR); the RR transparency
  and CBT guides (AUDIT_RT part 8).

**Feasibility: YES.** Effort L-M. Risk low. Particles are best left out (AUDIT_SOTA).

### 2.10 New items found

- **Light probes.** `LightProbes/_merged.lsf` + per-probe `.dds` cubes and `_SH.dat`
  per level (5,792 files). This gives off-screen probe placement and SH for a better miss
  and probe fallback at off-screen hits. The runtime b33/b45 list stays the source for
  visible probes.
- **Decals.** `Decals/_merged.lsf` (86 in the foundry) with `Dimensions`/`Material`. Hit
  materials miss decals today. Offline decal boxes can be applied at hits (low priority).
- **Fog volumes.** `FogVolumes/_merged.lsf` (178 levels): local fog for RT and volumetric
  terms beyond the froxel grid.
- **Character lights.** Characters carry `LightID` / `ActiveCharacterLightID`: a light that
  follows the party. It is dynamic, so it comes from b25 at runtime or from bg3se.
- **PhysX scene.** Full collision geometry for the whole loaded level (bg3se exposes
  `Ext.Level.Raycast*`). This is a low-poly proxy usable for occlusion-only rays if
  offline GR2 baking is not done.

## 3. Route evaluation

| Route | Feasibility | Effort | Fragility | Legal / ToS | Verdict |
|---|---|---|---|---|---|
| **(a) Vulkan level**: more passes (emissive, forward, cascades, light shadows, MV pass, skinning, grade), descriptor/UBO reads, VT page table and feedback | High: everything above except off-screen-never-drawn and dynamic off-screen lights | L-M per item | Low. The game is frozen (no more patches), shader hashes are stable, and the existing tooling (`query_binding`, `read_bound`, XFB patch) covers it | None beyond what the project already does | **Do first** |
| **(b) CPU RE of `bg3.exe`**: direct struct reads via bg3se layouts, or bg3se as a provider | High for lights, transforms, camera, atmosphere and visuals. bg3se v31-v33 (Apr-Oct 2026) target Patch 8 ([releases](https://github.com/Norbyte/bg3se/releases)) | M (provider via Lua + file) to H (own pattern scanner) | Medium. Offsets are per build but the build is frozen. bg3se binds ECS components by type name, which is robust | Single-player, no anti-cheat found. Larian has official mod support (Patch 7, mod.io) and tolerates SE ([VGC](https://videogameschronicle.com/news/larian-says-baldurs-gate-3-will-receive-official-cross-platform-mod-support)). Check bg3se's licence before copying code; reusing layouts as facts is lower risk | **Optional provider** for dynamic lights; `SDL_PollEvent` hook for Reflex |
| **(c) Offline assets**: LSF levels, templates, banks, GR2, HLOD, VT | High for everything static. Proven in this audit for LSPK, LSF, lights, banks and atmospheres; GR2 needs LSLib (BitKnit) | M (lights) to H (geometry + materials) | Low at runtime (files are frozen); the medium part is the join | Larian's modding terms grant a licence to use game assets to make mods ([terms](https://baldursgate3.game/modding-terms)). Keep caches local, built from the user's install, never redistributed | **Do for lights now, geometry later** |
| **(d) Combinations** | The joins are concrete: **position** (lights: b25 ↔ `Lights/_merged.lsf`), **instance transform** (class A matrix ↔ `Transform` → `MapKey`), **mesh** (VisualBank `ObjectID` + vertex/index count ↔ draw) | M | Medium (join rate must be measured) | as (c) | **The design** |

## 4. Ranked plan

Ranked by unlocked quality per effort, with dependencies respected. Each step has a
validation gate in the spirit of QUALITY R5/R6.

| Rank | Step | Route | Unlocks | Effort | Gate |
|---|---|---|---|---|---|
| 1 | **Grade/tonemap replication from the game's bound UBO + LUT; RR on pre-grade colour** | a | RR pre-grade (AUDIT_SOTA #1) | L-M | Our grade output = the game's, max abs diff < 1 LSB on S1/S8/S14 |
| 2 | **Emissive pass capture**: XFB + per-draw emissive record; emissive bit in the BLAS; add to `prevRadiance` and hit shading | a | Emissive GI and reflections; enables D-DI with emissives | L-M | Σ of emissive triangles reprojected = FX target (minus sky) within 5% |
| 3 | **MV-pass + skinning capture** → static/dynamic split, skinned object-space BLAS, prev transforms; hook `vkCmdBindIndexBuffer`/`vkCmdBindVertexBuffers` | a | AUDIT_SOTA rank 2 (−2 to −3 ms in motion), exact motion | M | Primary-ray gate ≥ 99%; skinned refit error 0 |
| 4 | **Hit-material table v1**: all draws UV(+N,T) captured; VT 3-layer `hit_material()`; plain-texture families via a dataflow table; mean-colour fallback | a | GI and reflection hit shading off-screen, D-RF tier (b) | M | Primary-ray `hit_material` vs RT1/RT2: albedo ΔE < 2 on ≥ 90% of pixels |
| 5 | **Offline light DB + runtime join**; read the full b25; log populated slots | c + a | Off-screen lights, box/cookie shapes, flicker, global light BVH | M | ≥ 95% of b25 entries matched; calibrated intensity fit R² > 0.99 |
| 6 | **Reflex markers from `SDL_PollEvent`** + `slReflexSleep` | b | Real latency reduction with DLSS-G | L | PCL latency stats in the overlay; no pacing regressions |
| 7 | **Forward-pass XFB** (water, glass) | a | Water/glass in reflections; D-RFR; RR transparency guides | L-M | Water surface present in `rt_debug` views of S8 |
| 8 | **Cascade + local-light-shadow capture, retention (EN3)** | a | Off-screen casters; partial closure | M | Planned gates (TRACK_D 8.4) |
| 9 | **VT feedback injection** for secondary-ray hits | a/d | Sharp off-screen VT materials | M | Feedback texels we inject show up as resident pages within 5 frames; no regression in the game's own residency |
| 10 | **Offline static world**: LSLib export → cache baker (placements × GR2 × MaterialID), `Fadeable`/`SeeThrough` roofs, HLOD far field | c + d | Closed interiors, reflections behind the camera, full-scene GI | H | Offline-vs-XFB overlap: per-object position error < 1 mm on matched `MapKey`s |
| 11 | Optional: **bg3se provider** for dynamic lights/entities (Lua → file) | b | Spawned lights off-screen | M | Lights from bg3se ⊇ b25 on screen |
| 12 | Long term: **shader transplant** (game FS → hit functions) | a | Exact materials everywhere | H | Primary-ray material = G-buffer bit-exact |

Optional and fragile, after rank 10 only: relax the CPU culling (planes or the MOC vtable)
so the game submits off-screen geometry with its runtime materials.

## 5. What remains impossible

- **Poses of off-screen characters.** The engine skins only what it draws (70 skinning
  dispatches for on-screen meshes). A culled character has no current pose; only the
  last-seen pose (retained) or the bind pose (offline) exists. Shadows and reflections
  of animated characters behind the camera therefore cannot be exact.
- **Geometry that was never modelled.** Building parts never authored because of the
  isometric camera (missing back walls, open tops) are absent from the assets too.
  Offline data closes cut-away roofs (they exist), not these.
- **IES profiles and physical light units.** The engine has none. Light intensities are
  artistic (`Intensity` 5,000-75,000, `PreExpose`, a global scale `b13`). Physically based
  units cannot be recovered, only calibrated to match the game.
- **State of dynamic VFX off-screen.** Particle systems and timeline-driven VFX are
  simulated only when relevant. Their off-screen emission is unknowable, except as
  "last seen".
- **Gameplay-driven light state off-screen** (Osiris toggles, extinguished torches)
  **without bg3se.** With bg3se it becomes readable.

Everything else in AUDIT_SOTA section 3 moves from "infeasible" to an effort estimate
above. Update that section once rank 2 or 4 lands.

## 6. Reproducing this audit

The read-only reader scripts are outside the repo, in `C:\Users\quali\AppData\Local\Temp\bg3rtx_audit\`
(the scratchpad path is too long for Windows' 260-character native limit):
- `lsf.py`: LSPK v18 index and read, LSF v5-v7 parse.
- `paklist.py`: dumps every pak's file list to `out/*.txt`.
- `census.py` / `templates.py` / `lights.py`: object-type and light censuses.
- `banks3.py`: material, atmosphere, visual and VT banks.
- `passes.py`: per-pass summary of a capture's `frame.json`.
- `exegrep.py` / `exegrep2.py`: `bg3.exe` strings.

They need Python with `lz4` (and `zstandard` for zstd entries). Promote them into `scripts/` if
rank 5 or 10 is started; this audit did not edit project code.

## Sources

- bg3se (Norbyte): https://github.com/Norbyte/bg3se
  - `BG3Extender/GameDefinitions/Render.h` (`LightComponent`, `LightDesc`, `Visual`,
    `MeshBinding` pose), `Camera.h`, `Resources.h`;
  - `GameHooks/BinaryMappings.xml`, `Docs/API.md`, `Exports.def`;
  - releases: https://github.com/Norbyte/bg3se/releases
- LSLib (Norbyte): https://github.com/Norbyte/lslib
  - `LSLib/LS/PackageReader.cs`, `PackageFormat.cs`, `Resources/LSF/*`;
  - `LSLib/Granny/GR2/Reader.cs`, `LSLib/VirtualTextures/*`.
- BG3 wiki, virtual textures: https://bg3.wiki/wiki/Modding:How_To_Find_A_Virtual_Texture
- Larian modding terms: https://baldursgate3.game/modding-terms; fan content policy:
  https://larian.com/fan-content-policy
- Official mod support (Patch 7):
  https://videogameschronicle.com/news/larian-says-baldurs-gate-3-will-receive-official-cross-platform-mod-support
- Intel Masked Occlusion Culling: https://github.com/GameTechDev/MaskedOcclusionCulling
- Vulkan spec, transform feedback in vertex post-processing:
  https://registry.khronos.org/vulkan/specs/1.3-extensions/html/vkspec.html#vertexpostproc-transform-feedback
- Lumen surface cache and far field:
  https://dev.epicgames.com/documentation/en-us/unreal-engine/lumen-technical-details-in-unreal-engine
- Streamline PCL markers: `external/streamline-sdk-v2.14.16/include/sl_pcl.h`
