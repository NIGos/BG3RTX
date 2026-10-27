# Asset database (offline game data)

The asset database is the static data BG3 keeps in its `.pak` files, extracted once and
read by the layer at run time. It holds:

- every placed light;
- the shader → material table;
- rooms, roofs and lighting presets per level;
- light probes;
- effect lights.

It implements AUDIT_ENGINE_DATA §2.3 (route c), R2 A1, A4, A5 and A11.

Code:
- `tools/assets/` builds it: a Python package with tests.
- `layer/assetdb.cpp` loads it and runs the join check.
- `common/assetdb_api.h` is the core's view (`Bg3HostApi::assetdb`, ABI 6; table
  version 2 adds the runtime lights of the Script Extender bridge, docs/SE_BRIDGE.md).

D-LS uses it for each light's source radius and owner (TRACK_D 9.8), merged with the
Script Extender bridge when that runs. Hit materials and emissives come later.

## Build

```
python -m tools.assets build --out data/assetdb          # 40 s on 16 cores
python -m tools.assets build --out data/assetdb --json   # + readable dumps (45 MB)
python -m unittest discover -s tools/assets/tests -t .   # 28 tests (3 read the install)
bin\assetdb_test.exe                                     # the layer's loader on the built DB
```

- **Requirements.** CPython 3.14 alone is enough:
  - LZ4 is decoded in Python when the `lz4` package is missing;
  - zstd comes from the standard library's `compression.zstd`.
- **Game folder.** `--game` (or `BG3_DIR`) points at the install. The default is
  `D:\SteamLibrary\steamapps\common\Baldurs Gate 3`.
- **Read-only.** The game files are only read. The build refuses to write inside the game
  folder.
- **Parallel steps.** SPIR-V hashing and the `.lsfx` scan use one process per core
  (`--jobs`).
- **Smaller builds.** `--no-effects` skips the effect lights. `--no-names` drops names and
  probe paths: 6.9 MB instead of 8.0 MB.
- **Honour mode.** Its level additions are skipped unless `--honour` is given.

Other commands:

| Command | What it does |
|---|---|
| `info` | Section sizes of a built database |
| `shader <hash or prefix>` | A shader's template(s), class and binding map |
| `join --dump captures/shaders --frame captures/<ts>/frame.json` | Offline shader join against the runtime shader dump and a trace's draws |
| `ls <pattern>` | Lists package files |
| `dump <path>` | Prints an LSF or LSX resource as JSON |

### Package layout

| Module | Role |
|---|---|
| `lspk.py` | LSPK v18 reader. Multi-part packages. `PackageSet` gives one virtual file system: higher header priority wins, then the patch and hotfix packages. |
| `compress.py` | zlib, LZ4 block and frame (linked blocks too), zstd |
| `lsf.py` / `lsx.py` | LSF v2-v7 (both node formats) and LSX, decoded to one `Node` tree. UUIDs follow LSLib's byte order. |
| `spirv.py` | BSHD/SHD8 chunks, SPIR-V entry point, the layer's shader hash |
| `geom.py`, `colour.py` | Transforms, quaternions; Kelvin and sRGB |
| `extract/world.py` | Level composition (below) |
| `extract/lights.py` | Light placements resolved against root templates (`ParentTemplateId` chains) |
| `extract/materials.py` | Templates, per-variant bindings, roles, classes, shader hashes |
| `extract/triggers.py` | Room, portal, floor, lighting and atmosphere triggers |
| `extract/probes.py` | Probes plus SH; lighting and atmosphere presets |
| `extract/effects.py` | `.lsfx` light components |
| `dbformat.py`, `build.py`, `cli.py` | Container writer/reader, orchestration, command line |

## How levels are composed

The save's "Current Level" (e.g. `CTY_Main_A`) is a **world**:
- A world places sub-levels through `LevelTemplates/_merged.lsf`. Each placement names a
  LevelTemplate root template whose `SubLevelName` is the child, plus a Transform.
- Placements nest. `BGH_SteelWatchFoundry_B` sits in `CTY_LowerCity_A`, rotated −90° about
  Y at (−242.9, 6.6, −184.2). `CTY_LowerCity_A` sits at the origin of `CTY_Main_A`.
- Every object goes through the full chain of transforms.

A level's files can come from several mods (Gustav, GustavDev, GustavX...). They merge in
mod order, and a later object with the same MapKey replaces an earlier one.

Only levels that no other level places become database levels: 113 of them. A sub-level
used by several worlds (camp levels, for example) is copied into each. The `ALIA` table
maps every sub-level name to its worlds.

AUDIT_ENGINE_DATA's 14,109 lights counted each Gustav.pak file once, in local
coordinates. Here there are 19,572 light instances in world space: Shared and GustavX
levels are included, and shared sub-levels appear once per world.

## Format and why binary

The layer loads `%BG3RTX_ASSETDB%`, else this checkout's `data/assetdb/bg3rtx_assetdb.bin`,
else, in a git worktree without one (the folder is not versioned), the main checkout's: the
worktree's `.git` file names its git dir, whose `commondir` names the common `.git`, whose parent
is the main checkout (`layer/assetdb.cpp` `db_path`). The `[assetdb] loaded ... (from ...)` line
says which; `bin/assetdb_test.exe` uses the same lookup.

One little-endian container, `data/assetdb/bg3rtx_assetdb.bin`, format version 1, laid
out as follows:
- A 64-byte header, a table of contents, and sections of fixed-size records, each
  section 16-byte aligned.
- Strings are offsets into one pool.
- The layer validates every section's record size, bounds and cross-reference ranges, then
  reads the records in place with no parsing.
- The C++ structs `static_assert` the same sizes as `dbformat.py`, and
  `tests/test_extract.py` pins them.
- `manifest.json` next to it records:
  - the format and tool version;
  - the packages read (name, size, mtime, MD5 from the header);
  - counts, statistics and section sizes.

**Why binary, not JSON.**
- **Size.** The JSON dump of the same data is 45 MB; the binary is 8.0 MB (2.0 MB gzipped).
- **Load cost.** The binary loads in one read with no parser in the layer. The project has
  no JSON library on the C++ side, and adding one to the layer is not worth it for
  read-only data.
- **Debugging.** `--json` keeps a readable form for that.

| Section | Records | Bytes | Content |
|---|---|---|---|
| `LGHT` | 19,572 × 128 | 2.51 MB | Lights: position, rotation, colour, intensity, range, Kelvin, spot angles, box size, attenuation, flicker, movement, channel, flags, cookie, name |
| `STRS` | | 1.69 MB | Names, template paths, probe path prefixes, UUIDs |
| `MPRM` | 40,390 × 32 | 1.29 MB | Material parameter bindings, de-duplicated lists |
| `PCAP` | 5,806 × 80 | 0.46 MB | Probe captures: lighting preset, inline L1 SH, path prefix of the cubes |
| `MVAR` | 17,235 × 24 | 0.41 MB | (template, variant): CB bindings and a parameter range |
| `EFXL` | 5,876 × 64 | 0.38 MB | Effect lights |
| `SHDR` + `SREF` | 15,363 × 24 + 34,470 × 4 | 0.51 MB | Shader hash (sorted), its variants |
| `PRBE` | 4,212 × 64 | 0.27 MB | Probes |
| `TRIG` + `TPTS` + `TREF` | 1,939 × 96 + … | 0.28 MB | Triggers, polygon points, links |
| others | | 0.17 MB | Levels, instances, aliases, presets (671), templates (1,149), engine shaders (648) |

### (a) Lights

- **Fields.** Each placement (`Lights/` and `CombinedLights/`) is resolved field by field:
  the placement first, then the nearest template up the `ParentTemplateId` chain, then the
  engine default. `manifest.json` lists how often each default was used. No template
  reference is missing.
- **Types.** 12,041 point, 374 spot and 7,157 box lights. `LightType` 2 is the
  `DirectionLightDimensions` box, the projector loop of the tiled lighting.
- **`range`.**
  - Point and spot lights: their `Radius`.
  - Box lights: half the box diagonal plus `DirectionLightAttenuationEnd`. This is the
    bound used for spatial queries.
- **Flags.** enabled, use-temperature, shadow, volumetric shadow, flicker, moving,
  sunlight, pre-expose, flat falloff, cast-shadow, cookie, combined,
  not-starting-loaded, half-lit.
- **Not-starting-loaded.** Set when a sub-level loads later through the story.
- **Identity.** Every light keeps its level instance and its world.

### (b) Shader → material

Templates and variants:
- Each template `Public/<mod>/Assets/Materials/**/<T>.lsf` is compiled into 15 variants:
  `<T>_<ST|STI>_<DEF|FOR|EMI|DEP|DEPS|DEPST|VEL|BAKE>_Vulkan.bshd`.
- A bshd is a header plus one 50-byte `SHD8` chunk per stage. The chunk's size field gives
  the module's exact length.
- AUDIT_ENGINE_DATA R2 cut each blob at the next SPIR-V magic. That kept the next chunk's
  header in every vertex shader's hash, which is why it matched only fragment shaders. This
  build matches both.

Classes and roles:
- **Bindings.** The template gives each variant's `VkDescriptorSet`/`VkBindingIndex` for
  textures and VT slots, and the constant buffer offset of each scalar or vector.
- **Roles.** Parameters get a role from their name:
  - base colour, normal, physical and emissive maps;
  - base colour, emissive colour and intensity;
  - roughness, metalness, reflectance;
  - alpha cut-off, opacity;
  - VT table, albedo, normal and physical caches;
  - VT tile-set index and UV transform;
  - diffusion profile index, light channel, fade opacity.
- **What is kept.** Parameters without a role are dropped. Samplers are dropped too.
- **Primary class.** One of opaque, alpha_test, emissive, refractive, sss_skin, hair,
  foliage, transparent, unlit, cloth, anisotropic, water, decal.
- **Flags.** 24 flag bits sit next to the class.
- **`ShadingModel`.** It equals the G-buffer material class in FRAME_MAP (0 standard,
  1 unlit, 2 anisotropic, 3 skin, 4 translucent/foliage, 5 hair, 6 cloth).

Shared hashes:
- 4,227 hashes are shared by several templates. The canonical one is the least specific
  (fewest flags, shortest name), and `aliases` counts the others. For example,
  `14678345` = `Base/Base_VT:StaticInstancedDeferred`, with `Base_AlphaTest_VT` sharing its
  deferred FS because the cut-out happens in the depth pass.
- 648 engine shaders (EngineShaders.pak) get a name. For example, `f469db78` =
  `ClusteredLighting/ClusteredLighting_0`.

**The layer's hash is not textbook FNV-1a.** `layer/util.cpp` uses the offset basis
`1469598103934665603`, the standard one with its last digit missing. Every shader name
in the captures and docs uses it, so `spirv.py` copies it. A unit test pins it, and the
GameFiles test checks the result against `f469db7865d7c524`. Do not "fix" one side alone.

### (c) Rooms, roofs, lighting

The trigger counts:
- 600 RoomTriggers, 22 PortalTriggers and 463 FloorTriggers (`Floor`, `BuildingUUID`,
  `AttachedRooms`);
- 330 TriggerLighting and 524 TriggerAtmosphere, whose `ResourceIDs` link to the presets.

Shapes:
- `PhysicsType` 2 is a box: `Extents`, taken as full size and centred on the Transform.
- `PhysicsType` 3 is a prism: a local x/z polygon extruded upwards by `Height`.

**`IsRoof` is set on one FloorTrigger in the whole game**, so it cannot mark roofs alone:
- A room counts as **roofed** when its `RenderMethod` is 1. 599 of the 600 rooms have it:
  the game cuts away their roof when a character is inside. A room also counts when a roof
  floor lists it.
- A floor is **covered** when its building has a higher floor (176 floors).

Lighting:
- Each level carries its `meta.lsf` `LevelLighting` and `LevelAtmosphere`.
- Presets: 219 lighting resources (sun colour, intensity, yaw, pitch, size, Kelvin; sky
  colour and intensity) and 452 atmosphere resources.

Probes:
- 4,212 probes with 5,806 captures, one per lighting resource.
- The 64-byte L1 SH of each capture is stored inline; 180 SH files are missing from the
  paks.
- The BC6H cubes are referenced by path prefix:
  `<prefix>_Diffuse_RGB.dds`, `_Specular_RGB.dds`, `_SH.dat`.

### (d) Effect lights

11,413 `.lsfx` files hold 5,876 `Light` components in 4,839 effects. Each record has:
- the effect path and the light template UUID;
- start and end time;
- peak and mean intensity, peak radius;
- the colour at the intensity peak;
- flicker amount and speed, offset, override flags.

These are definitions only. Where an effect plays is runtime state (bg3se `Effect`, or the
light array while it is on screen).

## Runtime (layer)

### Loading

- **When.** `layer/assetdb.cpp` reads the file on the first call that needs it:
  - a core query;
  - or the join check.
- **Location.** `data/assetdb/bg3rtx_assetdb.bin`, or `%BG3RTX_ASSETDB%`.
- **Validation.** Before anything is served, it checks:
  - the magic, the version and the size;
  - every section's record size;
  - every cross-reference range;
  - the sort order of the shader tables.
- **Failure.** A missing or stale file logs one line, and every query returns nothing.
- **Off switch.** `assetdb=0` disables it.

### Queries

The queries in `Bg3AssetDbApi` work in absolute world space. Add the camera position
(set 0 b2 `_m6`, std140 offset 208) to the lighting's camera-relative positions.

| Query | Returns |
|---|---|
| `lights_near(pos, r)` | Lights of the current level within `r`, nearest first. Uses a 16 m hash grid built when the level changes. |
| `lights_in_box(min, max)` | Lights whose `(pos, range)` sphere overlaps the box. Ranges above 48 m sit in a separate always-tested list. |
| `material(hash)` | Template, class, flags, shading model, pass, stage, variant, alias count, CB bindings, and one slot per role (kind, set, binding, VT layer, CB offset, default value, name) |
| `engine_shader(hash)` | The engine shader's name |
| `room_at(pos)` | inside room / roofed / covered floor / portal, room name, building, floor, and the lighting and atmosphere presets. The smallest containing lighting trigger wins, else the level's. |
| `probes_near`, `probe`, `probe_capture`, `preset`, `light`, `string` | Record access |
| `current_level(&name, &source)` | The level and how it was found (`"se"` when the Script Extender bridge reported it) |
| `runtime_lights(pos, r)` (v2) | The bridge's lights joined to their database records, plus the placed lights it did not report; the database alone without the bridge (docs/SE_BRIDGE.md) |
| `se_info`, `se_environment` (v2) | The bridge's state; the atmosphere and lighting resource in force at a point |

### Level detection

The level is found in two ways:
1. **From the save.** If the game was started with `-loadSaveGame <folder>` (as
   `auto-run.ps1 -Save` does), the layer reads that save's `SaveInfo.json`
   "Current Level". It parses the LSPK file list with a small LZ4 decoder.
   - With `-continueGame` it takes the newest save by file time.
   - Loading a save from the menu leaves no trace on the command line, so step 2 covers it.
   - Patch 8 saves compress that entry with zstd, which the layer does not decode, so in
     practice step 2 finds the level (see Join rates).
2. **From runtime lights.** At each join check, the lights the froxel lists use vote for
   the database levels that have a light within 5 cm of them, using a 0.5 m hash grid over
   all 19,572 lights. The voters are the b25 point and spot lights plus the b29 projectors
   at their near-face anchor.
   - A level with at least 3 votes, and more votes than the current one, replaces it.
   - While no level is set, a level that is the only one voted for is accepted. Positions
     match to float precision, so one exact match is strong evidence.
   - This catches a level change and a wrong hint.

### Join check

`assetdb_join=1`, every `assetdb_join_interval` presents (default 240).

At the first tiled-lighting dispatch after the check is armed, the layer records a copy of
the light list into a host-visible buffer. It recognises the dispatch by its bindings:
- set 1 b25 with 80-byte records;
- b29;
- b23 and b24;
- b13;
- set 0 b2.

It copies b25 (lights), b29 (projectors), b23 and b24 (froxel headers and index list),
b13 (global light scale) and the camera UBO. It reads the copy 4 presents later. The copy
is a GPU copy, so it is correct whatever memory the game keeps the list in.

It logs:
- **Lights.**
  - Valid slots, and the slots that the froxel lists reference this frame (the
    in-frustum set).
  - Matches by position within `assetdb_join_tol` (5 cm, plus the light's
    `MovementAmount`).
  - Median and maximum distance.
  - The colour check: chromaticity within 0.06 under five models (Color,
    sRGB→linear Color, Kelvin, Color×Kelvin, linear×Kelvin).
  - The radius check: 1/`m0.w` within 2% of `Radius`.
- **Near the camera.** The database point and spot lights within 25 m of the camera, and
  how many of them are in the array.
- **Projectors (b29).** The world matrix is inverted. They are matched against box lights
  at three anchors (near face, centre, far face) to find which one the editor's position
  is.
- **Shaders.**
  - The draws of the captured frame (VS, and FS, depth-only excluded) with a material or
    engine entry.
  - Every module the game has created so far.

Each check also writes `captures/assetdb_join_<stamp>_<n>.csv`, with one row per runtime
light and its match, for intensity calibration.

## Join rates

Measured on 2026-10-26, RTX 5090, with `auto-run.ps1 -Background -Save <scene> -Set
assetdb_join=1 assetdb_join_interval=120`. The figures are the steady gameplay checks.
Logs and CSVs are in `captures/`: `bg3rtx_assetdb_*.log` and `assetdb_join_*.csv`.

"Listed" means the lights the froxel lists use this frame. The other slots of the
4096-entry array hold lights of other places and frames: Grymforge has 1,182 valid-looking
slots and only 39 listed. Rates are therefore given over the listed set.

| | S1 foundry (CTY_Main_A) | S5 Grymforge (WLD_Main_A) | S8 Water Queen's House (CTY_Main_A) |
|---|---|---|---|
| Level found | from lights (26 votes) | from lights | from lights, once projectors voted and a single uncontested level was accepted (fixed after 2 runs) |
| b25 point/spot, listed | **26 / 40 matched (65%)** | **28 / 39 (72%)** | 1 / 3 |
| Position error of matches | 0.00 mm median and max | 0.00 mm | 0.00 mm |
| Colour (chromaticity) | 26 / 26 | 44 / 44 | 1 / 1 |
| Radius (1/`m0.w` vs `Radius`, 2%) | 26 / 26 | 44 / 44 | 1 / 1 |
| Database point/spot lights within 25 m that are in the array | 11 / 13 | 10 / 22 | 0 / 0 |
| b29 projectors (box lights) matched, near-face anchor | 16 / 25 | 16 / 58 | 2 / 2 in one check; 2 / 59 in a later check of another run |
| Draws with a known shader (VS) | 100%: 88-97.5% material + rest engine | 100%: 79-93% material + engine | 100%: 86-97% material + engine |
| Draws with a known shader (FS, depth-only excluded) | 99.5-99.9% | 98-99.9% | 99.7-99.9% |
| Shader modules created so far | 1,449: 72.1% material, 23.8% engine, 59 unknown | 1,157: 62.4% / 29.9% / 89 | 1,353: 70.1% / 25.6% / 59 |

**The unmatched runtime lights are not placed lights.** Each was traced to the nearest
level objects with `captures/assetdb_join_*.csv`:
- **Foundry.** 13 of the 14 are glasshouse lamp **items** (`LTS_GEN_Glasshouse_Lamp_*`,
  children of the `BASE_LIGHT` item template). Their light is created by the item at run
  time and sits 0.29-0.58 m from the item origin. The 14th is a constant white light
  (colour 4000, r = 18-24) that appears in every scene: the **character light**
  (`ActiveCharacterLightID`).
- **Grymforge.** 3 torch and bowl items, 6 hanging-bowl scenery pieces whose light comes
  with their visual, 1 shadowed light, and the character light.
- **Water Queen's House.** Its sub-level `BGH_WaterQueenHouse_B_ART` places **no lights at
  all** (only scenery and items). It is lit by projector lights of `CTY_LowerCity_A`, which
  match exactly, by item lights and by the character light.
  - One check of an earlier run listed 59 projectors of which only 2 matched. This was not
    reproduced in the next run (7 valid, both listed ones matched), and remains unexplained.

**Projector anchor.** For box lights, the editor's position is the **centre of the box's
near face** (box space (0.5, 0.5, 0)). The box centre and far face never match.

**Colour model.** The runtime chromaticity is the black-body colour of `Kelvin` for lights
with `UseTemperature`, and `Color` otherwise. `Color × Kelvin` matches every light,
because Kelvin lights keep `Color` = white.

**Intensity.** The runtime magnitude `max(m1.rgb)` ≈ `Intensity` × 1.00-1.19 (median;
range 0.97-1.77). A proper fit, including `Gain` and the global scale `b13`, is left to the
CSVs.

**Offline shader join.** `python -m tools.assets join` against the shader dump of every
earlier run (`captures/shaders`) and the three traced frames of AUDIT_ENGINE_DATA:
- **Modules.** 6,135: 5,619 material (91.6%), 371 engine (6.0%), 145 unknown. The unknown
  ones include the layer's own compiled shaders.
- **Draws with a known VS.** 100% in all three frames.
- **Draws with a known FS.** 99.6% / 99.7% / 99.4%.

**The save hint does not work on Patch 8 saves.** They store `SaveInfo.json`
zstd-compressed (entry flags 0x23), and the layer has no zstd decoder. The layer logs this
and falls back to the runtime lights, which found the level in every run. The hint still
works for LZ4 or uncompressed entries.

## Distribution: ship the tool, not the data

**Recommendation: generate the database at install time on the user's machine. Do not
ship it.**
- The data is derived from Larian's files. The modding terms allow using the assets to make
  mods, but redistributing extracted game data is a different thing. AUDIT_ENGINE_DATA
  §3 already recommends "keep caches local, built from the user's install".
- The build needs only the install and CPython. It takes about 40 s on 16 cores, and
  about 3-4 minutes on one.
  - The launcher (`tools/launcher`), or the first start of the layer, can run it once and
    re-run it when `manifest.json`'s package fingerprint no longer matches.
  - The game no longer gets patches, so in practice that is once.
- The output is 8.0 MB (2.0 MB gzipped; 6.9 MB with `--no-names`). That is small enough
  that shipping would be easy, but size is not the argument.
- `data/assetdb/` is in `.gitignore`.

If a Python dependency at install time is unacceptable later, the readers are small enough
to port into the launcher (C++). `layer/assetdb.cpp` already has the LSPK and LZ4 parts.

## Limits and unverified points

- **Lights the files do not hold.**
  - Item-attached lights (lamps and torches as items), character lights and spell/VFX
    lights are not in `Lights/`. They are 28-35% of the listed runtime lights in the
    foundry and Grymforge.
  - Item lights come from the item template's visual or script (`BASE_LIGHT` children,
    `Stats: OBJ_Torch`). Adding them offline means resolving item placements × the
    light their visual attaches.
  - **The Script Extender bridge covers them at run time** (docs/SE_BRIDGE.md). In the
    foundry, the listed lights covered went from 26/40 (65%, database) to 40/40 (100%,
    bridge + database). The 14 missing lights were 12-13 glasshouse lamp items, the
    darkvision light of the controlled character, and one lamp whose item had not
    loaded yet.
  - Effect lights are definitions without placement.
- **Story state.**
  - Lights toggled by Osiris or layers, and sub-levels loaded later
    (`not_starting_loaded`), are in the database whether or not the game shows them.
  - The runtime array is the truth for what is on.
- **Intensity units.** `intensity` is the artistic value. Mapping it to the runtime
  `m1.rgb` (with Kelvin, `b13` and pre-exposure) needs a fit from the join CSVs; it is
  not done here.
- **Trigger shapes.** Box = full-size centred `Extents`; prism = extruded upwards from the
  Transform.
  - An offline check counted level objects inside each shape. Prisms upwards hold
    98k points, against 69k centred and 13k downwards.
  - Not confirmed against the game's own roof fading.
- **Probe shape.** `LightProbeShape` is stored raw (1 = box with `Size`); the sphere case
  is assumed.
- **Material roles are name-based.**
  - Texture names like `MSKColor`, `ColorRamp` or `GradientMap` stay `mask_map` or
    "other".
  - Gradient-mapped and multi-layer character materials (CHAR_BASE_GM…) need their
    shader to interpret the textures; the table gives the bindings, not the formula.
- **Shared shader hashes.**
  - The bindings agree by construction (same SPIR-V), but the class and flags are the
    canonical template's.
  - `SREF` lists every template that shares a hash.
- **Not decoded.** Terrain heightmaps, decals, fog volumes, scenery or HLOD geometry and
  virtual textures (AUDIT_ENGINE_DATA A6/A7/§2.6) are not in this database.
