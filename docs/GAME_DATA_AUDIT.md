# Game data audit: what each RT feature reads, and from where (2026-11-02)

Question (user): do we take everything we can from the game's own data, correctly, for every RT
feature? Trigger: the RT alpha test read VT foliage alpha from the game's streaming page cache
(a coarser page where the requested one is not resident), while masks extracted offline from the
paks are exact (camp: 100.00% bit-equal where resident, 98.51% triangle-weighted agreement, the
1.48% rest all on coarser pages; P4_OMM.md 11.2a).

Audit only (main 378d72c, no code change). Sources: COVERAGE.md, ASSET_DB.md, MATERIAL_FAMILIES.md,
LIGHT_CONVENTIONS.md, P4_OMM.md, PLAN_M5_WATER.md, TRACK_C_GI.md, TRACK_D_*.md, NATIVE_HAIR.md,
NATIVE_TILESET.md, docs/progress/*, and the live lines of `captures/queue/results.md` (copy,
runs 2026-11-02 01:34-14:08). Source kinds below: **R** runtime capture (draws, descriptors,
UBOs, XFB), **A** offline asset DB from the paks, **H** hand constant or approximation, **-** none.

## 1. Per feature

| Feature | Data it needs | Where it comes from today | Coverage (latest live) | Better data in the game we do not use | Gain / cost of switching |
|---|---|---|---|---|---|
| Geometry (static, skinned, wind, terrain) | post-VS positions, UV | R: XFB of the depth prepass, every draw class incl. GPU skinning, wind, terrain heightmap (TRACK_D_SCENE_BUILD 2) | TLAS surface under 94-99% of px; capture drops 0 | none better for what is drawn: XFB is the game's own output, exact. Off-screen and culled meshes: level placements + GR2 in the paks (M2, blocked) | exact already; off-screen world is H cost |
| LOD | the LOD the game draws | R: whatever the frame drew; cascade casters at the cascade's coarser LOD (rt_common.hlsli:192 offset rule); `rt_dyn_hold` keeps alternating LODs | - | GR2 LOD0 for cascade/off-screen copies (paks) | low gain (offset rule handles it) |
| Vegetation wind | displaced positions, same as raster | R: XFB rows; instanced hit rows check the wind VS arithmetic bit for bit (wind RGB 2S: 10.1M checked, 10.1M bit-exact, S8 09:46) | rows bit-exact | - | exact already |
| Alpha test, non-VT | mask texture, UV, cut, sampler | R: the draw's own descriptors (alpha table, tex_alpha) | exact by construction | - | exact already |
| Alpha test, VT (91% of alpha triangles at camp) | alpha mip chain of the VT texture | R: the game's page cache through its indirection (rt_common.hlsli:340-372); falls back to the finest resident page (counter 15) | camp: 1.48% of the alpha test differs (coarser pages); offline masks exist for 7 camp textures only | **yes: the GTS/GTP tile sets in VirtualTextures*.pak** (exact BC4 alpha, done for camp: `rt_vt_alpha_masks`, 181 records/frame matched, om3 13:38) | quality: no residency flicker, exact cut; also the only OMM bake source (11.2). Cost: generalize the extractor (S-M) |
| Hair / scalp / pubes | native coverage noise, density, root, thickness | R: the draw's material packet, native recipe replicated (NATIVE_HAIR) | key off in Ultra (`rt_native_hair=0`); records 0 in the dialogue runs | - (runtime packet is the exact source) | exact recipe; only the RT cone footprint differs from raster sample identity |
| Hit material: textures, UV, params | per-instance material row: VT layers, UBO fields, UV basis | R: the G-buffer draw's descriptors + engine UBO, per audited VS/FS recipe (common/native_vt_surface.h); A: shader -> template names | qualified **S8 63.5%** (wind RGB on, m4w 08:32), S6 41.7% (hrg1 09:39; base 48.9%, row 9.4%), camp 13.1% (rc2 01:34; row 70.8%: IP grass), S14 22.3% (row 48%), inn 30.1% with mask rows | (a) **A: per-variant bindings and CB offsets per role are in the asset DB** (ASSET_DB (b)) but recipes are still audited by hand per pair; (b) **instances without a record** (base: S8 25%, S6 49%) are meshes the camera never G-buffer-drew: their material is in the paks (visual -> LOD0 material -> template, MATERIAL_FAMILIES resolves 366,822 placements) but needs a mesh identity join (H) | (a) high, M; (b) high, H |
| Hit material: albedo pages | VT albedo/normal/physical at the hit | R: same page cache as the alpha test (native_hit_material.hlsli:31-66); tile set not resident = unknown (fail closed) | not counted | GTS/GTP in the paks | low: a coarser page is a blurrier albedo with the same mean; GI hits are low-frequency. Keep runtime |
| Terrain / TileBlend | blend masks, 4 brush layers, LUT | R: research readers only (tools/assets/terrain_material.py, TERRAIN_CAPTURE.md); not a hit recipe | S8 terrain 393k vertices declined; TileBlend pairs placed, never traced | brush textures are plain DDS in TerrainTextures (paks), and bound at runtime too | medium, M (recipe audit) |
| Tileset (ENV_Tileset_MultipleTextures) | 9 textures, **vertex COLOR** | R: depth coverage only (`rt_native_tileset`, off) | 2.5% combined share, 5.0% of frame draws, declined | vertex COLOR is a VS input: not in the XFB stream (stride 64: Position + UV) | medium; blocked by the COLOR stream (layer change) |
| Emission (D-EM) | emitter triangles, colour x intensity, glow maps, pulse | R: effects-pass draws + host-read UBOs (0 constants not host-readable); A: param roles; 7 families exact against decompiled shaders | exact families never scaled; approximate ones calibrated on screen (sqrt per window, [1/64, 64]) | **lava heat** (S6 FLUID_Lava_Plane_Flowmap, 54.5% of S6 luminance): **H constant `rt_em_lava_heat` 0.5** (common/emissive_policy.h:84, core/em_policy.cpp:52) instead of the FS's two flow-noise layers + vertex COLOR z,w + distance + rock mask; Tileset BGM taken at vertex colour 0 (upper bound); forward ENV_Lava at the noise's upper bound | medium-high near lava; needs the COLOR stream + the FS replicated (M) |
| Local lights (point/spot) | position, colour, radius, cone, shadow flag, source size | R: the light array b25 (exact; join 0.00 mm, colour/radius 100%); A: source radius + owner (65-72% of listed matched; SE bridge 100% in the foundry) | traced by D-LS | **off-screen lights**: hits outside the froxel volume get 0 local light (TRACK_C_GI 237: local lights 1-16% of the reference) while the asset DB holds all 19,572 placed lights (+ SE bridge for items) | medium, M-H (M4.5 world-light provider; story state caveat) |
| Box (projector) lights | box, ramps, cookie, direction, shadow flag | R: b29 records + bg3_boxes.hlsli (exact runtime recompute); A: shadow flag (3,828 of 7,157) | `ls_box=2` energy in band, +0.20-0.32 ms (awaiting the user) | box units and ramp mapping in the DB are unverified against b29 (LIGHT_CONVENTIONS 2) but the runtime path does not need them | none needed |
| Sun | direction, colour, size, clouds, cascades | R: lighting UBO (`bg3_sun_colour` x clouds x b13), cascade casters for cut roofs (mask 0x02); clouds kept by multiplying the mask | sun replace: S8 "kept by roofs" 96.1-97.2% of game-dark/RT-lit px, under roofs 95-97% of px (wf1/wf2) | **H: roof pad 1 m, up 3 m, rise 6 m** (TRACK_D_SUN 12.4: "not derivable from the asset DB": triggers only, no meshes) | see roofs |
| Roofs (cut away) | roof geometry | A: room/floor trigger volumes (599 of 600 rooms RenderMethod 1) as padded boxes; R: cascade casters for sun rays only | GI rays never see them (gi_trace_rt.hlsl:111-131 volume rule); the rule drops **85% of qualified sun-lit far hits at S8** (24.2M of 28.4M, small-defects) | (a) **R: the cascade caster geometry is already in the TLAS** (mask 0x02) and could replace the volume test for the GI far ray and reflection misses; (b) A: GR2 roof meshes from level placements (M2) | (a) high at S7/S8 interiors, S-M; (b) H |
| Sky / ray misses | sky radiance at misses | R: the game's global probe b22 / b34 (its own environment term; misses are never invented); skydome `f12c2004` not in the TLAS | water rays miss 77-89% (M5.2 77.7%, compose 76.1%); S1 sky 29.6% of screen | R: the game's own sky inputs exist at runtime (atmosphere constants, transmittance LUT, clouds: replay pack sets 2-3) | low-medium outdoors (moving clouds, sun disc), M; indoors misses must keep the probe (cut roofs) |
| Probes / ambient | SH irradiance, specular cubes, local probe records, DFG LUT | R: the lighting's own descriptors (b22, b32/b33 records, b38 DFG; bg3_ambient.hlsli), exact; A: 4,212 probes (L1 SH, BC6H cube paths) unused at runtime | M6 native / PT reference: S8 1.44, camp 0.84, inn 0.78 (not a data error: the PT lacks native ambient and emission) | off-screen hits get the global probe only (froxel lists); the DB's probe boxes + cubes could give them their local probe | low-medium, M |
| Water | FS N (3 flow-mapped normal maps), roughness, F0, a, T, X, Ks, DFG | R: the patched water FS writes its own factors per pixel (common/water_fs_patch.h, water_compose.hlsl:1-12): **exact N and DFG LUT** | composed 120/120; refraction 75% transmissive owned, 99.9% traced | the Schlick stand-in survives only in the M5.2 diagnostic `water_reflect.hlsl:212` (statistics, not the image) | none for the image; trivial to align the diagnostic |
| GI far field | radiance of qualified far hits | R + H: `rho x the receiver's ambient` for the hit's own ambient (gi_far.hlsli:10) | -2.0% S8, -2.4% camp, in Ultra | the hit's own native ambient (bg3_ambient at the hit, already in native_hit_shade.hlsli:102-120) | low-medium, S-M |
| Thin translucency (class 4) at hits | transmission inputs (FS out3) | R: front lobe only; "the transmission lobe is missing" (TRACK_D_REFLECTIONS 469; Iridescent pool rows too) | class 4 is 30.6% of S5 Grymforge screen, 11.9% S3 | the same G-buffer draw's out3 inputs (runtime) | medium in Grymforge/foliage, M |
| Decals | decal albedo/normal at hits | R: primary pixels exact (they edit the G-buffer); hits: none | decal pass 0-9.3% of screen | runtime decal UBO; placements in the paks (not decoded) | low (hits only), M |
| Forward VFX / glass | emission, refraction | - | VFX up to 5.1% (S8) | runtime forward draws | low, M-H |

## 2. Approximations that exact game data could replace

| Approximation | Where | Exact source | Gain |
|---|---|---|---|
| Lava heat constant 0.5 + on-screen calibration | common/emissive_policy.h:84, core/em_policy.cpp:52 | the FS's flow noise (runtime textures) + vertex COLOR z,w (needs the XFB COLOR stream) + distance | S6 lava hue/intensity per pixel instead of one calibrated scale |
| Roof volumes padded 1 m / 3 m, rise 6 m | TRACK_D_SUN 12.4, gi_trace_rt.hlsl:118 | cascade caster geometry (runtime, already in the TLAS) or GR2 roof meshes | unblocks up to 85% of S8's sun-lit far hits; fewer conservative "kept by roofs" |
| VT alpha: coarser resident page | rt_common.hlsli:340-372 | GTS/GTP masks (offline, exact) | 1.48% of camp alpha test; residency-dependent flicker; OMM source |
| `rho x receiver ambient` at far hits | gi_far.hlsli:10 | native ambient at the hit | correct colour of the far bounce |
| Tileset BGM emission at vertex colour 0 | TRACK_D_EMISSIVE 13 table | vertex COLOR | exact instead of upper bound |
| Front lobe only for class 4 hits | TRACK_D_REFLECTIONS 469 | FS out3 translucency inputs | back-lit foliage and Iridescent rock in bounce |
| Schlick in water_reflect (diagnostic) | water_reflect.hlsl:212 | b25 DFG LUT (the compose already uses it) | diagnostics only |

Already exact (no change): water compose N/DFG, ambient/probes/DFG at screen pixels and hits, point/spot and
box light records, sun colour and clouds, wind and skinning positions, non-VT alpha, emissive exact families.

## 3. Fragile runtime reads

- **VT page cache** (alpha test and hit-material layers): residency-dependent; the content-epoch protocol already
  refuses to bake OMM from it (P4_OMM 11.2). Alpha: replace (exact masks). Albedo: keep (mean preserved), but the
  "tile set not resident" fail-closed share is not counted for hit materials.
- **Mask extractor inputs**: `tools/assets/vt_alpha_masks.py` lists records from a replay pack (camp 013545-2), so
  only textures seen in one capture get masks (7), and its LSLib path is hard-coded to a codex worktree
  (`LSLIB = ...\.codex\worktrees\m2-foundry-mesh\...`, line 33). Both must go before release.
- **Descriptor capture** for hit materials is robust: capture failures by bit 0 in the long camp session
  (pool line 09:23:40); source-epoch rejections only drop stats lines.
- **Asset-DB joins**: box-light match rates (16/25, 16/58) are understated by the denominator (fixed 11-01); the
  runtime path does not depend on them. Save hint does not work on Patch 8 saves (zstd); level detection falls
  back to lights (works).

## 4. Licensing: what must be generated on the user's machine

- **Asset DB** (lights, materials, triggers, probes, effects): derived from Larian's files; generate at install
  (ASSET_DB "ship the tool, not the data": ~40 s, CPython). Not redistributed today (`.gitignore`).
- **VT alpha masks**: bit copies of Larian texture data: generate locally only. Needs LSLib (Norbyte, MIT:
  redistributable with its licence) on .NET 8 + pwsh 7, or a C++ port of the GTS/GTP reader into the launcher
  (layer/assetdb.cpp already has LSPK + LZ4). Same for any future albedo or terrain texture extraction.
- **Offline geometry** (GR2 roofs, M2 world): Larian meshes: local generation only, via LSLib's GR2 reader.
- **Recipes, hashes, UBO offsets, re-implemented formulas**: our code from observation; ship. Never ship
  `captures/shaders/*.spv` dumps or decompiled GLSL.
- Runtime reads (DFG LUT, probes, blackbody LUT, sky LUTs, b25/b29) involve no redistribution.

## 5. Ranked actions (score = screen impact 1-5 x confidence 0-1 / cost 1-5)

| # | Action | Impact | Conf. | Cost | Score |
|---|---|---|---|---|---|
| 1 | Generalize offline VT alpha masks to every VT alpha-test material (enumerate from the DB, not a pack), then promote after `vt_alpha` | 3 | 0.9 | 2 | 1.35 |
| 2 | Generate hit-material recipe rows from the DB's bindings/CB offsets + the GLSL classifier (VARIANT "fields only" rows first) | 4 | 0.6 | 3 | 0.80 |
| 3 | GI far ray and reflection misses: test the cascade caster geometry (mask 0x02) instead of the padded roof volumes | 3 | 0.5 | 2 | 0.75 |
| 4 | Vertex COLOR in the XFB stream: Tileset rows, Tileset BGM emission, exact lava heat (+ S6 lava row) | 3 | 0.6 | 3 | 0.60 |
| 5 | Transmission lobe for class 4 hits from the draw's out3 inputs | 2 | 0.6 | 2 | 0.60 |
| 6 | Native ambient at qualified far hits instead of `rho x receiver ambient` | 2 | 0.5 | 2 | 0.50 |
| 7 | Off-screen local lights at hits from DB + SE bridge (M4.5) | 3 | 0.5 | 4 | 0.38 |
| 8 | Game sky model at outdoor misses (atmosphere LUTs, clouds) | 2 | 0.4 | 3 | 0.27 |
| 9 | Local probes for off-screen hits from the DB's probe boxes and cubes | 2 | 0.4 | 3 | 0.27 |
| 10 | Materials for never-drawn instances (DB placement -> material via a mesh identity join) | 4 | 0.3 | 5 | 0.24 |
| 11 | Decals at hits | 1 | 0.5 | 3 | 0.17 |
| 12 | DFG LUT in the water_reflect diagnostic | 0 | 1 | 1 | ~0 (do with any water change) |

### Top 5, with pointers

1. **VT alpha masks for every VT alpha-test material.** `tools/assets/vt_alpha_masks.py` (`vt_records` reads a
   pack; switch to the DB's VT tile-set index + texture name per material, `tools/assets/extract/materials.py`;
   find LSLib instead of the hard-coded path), `tools/assets/vt_alpha.ps1`, runtime `layer/alphatest.cpp:240`,
   `common/vt_alpha_masks.h`, key `rt_vt_alpha_masks` (common/settings.h:339-344). Gate: `tools/live_batches/vt_alpha`
   (counter 15 "coarser" -> 0 on masked records, energy within the OFF spread). Unblocks OMM (P4_OMM 11.2c) and
   with it the IP grass rows' +1.9 ms cost (hm-source-trim).
2. **Recipe rows from the DB.** OFFLINE_OPPORTUNITIES B. `tools/material_families` (classifier), `common/native_vt_surface.h`,
   `common/native_vt_fields.h`, `tools/hit_material_blockers.py`. First the "fields only" variants (MATERIAL_FAMILIES
   "Cheap": 9cc7094f/4464f442 0.8%, 6561beef/ce81ffaa 0.5%, 1e9145e4/e4ade329 0.4%), then 735d5a19/83831811 (3.4%).
   Observable: `hit material` qualified up, `row` down (S14 48%, S8 29.8%).
3. **Cut roofs from the cascade casters for GI rays.** `shaders/gi/gi_trace_rt.hlsl:111-131` (`gi_far_roof_clear`,
   volume loop) and `:409-412`; mask rule `shaders/rt/rt_common.hlsli:156-170` (`RT_VIS_MASK`); counter 10 of
   kGiFarStatFirst (372+10) already counts the rejected hits. Variant define, off; offline first on the S8 pack
   142819-1 (casters in the pack's TLAS array but with 0 triangles: needs a new pack, TRACK_C_GI "full pack").
4. **Vertex COLOR capture.** `layer/geocapture.cpp` + `layer/spirv_xfb.h` (XFB decorations, stride 64), then
   `common/emissive_policy.h:84` (lava heat), `core/em_policy.cpp:52`, the Tileset recipe (MATERIAL_FAMILIES row 8),
   S6 lava FLUID_Lava_Plane_Flowmap bd5dbda2/0b8dfd73 (hm-camp-grass.md). Layer change: restart, ABI only if the core
   needs the stream.
5. **Class 4 transmission at hits.** `shaders/rt/native_hit_shade.hlsli`, record fields in `common/native_vt_fields.h`
   (nv_iridescent_fields), the wind RGB 2S out3 inputs (TRACK_D_REFLECTIONS 469). Observable: S5 / S3 bounce under
   back-lit foliage; energy rule as M4.

Not recommended now: offline world geometry (M2, blocked), albedo pages from the paks (no visible gain), box-light
DB units (runtime path is exact).
