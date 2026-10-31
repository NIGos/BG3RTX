# Script Extender bridge

The bridge carries game state that the GPU never sees from the BG3 Script Extender
(bg3se v33) into the layer:

- every light entity near the controlled character: position, radius, colour,
  intensity, type, flicker and movement, on/off state, and owner (placed, item,
  scenery, character);
- the current level and Osiris region;
- the atmosphere and lighting resource each trigger is set to (BG3 has no time of day).

It implements AUDIT_ENGINE_DATA §2.3 route (b) and round 2 S2/S3/S5, and feeds D-LS
(TRACK_D_LOCAL_LIGHTS.md 9.8).

Code:
- `mods/BG3RTX_Bridge/` is the Lua mod (client and server contexts).
- `scripts/install-se-bridge.ps1` installs and removes it.
- `layer/sebridge.cpp` reads its files.
- `layer/assetdb.cpp` merges them with the asset database behind `Bg3AssetDbApi` v2
  (`common/assetdb_api.h`; `host_api.h` is unchanged).
- `core/ls_lights.cpp` and `shaders/gi/rt_local_shadows_match.hlsl` give D-LS its
  per-light data.

Without the Script Extender, without the mod, or with `se_bridge=0`, everything works as
before. The database alone then supplies the placed lights.

## Two games at once

Shared test runs (`scripts/auto-run.ps1`, docs/TEST_SCENES.md "Concurrent runs") can
have two games running, and both would read and write the same `bg3rtx\` files. Each
shared run therefore adds `bg3rtxse=slot<N>` to its game's command line. The layer reads
it from its own process (`GetCommandLineW`, `se_token()`), the Lua from
`Ext.Utils.GetCommandLineParams()`, and both use `bg3rtx\slot<N>\` (heartbeat
`active.txt`, `lights.txt`, `world.txt`, error files). Without the argument (exclusive
runs, the player's launcher) the folder is `bg3rtx\` as before, and an older Lua that
does not know the argument simply sees no heartbeat there and stays idle. The slot
number is unique among running games because each holds its slot's lock.

## Install

```
.\scripts\install-se-bridge.ps1              # install or update
.\scripts\install-se-bridge.ps1 -Status      # what is installed, and the output files
.\scripts\install-se-bridge.ps1 -Uninstall
```

**Layout: loose files, no .pak, no `modsettings.lsx` change.**
- The extender reads `Mods/<Folder>/ScriptExtender/Config.json` for every module in the
  load order (`ExtensionState::LoadConfigs`). It reads them through the game's file
  system, which also serves loose files from `<game>\Data`.
- The bridge therefore rides on the base module, which is always loaded:
  `<game>\Data\Mods\GustavX\ScriptExtender\{Config.json, Lua\BootstrapClient.lua,
  Lua\BootstrapServer.lua}`. Its `ModTable` is `BG3RTX_Bridge`.
- A marker file, `BG3RTX_Bridge.installed`, lets `-Uninstall` remove only our files. The
  script refuses to touch a `ScriptExtender` folder there that is not ours.
- **Verified** with v33 on 2026-10-26: the Lua ran and wrote its files on every run.
- A packed mod would also need a `meta.lsx` and an entry in `modsettings.lsx`, which the
  game's mod manager rewrites. The loose layout avoids both.
- The `GustavX\Story` folder next to it is managed by Vortex. Our files sit in their
  own subfolder, so Vortex does not own them.

## Transport

`Ext.IO.SaveFile` writes under
`%LOCALAPPDATA%\Larian Studios\Baldur's Gate 3\Script Extender\bg3rtx\`. The Lua sandbox
has no `io`, `os`, sockets or FFI (AUDIT_ENGINE_DATA R2.4), so files are the only
channel.

| File | Writer | Rate | Content |
|---|---|---|---|
| `lights.txt` | client tick | 4 Hz | header + one line per light within 60 m of the character |
| `world.txt` | server tick | checked at 1 Hz, written on change | level, region, the atmosphere and lighting triggers of the level and their current resource |

Format v1 is plain text: one header line, then `L` or `T` lines, then `END`.
`SaveFile` rewrites the whole file, so the reader accepts a file only when it ends with
`END`. A half-written read is dropped, and the next poll gets the whole file.

```
BG3RTX_SE 1
seq <n> ms <clock> level <name> host <x y z> scan_us .. avg_us .. max_us .. all <n> classified <n> out <n> classify_us .. classify_max_us ..
L <entity id> <owner> <type> <x y z> <radius> <r g b> <intensity> <kelvin> <flags> <channel> <flicker speed> <flicker amount> <move speed> <move amount> <template name>
END
```

- `owner`: 1 placed, 2 item, 3 scenery, 4 character, 0 unknown.
- `type`: 0 point, 1 spot, 2 box.
- `flags`: the `Bg3RtLightFlags` low bits: enabled, use-temperature, fill, flicker,
  moving, shadow, cast-shadow, volumetric shadow, player light.

**Reader (`layer/sebridge.cpp`).**
- A thread of its own polls both files' write time every 100 ms. On a change it reads
  the file (with share flags, so the game is never blocked) and parses it. It then
  publishes an immutable snapshot through a `shared_ptr`.
- The render thread only copies that pointer. Nothing on it waits for a file.
- **Freshness.** A snapshot counts only if it was written after the game process
  started and is younger than `se_stale_ms` (default 3000 ms). Otherwise the API reports
  the bridge as inactive, and the database alone is used.
- The thread starts at the first present and stops at `vkDestroyInstance`.

## What the Lua exports, and how

Measured against the running game. The field names come from `Ext.DumpExport` of live
entities.

**Light entities (client).**
- Source: `Ext.Entity.GetAllEntitiesWithComponent("Light")`. In the foundry save this
  returns **about 4,020 entities**: the lights of every loaded sub-level of `CTY_Main_A`,
  not only the nearby ones.
- Each entity has `ls::LightComponent` (`Color`, `Radius`, `Intensity`, `Kelvin`,
  `Flags`, `LightType`, `LightChannelFlag`, `WorldTransform`, `Template`...). Its only
  other component is `ecl::level::PresenceComponent`, so the owner cannot be read from the
  entity itself.
- `Light.Template` is the light's template resource. `Template.FileName` tells placed
  lights apart from visual lights:
  - `Mods/*/Levels/<level>/Lights/_merged.lsf` or `CombinedLights/_merged.lsf` are
    placed lights. These are the database's.
  - `Public/Shared[Dev]/RootTemplates/_merged.lsf` are lights that come with a visual
    (lamps, torches, the character light).
  - No template means a character's timeline light.
- **Live state.** `Light.Flags` (`Enabled`, `UseTemperature`, `FillLight`,
  `IsFlickering`, `IsMoving`, `PreExpose`), `Intensity` and `WorldTransform.Translate`.
  Every glasshouse lamp is two entities at the same position: one enabled at intensity
  30000, one disabled at 0.
- **Flicker and movement** come from `Template.Speed`/`Amount` and
  `MovementSpeed`/`MovementAmount`.
- **Shadow flags** are `Template.Shadow`, `CastShadow` and `VolumetricShadow`.

**Owner.**

| Owner | How it is decided |
|---|---|
| placed | template file under `Levels/*/Lights/` or `CombinedLights/` |
| character | the entity is in a `CharacterLight.Lights` list (`ecl::CharacterLightComponent`: the timeline rim and fill lights, boxes attached to `Head_M`), or the template name starts with `Light_Player_Darkvision` (the darkvision point and spot that follow the active character; flag `player light`). `Light_Player_Fill_*` are fill lights at fixed world positions and are classified like other visual lights (the layer re-tags them when an older Lua reports them as character lights) |
| item | a root-template light with an `ecl::Item` entity within 1 m (`Ext.Entity.GetEntitiesAroundPosition`) |
| scenery | a root-template light with a scenery entity within 1 m |
| unknown | a root-template light with neither within 1 m; D-LS treats it like scenery |

**The "character light"** that AUDIT/ASSET_DB found in every scene ("colour 4000,
r = 18-24") is the darkvision pair:
- `Light_Player_Darkvision_Spot_Drow`: spot, radius 24, intensity 4000, white,
  channel 129;
- `Light_Player_Darkvision_Point_Default`: point, radius 18, intensity 0, `FillLight`.

Both sit 2.0 m above the controlled character. Their templates have `Shadow = false` and
`CastShadow = false`. The other character lights (`Rim1`, `Rim2`, `Fill1`) are box
lights, so they go to the projector loop, which D-LS does not handle.

**Level and region.** The level is `Level.LevelName` of the controlled character (client).
The region is `Osi.GetRegion` (server). In the foundry both are `CTY_Main_A`.

**Atmosphere and lighting.**
- `ServerAtmosphereTrigger` and `ServerLightingTrigger` (130 and 65 entities in
  `CTY_Main_A`) carry `Current{Atmosphere,Lighting}ResourceID`. That is what story
  scripting set on each trigger.
- Lua has no point-in-trigger test, and the blend state the renderer uses has no
  accessor (AUDIT_ENGINE_DATA S5). So the bridge exports the triggers by name, and the
  layer takes the containing trigger from the database's trigger shapes
  (`se_environment`).
- **BG3 has no time of day** (AUDIT_ENGINE_DATA E5).

## Cost

**Lua.** Measured with `Ext.Timer.MicrosecTime` inside the tick, foundry, 4 Hz.

| Step | Cost |
|---|---|
| naive first version (read every light's fields each export) | 10.6 ms per export (4,017 lights) |
| one-off classification (template file, owner query), at most 100 entities per tick | **30 ms in total**, spread over about 40 ticks after a load; **at most 1.3-1.6 ms in one tick** |
| each export (122 lights written) | **0.73 ms average, 3.4 ms maximum** (the maximum is a full re-diff of the list after streaming) |

- That is about 2.9 ms of Lua per second on the client logic thread, which is about 0.3%
  of it. It is small, but it is **not zero**: an export is a 0.7 ms stall of that thread
  every 250 ms.
- Most of the cost is reading the properties of SE objects (`Light`, `WorldTransform`,
  `Flags`), not string formatting.
- Cheaper is possible if needed: exports at 2 Hz, or `Ext.Entity.Subscribe` on `Light`
  changes instead of polling (AUDIT_ENGINE_DATA S3; whether it fires for render
  components is unverified).

- Placed lights are classified once and cached by entity handle. Handles are stable
  userdata that work as table keys.
- After that, only the flags and intensity of placed lights within 60 m are read
  again, from a 16 m grid. Visual and character lights also get their position.
- The light list is re-diffed when its length changes, and otherwise every 5 s.

**Layer.**
- The reader thread parses a 15-20 KB file 4 times a second. Its time was not measured.
- The render thread pays one `shared_ptr` copy per query. The core re-queries
  `runtime_lights` every 8 frames, or when a new export arrives.

**GPU.** The join pass (4096 threads × up to 1,024 uploaded lights) is in TRACK_D_LOCAL_LIGHTS.md 9.8.

## API (`Bg3AssetDbApi` v2)

| Function | Returns |
|---|---|
| `runtime_lights(pos, r, out, max)` | The bridge's lights within `r` (when it is active), each joined to its database record when it is a placed light (5 cm + jitter), plus the database's placed lights the bridge did not report. Within 55 m of the character, a database light the bridge did not report is not loaded now (a sub-level not streamed in), so it is left out. Without the bridge: the database's placed lights. Each light carries `source_radius` (below), flags, owner and type. |
| `se_info(out)` | Whether the bridge is active; export number and age; level and region; character position; light counts; the Lua cost. |
| `se_environment(pos, &atm, &light)` | The atmosphere and lighting resource UUIDs in force at `pos`: the smallest database trigger containing it, with the bridge's current resource for that trigger if story scripting changed it, else the trigger's (or the level's) own. |

The bridge's level also becomes the database's current level (source `"se"`), ahead of
the light-vote detection.

### Source radius

**BG3 authors no emitter size.** No field in the light templates, the placements, the
runtime `LightComponent` or `LightDesc` gives one. The only sized lights are box
lights. So the radius comes from the **fixture class**, read from the template name.
The first keyword that matches wins:

| Keyword | Radius (m) | | Keyword | Radius (m) |
|---|---|---|---|---|
| candle | 0.015 | | campfire / fireplace | 0.35 |
| torch, sconce | 0.05 | | bonfire, lava | 0.50 |
| lamp, lantern | 0.08 | | forge | 0.40 |
| glasshouse (lamp) | 0.10 | | fire | 0.25 |
| crystal | 0.08 | | window, fill, exterior | 0.60 |
| brazier | 0.20 | | ambient, bounce, sky | 0.80 |
| chandelier | 0.30 | | `FillLight` flag | 0.60 |

Anything else gets `ls_source_radius` (0.05).
- **Fill classes.** Fill and ambient lights stand in for bounce light, not for a lamp.
  A large radius gives them the soft, broad occlusion such light has.
- **Not exact.** These are typical fixture sizes, not measured meshes. The next step is
  to measure each light's fixture from the TLAS (the distance from the light to the
  enclosing geometry; TRACK_D_LOCAL_LIGHTS.md 9.8).

## Coverage of the runtime lights

The join check (`assetdb_join=1`) now also matches every froxel-listed point and spot
light against the bridge's export. It uses position (5 cm + jitter; 3 m for item and
character lights) and radius (2%), and logs one `[se] join:` line per check.
Steel Watch Foundry (S1), steady gameplay, every check of three runs:

| | Lights covered (of 40 listed) |
|---|---|
| Database alone | **26 (65%)** |
| Bridge alone | **40 (100%)**: 26 placed, 12-13 item, 1 character, 0-1 unknown |
| Bridge + database | **40 (100%)**, position error 0.000 m |

- **The 14 lights the database lacks** are the 12-13 glasshouse lamp items
  (`LightSources_GEN_Glasshouse_Lamp_A`) and the darkvision spot of the controlled
  character. These are exactly the lights ASSET_DB.md traced by hand.
- **The 8 "unknown" owners of one run** were lamps whose item had not streamed in when
  the light was classified. They are now asked again on each re-diff, up to 5 times. The
  next run had 13 items and 0 unknown.
- Inside D-LS (`ls_stats=1`), **100% of the 2.49 light evaluations per pixel matched**
  (all through the bridge). With the bridge off, the character and item lights stay
  unmatched and are traced as before.
- **Other scenes were not measured with the bridge** (Grymforge S5 and the Water Queen's
  House S8 have the database rates of ASSET_DB.md only).

## Verified, and not

**Verified** (foundry, RTX 5090, bg3se v33, `-Background` runs):
- The loose install loads.
- The Lua runs in both contexts, and the files arrive at 4 Hz.
- The reader never blocks the render thread: it is a thread of its own, and the render
  thread only copies a pointer.
- With the export stale or switched off (`se_bridge=0`), everything falls back to the
  database within `se_stale_ms`.
- The join gives 100% of listed lights, and owners, flicker and flags arrive.
- **0 non-finite values** in every dumped image of the A/B traces.
- **`-Validation`**, foundry:
  - Before merging main's RR-before-post (`36f3c6e`), against a baseline run of main
    without the bridge: the same 7 message IDs, no new one.
  - After that merge, one more ID: `VUID-vkDestroyImage-image-01000`. It appears in
    the same run as `[rrpost] the game destroys an image our post steps read`, so it
    belongs to the RR post chain (`layer/rrpost.cpp`), not to this change. That was
    not confirmed with a validation run of main alone.

**Not verified:**
- **Level changes and streaming** across a load (the reset on a level change is
  written, but untested), and saves started from the menu.
- **`se_environment`** against the game's actual atmosphere: the unit test checks only
  that a trigger and resource come back.
- **`Ext.Entity.Subscribe` for render components**, which would remove the polling.
- **Multiplayer clients** (`ReservedUserID == 1` picks the local player).
- **A torch-lit scene with shadow-mapped lights.** In the foundry no point or spot
  light casts a game shadow.

## Join tolerances (2026-10-27)

Every tolerance of the light join is derived from the bridge's own behaviour, the float
format and measured game data (docs/QUALITY.md R0). Code: `layer/sebridge.h` (constants and
`se_static_tol` / `se_static_radius_tol` / `se_covers` / `se_stale_ms`), `core/ls_lights.cpp`
(the D-LS upload, which writes both tolerances into each record),
`shaders/gi/rt_local_shadows_match.hlsl` (reads them), `layer/assetdb.cpp` (`runtime_lights`
and the `[se] join` check use the same rules).

| What | Was | Now | Derivation |
|---|---|---|---|
| Position, lights that stay put (placed, database) | 5 cm + jitter | √3·(4 ulp(\|p\|max + 80 m) + 0.05 mm) + jitter: 0.5 mm at Campsite, 6.9 mm at 8.5 km | Per coordinate: the game's camera-relative position and the shader's + camera (2 roundings), the bridge's float parse (1), the engine's transform composition (1: the array was within 2.9 ulp of the database in the 93 joins of `captures/assetdb_join_20261026-*.csv`, \|p\| ≤ 1570 m); the bridge writes `%.4f` (0.05 mm). Database lights reach \|p\| = 8497 m. |
| Position, moving lights (character, item, visual) | character 4 m, others `ls_dyn_tol` 1.5 m | static + `ls_dyn_speed` (8 m/s) × T_age, ≈ 3.3 m before any measurement, ≈ 4 m measured | T_age = longest export interval + longest age at arrival + 2 frame times (last 16 samples each). Measured interval: 0.289 s and 0.330 s mean (97 and 186 exports, `bg3rtx.log` / `bg3rtx.a.log` 2026-10-27; the Lua exports on the first tick after 250 ms). Age at arrival ≤ the reader's 100 ms poll + parse. v_max: 8 m/s is the `MovementSpeedSprint`/`Dash` most root templates set (Shared `_merged.lsf`: 13 and 11 of them; up to 12 m/s for a Cloaker or the Hellsphere); humanoids inherit the engine default (no template states it), so it is measured in game (below). |
| Radius, lights that stay put | 2% + 1 cm | 0.5 mm + 4 relative ulp | The bridge writes `%.3f`; 1/r in the game (0.5 ulp), the shader's 1/inv_r (OpFDiv, 2.5 ulp in Vulkan), the parse (0.5). The database's range matched 1/inv_r to 1e-6 in the same captures. |
| Radius, moving lights | 2% + 1 cm | unchanged | **Not derived**: a VFX light's radius may be animated between exports, and no capture holds both values of a moving light. The `[se] join` line now logs the largest relative disagreement it accepted for placed and for moving lights; a run with carried torches and weapon VFX lights (S3, S8, S14) measures it. |
| Database lights the bridge did not report | dropped within 55 m of the host | dropped where the export covers them (`se_covers`): within 60 m of the host and in the Lua's vertical cell rows (cy ± 2 of 16 m) | The Lua's `RANGE` and its grid scan; the host's `%.3f` as margin. The 55 m sphere also dropped lights 32-55 m above or below the character, which the Lua never scans. |
| Upload refresh | every 8 frames | on every export, else every 0.25 s | Nothing the bridge reports changes faster than its export period; 8 frames was 4 refreshes per export at 120 fps. |
| Staleness | `se_stale_ms` 3000 | `se_stale_periods` (4) × the longest of the last 16 export intervals + 100 ms poll: 1.1 s nominal, ≈ 1.4 s measured | k = 4: one export is in use, 3 in a row may go missing (a hitch, a failed Lua tick) before the lights fall back to the database. `se_stale_ms` > 0 still sets a fixed limit. |

**Measurements in game** (each logged, no extra setting):
- `[se] motion (120 exports): ... fastest X m/s (owner, name), N above 8.0 m/s; export
  interval max Y s`, every 30 s: the fastest moving light between consecutive exports (a
  teleport or level change also counts). It checks `ls_dyn_speed`.
- `ls: per-light data: ... moving-light tolerance T m = 8.0 m/s x A s (export interval I +
  age at arrival G + 2 frames of F s)`: the derived tolerance and its inputs.
- `[se] join: ... radius disagreement max a (placed) / b (moving, relative); moving
  tolerance T m (D s between export and frame)`: with `assetdb_join=1`.

**Classification absence fix (source only, 2026-10-31).** The first export queues
classification after that tick's classification loop, so `seq 1`, zero output and
thousands of `all` entities is expected startup behavior. The reader previously
logged only this first fresh export. Retained later files contain nonzero records;
this is not evidence of a failed Light-component API mapping.

The old reader also treated omitted database lights as absent while classification
was incomplete. The existing Lua now recounts successful CURRENT live membership
on resync, exports the pending count, and forces resync on same-count level changes.
The reader requires known `all/classified/pending/out`, `classified == all`,
`pending == 0`, and an exact emitted-body count before `se_covers` can supply negative
absence evidence. Explicit emitted lights remain usable independently. Failed
classification remains UNKNOWN rather than an invented absence. A once-per-level
completion log makes the transition visible; no global percentage threshold exists.

The controlling key remains `se_bridge=1` (existing default). Legacy exports missing
pending remain readable and supply their explicit records; they no longer suppress
unmatched database fallback. This compatibility change is conservative UNKNOWN,
not current activation proof. Both source Lua and layer must be updated to obtain
complete negative coverage; no installed game script/profile was changed here.
Standalone source tests cover 16 parser/coverage scenarios, including startup,
partial membership, pending equal-count replacement, legacy/malformed counts and
retained explicit records. No game/GPU run verified this source change yet.

Read-only observations and copied complete outputs live at
`captures/m2-foundry/se-bridge-review/evidence.json`: default snapshot seq 525 has
4013/4013 classified and 124 emitted records (110 enabled); historical slot0 has
3964/3964 and 89 records, slot1 has 4082/4082 and 94. Exact paths, timestamps and hashes
are retained. An earlier header-only seq 1385 observation had 132 records; that file
was overwritten by the next run before copying, so no hash is invented for it.

The bridge already supplies process-local entity IDs, world positions, type/radius,
raw colour/intensity/Kelvin and explicit enabled/channel/owner/flicker flags to the
existing `runtime_lights` provider. These are sampled states, not permanent resource
UUIDs or a complete transform/shape packet. Rotation, spot cones, box dimensions,
cookies and calibrated native colour-temperature/exposure/intensity conversions
still require existing DB/native bindings. The provider's name-based source-radius
heuristic is not newly certified physical photometry. No new resolver is added.


## Native Blackbody read-only probe (prepared, not run)

Official Norbyte/bg3se v33 is pinned to commit
`af85fd2bd2be28646539060d45df487f4d3314b9`. Its public LightComponent and generated
IDE helpers expose Blackbody as vec3, plus Gain, IntensityOffset, UUID and world
transform. This establishes a readable field, not its normalization/multiplication
meaning. The five cell point lights use temperature; no Planck approximation,
LUT-coordinate guess or white/intensity whitelist was accepted as a converter.

Existing queued auto-run can opt in with `-SELightProbe` in job.args. It is limited
to exclusive static foreground runs, rejects an already held Shift, temporarily
injects only RightShift around launch and releases it in nested/outer finally.
Official v33 SetupScriptExtender samples Shift to enable its console without an
installed config change. Native console creation may activate its own window;
this diagnostic cannot claim no-activate/FG-timing equivalence.

A dedicated hidden child attaches only to the owned PID/start identity and exact
SE console title. It preserves the runner's console, sends a fixed one-shot Lua
single-line payload through CONIN$ (never global typing/window activation). Every
write and wait rechecks the canonical shared USER_PLAYING markers and the finite
consumed queue receipt. Context completion requires an exact S/C prompt on a new
console row beyond the pre-write cursor; an old tail prompt is never accepted.
The prior context is restored only after proven input completion. Partial native
writes or uncertain completion cause failure without repair typing; auto-run then
aborts only its owned game. The helper detaches in all cases and never falsely
reports uncertain input as restored. It reads at most 32 S1-cell candidates and writes
nonce-matched diagnostic reports only. No subscriptions, entity setters, installed
Lua/config/profile edits or game launches occur in the helper. The normal queue
still owns every launch/cleanup and rejects USER_PLAYING/unowned games.

Readbacks include actual Blackbody/Color/Intensity/IntensityOffset/Gain, IDs,
UUID/name/position/radius/type/flags and reflected type. Missing/nonfinite core
fields fail explicitly. `assetdb_join=1`, `assetdb_join_interval=30` and existing
CSV output provide native b25 RGB/m3/m4/m5/current global scale for comparison;
no parallel native light capture is added. Default-off opt-in leaves normal runs
unchanged. CPU checks validate input-record layout, single-line budget, fresh/old
prompt boundaries, guarded write structure and PowerShell syntax. Twelve actual
permission-function tests use synthetic consumed receipts and linked TEMP trees:
canonical markers, missing/revoked/expired/closed permissions all fail closed.
Console transport, Lua execution and colour mapping still
await the first separately queued probe.

Primary source:
[Render.h](https://github.com/Norbyte/bg3se/blob/af85fd2bd2be28646539060d45df487f4d3314b9/BG3Extender/GameDefinitions/Render.h),
[console](https://github.com/Norbyte/bg3se/blob/af85fd2bd2be28646539060d45df487f4d3314b9/BG3Extender/Extender/Shared/Console.cpp),
[Shift enablement](https://github.com/Norbyte/bg3se/blob/af85fd2bd2be28646539060d45df487f4d3314b9/BG3Extender/Extender/Shared/dllmain.cpp).
Private source fingerprints/probe job draft are under
`captures/m2-foundry/se-bridge-review/`; the draft is outside queue inboxes.
