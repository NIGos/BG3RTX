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

D-LS uses it for each light's source radius and owner (TRACK_D_LOCAL_LIGHTS.md 9.8), merged with the
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
     practice only step 2 can find the level.
2. **From runtime lights.** At each join check, the lights the froxel lists use vote for
   the database levels that have a light within 5 cm of them, using a 0.5 m hash grid over
   all 19,572 lights. The voters are the b25 point and spot lights plus the b29 projectors
   at their near-face anchor.
   - A level with at least 3 votes, and more votes than the current one, replaces it.
   - While no level is set, a level that is the only one voted for is accepted. Positions
     match to float precision, so one exact match is strong evidence.
   - This catches a level change and a wrong hint.
   - It runs only with `assetdb_join=1` (the diagnostic join, below) or `assetdb_level_vote=1`
     (the vote alone, `vote_check`, no CSV; every `assetdb_join_interval` presents; live: turned on it
     checks at once, turned off with `assetdb_join=0` it forgets a level it found). Without either, a
     run on a Patch 8 save has no level: no roof volumes (rt_sun_replace inactive, the
     gi_far and hit-sun roof rules block every far hit) and no database lights for D-LS. Found
     2026-11-23 (.scratch/coord/hunt_core.md F1: 171 of 171 logs "level unknown").
   - `assetdb_level_vote` is **on by default since 2026-11-24** (user decision, claude/level-vote-on) in
     Medium, High and Ultra; Low writes 0 (no RT sun, local shadows or RT GI there, so no consumer). In release
     (no dev=1) `enforce_release_policy` writes it On wherever Sun shadows are ray-traced. Live
     2026-11-23/24 (`level_vote`): level found in S8 and Dialogue1, kept after an F8 reload, cleared on
     switch-off, 0 faults, about +0.2 ms. The offline PT put far GI screen hits FARTHER on 1.4% of px; the user
     accepts it as a regression fix (the consumers were qualified with the level known). Liveness row
     `level_known` (covers the key), with `roof_volumes`, `sun_replace_active`, `sun_resolve`, `ls_per_light_data`.
   - Fail closed (hunt2_lifecycle 2, 2026-11-23): a stale level would trust the sun with no roof volumes around
     the new camera (an interior sun leak). A check with at least 3 voters and none for the current level, and
     nothing better that qualifies, clears the level (`assetdb_vote` `clear`). A level load (a new G-buffer
     depth image) drops a copy recorded before it and re-runs the vote 30 presents later; that vote clears the
     current level on any lack of evidence for it, an empty voter list included. In play, no voters change nothing.
   - The bridge's level: a sub-level maps to its world (as the save hint); a level the database lacks clears
     the current one.

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

**Projector rate denominator.** The b29 counts above are over every valid slot, not the
froxel-listed ones (`layer/assetdb.cpp` counts `m_proj` without `pref`), so they
understate the listed rate (docs/LIGHT_CONVENTIONS.md 2).

**Projector anchor.** For box lights, the editor's position is the **centre of the box's
near face** (box space (0.5, 0.5, 0)). The box centre and far face never match.

**Colour model.** The runtime chromaticity is the black-body colour of `Kelvin` for lights
with `UseTemperature`, and `Color` otherwise. `Color × Kelvin` matches every light.
The matched lights kept `Color` = white, but 3,450 of the 9,616 Kelvin lights in the
database do not (docs/LIGHT_CONVENTIONS.md): keep `Color × Kelvin`.

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
and falls back to the runtime lights, which found the level in every run that had `assetdb_join=1`
(none since; see "Level detection" for `assetdb_level_vote`). The hint still
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


## Bounded native geometry export: S1 board ground (2026-10-31)

This private M2 export is separate from the packed asset database. Native snapshot
20261031-022347 qualifies Floor_B POSITION: ScaleZ acts on local X; the observed
Flip flag adds no position reflection. UV/normal/material equivalence is unproved.
`python -m tools.assets.check_floor_b_positions --capture <S1 capture>` verifies
ordered native vertices, then `probe_foundry_construction --construction
floor-b-ground --lslib <existing Packed>` exports the nearby subset. Source hashes
must match; no game launch, fitted transform, proxy geometry or snapping is used.

Three native LOD1 Floor_B pieces plus the existing Floor_A corner (independently
matched at draw 426 / instance 0 / LOD1) preserve topology/material IDs: 736 triangles,
1191 vertices. Bounds select candidates; actual triangle interiors intersect 11/16
low board-vertex lines for Floor_B, 13/16 combined. Both above/below heights are
retained. Remaining samples 4, 5 and 12 have exact coordinates in
`captures/m2-foundry/construction-native-ground/evidence.json`; other native draws
can underlie them. This is not continuous footprint, contact, opacity or room
closure. Existing Floor_C/wall exports remain adjacent; roof/opening closure is open.


### Fixed S1 open-bay geometry bundle

`python -m tools.assets.probe_foundry_cell --capture <S1 capture> --lslib <Packed>`
exports `captures/m2-foundry/reference-cell/`: 27 native-qualified mesh components /
24506 package triangles (board, seven floor pieces, corner, platform, west wall,
opening frame, pillars and beam). Nine additional authored components have no
unique native correspondence, even after ordered merged-window checks; they stay
in a separate visibility-unknown file, not an invented enclosure. No GR2 source
is missing. Shader/material/UV equivalence and parent-level roof state remain open.

A separate frozen/current-capture-only file contains 824 real heightfield
triangles with original draw/triangle provenance. Both original clip coordinates
and world positions must be finite; geo unprojection ignores Z and alone would
retain inactive Z-NaN vertices. Runtime XFB already rejects finite4 failures;
this is an offline export guard. Unknown terrain material never means opaque.
The expanded geometry intersects all 16 low board-vertex lines; signed heights
remain unmodified and do not certify contact/opacity/continuous support.
The optional `--persistent-foundry-terrain` writes a separate
`reference-cell-archive-terrain/` bundle after
`python -m tools.assets.probe_foundry_terrain --capture <S1 capture>` qualifies
both v8 render `.patch` files for terrain 6675c295-3445-49ef-92ec-1b27be2c48c5.
The bounded reader preserves authored float32 heights and opaque material tails;
physics SEBD `.bin` is rejected. Its full 7628 vertices / 13512 triangles and
738 holes correspond exactly to all active native draw 803 grid/cell/fan identities.
Native R16 height-image differences reach 0.000097275 world units, within the
declared registered-height-domain code plus FP32 transform/projection enclosure
(maximum 0.000210342); the actual GPU quantizer/filter is not certified.

The local bundle replaces only the 289 mainfloor triangles with persistent
render geometry; 535 parent-terrain triangles remain frozen. Original source
vertex/triangle, cell/fan and native primitive IDs survive selection/remapping.
Source/archive/implementation hashes must match the qualification evidence.
All 16 low-vertex lines still intersect geometry. Terrain material/layer/UV tails,
current destruction/deformation and parent roof state remain open; geometry is
not silently classified opaque. The reader intentionally rejects other patch
versions/dimensions rather than claiming a general terrain importer.


### Mainfloor terrain material inputs

`python -m tools.assets.probe_foundry_terrain_materials --native-cache <alpha_cache>`
recovers the same terrain's four ordered brush resources: SharTemple_C,
SharTemple_C_Broken_A, Gravel_B_Grey and Coastal_Sand_Grey_B. Their actual
albedo/normal/physical/detail texture UUIDs resolve to 14 distinct DDS resources
in the TerrainTextures and Engine banks; all decode with installed Pillow 12.3.0.
DDS bytes/dimensions remain authoritative export evidence; Coastal Sand albedo
LSF metadata says 128x128 while the actual DDS is 1024x1024, recorded rather than
rescaled or silently declared equivalent to current native image dimensions.

The bounded v8 tail decoder requires the exact LSF brush order and four
49x64 / 49x20 authored byte planes. These are not normalized blend weights.
The following length-prefixed BC7 UNORM images decode to 52x68 / 52x24 RGBA.
Alpha < 0.5 at integer cell coordinates reproduces 575 / 163 holes exactly,
independently tying the payload to native VS64e2568916ede3d6 texelFetch semantics
and the already checked active native topology. RGB is retained without asserting
that a compressed normal is a geometric face normal.

Native FS6650c75f3d3e4968 consumes this normal/hole image at s4b3, a painted-layer mask
at s1b2 and base slope/height curve LUT at s1b3, brush records at s1b4 and bindless textures at s2b32. Its UVs
use local grid X/Z, normalized height converted with native terrain scale/bias,
and signed triplanar weights derived from normalized (2*RGB-1), with the native
0.576 weight threshold/rebalancing. Layer alpha-height tests and subsequent
height contrast/offset decide contributions; summing/normalizing raw bytes would
be wrong. Shader source fingerprints and actual resource identities are retained
in `captures/m2-foundry/terrain-material-inputs/evidence.json`.

This increment decodes native inputs, not complete material sampling. Native
brush SSBO packing/upload, byte-plane-to-mask filtering/channel mapping,
derivative/LOD-dependent triplanar sampling and current image content remain
unimplemented. No opacity/material defaults or parent-terrain/roof claims follow.


`tools.assets.terrain_material` implements the native post-sample UV projection,
painted-layer alpha/height scores, competitive blend and base curve weighting.
Its inputs explicitly name sampled values/native members; it never automatically
binds authored field names to an unobserved upload layout. Native 0.001/0.01
branches, epsilon contributions, base remainder and factor-two curve branch stay
intact. CPU binary64 formula tests are not GPU FP32/fma or sampling equivalence.
Undefined normal/weight/power domains are unsupported, not replaced by black.

The S1 capture contains image metadata and geometry, not s1b4 brush bytes or
s1b2/b3 pixels. Minimum future same-draw input capture: s4b0 80-byte UBO (blend
width at byte72; two base indices at64/68), s1b4 descriptor offset/range and
112-byte-stride records0..3 plus actual base indices<64; s1b2 RGBA8_UNORM49x84
(16464 tightly packed bytes), s1b3 R16_UNORM256x4 (2048 bytes), and sampler/address
states s4b4/s1b1/s0b7. Preserve format/view/swizzle/row pitch and same draw identity.
The 256x4 s1b3 is a curve LUT, not a second painted mask: native textureGather
uses (normalY,0.25) and (normalizedHeight,0.75). Exact native derivatives/LOD
remain a separate missing raster observable. No generic capture system is added.


### Authored open-bay light candidates

`python -m tools.assets.probe_foundry_lights --join-dir <existing captures>`
reuses the existing light/world/template extractor for CTY_Main_A. The fixed cell
bounds select seven authored support-sphere candidates out of 3886 world lights:
five point and two box, no spot. All seven belong to the parent LowerCity level;
full affine recomputation agrees exactly with the extractor positions for these
instances. Raw placement/template/level chains, resource IDs, complete resolved
fields, default provenance and all transforms remain in
`captures/m2-foundry/cell-lights/evidence.json`. Archive Enabled is true for these
seven, but runtime activation/state and native photometric parameters stay UNKNOWN.

Five retained assetdb_join CSVs dated 2026-10-26 contain seven distinct historical
name+position candidates, one for each placement. Decimal CSV rounding plus the
existing FP32 affine evaluation enclosure separates two same-name lights without
an arbitrary join tolerance. This is historical reconciliation evidence, not an
S1 same-frame/native UUID/current activation certificate. Candidate selection
retains the existing extractor point/spot radius and box half-diagonal+att_end
policy; cone/OBB, generic nonuniform-transform/range completeness and native
colour-temperature/exposure/cookie/occlusion/shadow behavior remain unqualified.
No emissive surface is promoted into a runtime light and no radiance is guessed.


### Opt-in exact native mainfloor packet capture (not yet run)

In a frozen candidate root, create `trace_terrain_packet.request` before the
existing queued S1 run, then use its normal `trace.request` / F10 trace trigger.
The marker alone does not capture outside a trace. No new settings/host ABI or
queue path is added. `dump_images=0` does not suppress this explicit tiny packet.
Remove the marker from the candidate after that run; agents do not launch live.

Only VS64e2568916ede3d6 / FS6650c75f3d3e4968 with the exact native image profile is
supported. The draw event records a terrain_packet ID. Existing end-pass hooks
copy s4b0 bytes80, up to64 s1b4 records at stride112 (bounded by actual descriptor
range), s1b2 RGBA8_UNORM49x84 and s1b3 R16_UNORM256x4. Maximum25760 bytes/packet,
eight outstanding packets/trace (201.25 KiB). Source offsets/ranges/dynamic
resolution, view/swizzle/row pitch and available sampler metadata are retained in
`captures/<trace>/terrain_packet/manifest.json`; exact binaries sit beside it.
The deciding log is `[terrain_packet] frame F: copied N, declined M`. This is raw
input acquisition, not a native derivative/LOD/shading certificate.

No transfers occur inside a render pass and no native pass is split. The narrow
path requires a known single-subpass native pass, frozen ordinary descriptors,
nonattachment input images with the exact simple profile and TRANSFER_SRC usage,
and TRANSFER_SRC on the actual source buffers. Later storage aliases, push-storage
uncertainty and secondary execution decline. Native ONE_TIME_SUBMIT without
SIMULTANEOUS_USE is required; unsubmitted allocations stay alive until submit or
successful native reset/free. Submitted copies retire only after queue idle.
Missing/unsupported paths produce explicit status/logs rather than unsafe copies.

Retained S1 proves both source images have TRANSFER_SRC (usage23/7), but contains
no terrain buffer usage metadata. Therefore actual buffer support remains
UNPROVEN until the first targeted run. No compute-copy fallback is guessed;
only a real missing-transfer-source decline would justify that next change.
Offline verification: all four touched layer translation units compile, and13
standalone CPU range/overflow/alias checks pass. No GPU or game execution yet.

### Native terrain buffer readback correction (2026-10-31)

R4 proves s4b0 is UNIFORM_BUFFER usage 16, not TRANSFER_SRC. The exact packet
copy therefore needs the same shader-read approach already used for the camera.
Development fa3d9f4 adds raw uint copies of 80 UBO bytes and up to 7168 SSBO bytes,
with descriptor type/usage/alignment/range checks. It preserves the image transfer
path, post-pass capture point and bounded packet ownership. Host links and both
new SPIR-V modules validate; no original shader input changed. GPU byte capture
is pending. Actual-submit pinning/success and Submit2 stage coverage are being
corrected before this adaptation is released. This is not terrain shading parity.
