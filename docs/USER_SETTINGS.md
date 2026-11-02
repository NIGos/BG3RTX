# BG3RTX settings: user guide

BG3RTX adds ray-traced lighting, DLSS Ray Reconstruction and DLSS Frame Generation to Baldur's
Gate 3 (Vulkan renderer). You set it up with `bin\BG3RTX.exe`, the settings window and launcher.

## Starting the game

- **From the settings window:** pick your settings, then **Save and launch**. The launcher
  starts `bin\bg3.exe` (the Vulkan renderer) directly, with BG3RTX enabled for that process
  only. Nothing in the registry or the game folder is changed.
- **From Steam:** set the game's launch options (Properties > General) to
  `"<BG3RTX folder>\bin\BG3RTX.exe" %command%`. Steam then starts the launcher, which starts
  the game with BG3RTX and your saved settings, without showing a window.
- **Game folder:** the launcher finds Steam installs by itself. Otherwise use **Browse...** and
  pick the folder that contains `bin\bg3.exe`.
- **Overlays:** "Disable other Vulkan overlays" turns off the Steam, EOS and GOG overlay layers
  for that launch. NVIDIA Smooth Motion is always turned off when frame generation is on,
  because DLSS Frame Generation needs it off.

In the game's own Graphics options, set the upscaler to **DLSS** (Quality recommended). Ray
Reconstruction replaces the game's DLSS; it cannot run with FSR, XeSS or TAA.

## Comparing with the original image

Press **Ctrl+Shift+F9** while playing to switch every ray-traced effect off, and again to switch
it back on: sun and local shadows, GI, reflections, emissive light, the radiance cache. You then
see the game's own lighting in the same frame and view. DLSS and frame generation are not
touched, so the comparison is lighting only. Each press is logged in `bg3rtx.log` as `[ab]`.
`ab_hotkey=0` in `bg3rtx_user.ini` turns the hotkey off.

## Presets

The window opens on your GPU and suggests a preset:

| Preset | Meant for | What it does |
|---|---|---|
| Low | RTX 2060-3060, 4060; AMD/Intel with ray tracing | No ray-traced scene, no Ray Reconstruction. Screen-space GI and contact shadows |
| Medium | RTX 3070/3080, 4060 Ti/4070 | Ray-traced scene without alpha-tested objects, RT GI, fast local-light shadows, glossy reflections |
| High | RTX 4080/4090, 5080 | Everything ray traced |
| Ultra (RTX 5090) | RTX 5090 | High plus 2 GI rays, shadowed lights, rougher reflections, frame cap 58 (116 shown with frame generation 2x) |

The presets below RTX 5090 are estimates and have not been measured on those GPUs yet. Settings
your GPU cannot run are marked "(not supported)" and the presets skip them. For example, an
RTX 30 card gets no frame generation, and AMD/Intel get no Ray Reconstruction.

Changing any setting makes the preset "Custom". **Reset to preset** brings the preset back.

## Settings

The right-hand panel describes the selected setting. The marker next to each setting says how
a change reaches a running game:

| Setting | Choices | Applies |
|---|---|---|
| Ray Reconstruction | Off / On | live, brief hitch |
| Ray-traced geometry | Off / Opaque only / Full | Off/Opaque live; **Opaque <-> Full needs a restart** |
| Sun shadows | Screen-space / Ray-traced | live |
| Roof sun shadows | Off / On (on in Ultra) | live |
| Global illumination | Off / Screen-space / Ray-traced / Ray-traced High | live |
| Local light shadows | Off / Fast / Full / Full + shadowed lights | live |
| Box light shadows | Off / Shadowed / All (off in every preset) | live |
| Reflections | Off / Glossy / Standard / Rough | live |
| Frame generation | Off / 2x / 3x / 4x | 2x/3x/4x live; **Off <-> on needs a restart** |
| RT lighting intensity | 50-100 % | live |
| Frame cap (rendered fps) | Off / 30-240 | live |
| NVIDIA Reflex (Advanced) | Off / On / On + Boost | live |

**Experimental (being tested, off in every preset).** Each entry is one choice with levels; a
higher level includes the lower ones and writes all of their ini keys.

| Setting | Choices | Needs | Applies |
|---|---|---|---|
| Hidden-surface shading | Off / Materials / + Bounce / + Reflections | Full geometry; + Bounce: RT GI; + Reflections: Reflections on | **first enable: restart**; levels above Materials live |
| Emission at ray hits | Off / Hidden hits | Hidden-surface shading | live |
| Far-field sun bounce | Off / 6 m | Hidden-surface shading + Bounce | live |
| RT water | Off / Reflections / + Shadows / + Refraction | Ray-traced geometry | **raising a level: restart**; lowering live |
| Native coverage | Off / Hair + scalp / + Tilesets | Full geometry | **restart** |
| Physical hair ray origins (advanced) | Off / On | Native coverage | enable at startup, then live |
| Opacity micromaps | Off / On | Full geometry, GPU support | **restart** |
| Preserve cutout capture on overflow | Off / On | Full geometry | live |

What each level writes: Hidden-surface shading Materials = `rt_hit_material`,
`rt_hit_material_instanced`, `rt_hit_material_recycle`, `rt_hit_material_wind_rgb`; + Bounce adds
`gi_hit_direct`, `gi_hit_sun`; + Reflections adds `rf_hit_shade`, `rf_hit_gi`. Emission = `rt_hit_emission=1`
and `rt_emissive=1`. Far-field = `gi_far=1`, `gi_far_m=6`. RT water Reflections = `rt_water_geometry`,
`rt_water_reflections`, `rt_water_compose`; + Shadows adds `rt_water_shadows`, `rt_water_ls_shadows`,
`rt_water_game_shadow`; + Refraction adds `rt_water_refraction`. Native coverage Hair + scalp =
`rt_native_hair`, `rt_native_scalp`; + Tilesets adds `rt_native_tileset`.

**Developer only** (shown when `dev=1`, the developer `bg3rtx.ini`): Experimental lighting
reference (`rt_hit_reference`), Local light sampling exact-k (`ls_k`), Current cascade sun shadows
(`rt_sun_cascade_current`), and Emission at ray hits "All hits" (`rt_hit_emission=2`).
**No entry (automatic):** `fg_letterbox=1` (frame generation in a swapchain larger than the game's
output; equal sizes are unchanged), the hit-material buffer reuse, unlocked captures and lean hooks
(default 1, keys kept for A/B), `rt_scene_wait=1`; the foliage rows come with Hidden-surface shading;
capture buffer sizes follow the scene.

- **Dependencies.** Every ray-traced effect needs "Ray-traced geometry", and reflections also
  need global illumination. A red marker shows that a dependency is off; the effect cannot
  work until it is on. **Save** lowers a choice whose required setting is off (the status line
  names it).
- **Files from older versions.** A `bg3rtx_user.ini` written before these levels loads as the
  highest level any of its keys selects (for example `rf_hit_shade=1` reads as Hidden-surface
  shading + Reflections). Saving writes that level's keys; the marker shows "restart required"
  when one of the added keys needs a game start.
- **Restart required.** An orange marker shows a change that applies at the next start.
- **While the game runs.** You can keep the window open, change settings and press **Save**:
  the game applies them within about a second.

## Files

| File | Written by | Purpose |
|---|---|---|
| `bg3rtx.ini` | BG3RTX (do not edit for normal use) | defaults for every setting |
| `bg3rtx_user.ini` | the launcher | your preset and changes; read after `bg3rtx.ini` |
| `bg3rtx_launcher.ini` | the launcher | the game folder |
| `bg3rtx.log` | the game | what BG3RTX did; `[cfg]` lines show the settings in effect |

`bg3rtx_user.ini` is a plain text file:

```ini
preset=high
# Changed from the preset
rt_reflections=1
rf_max_rough=0.8
```

The preset applies first, and every key under it overrides the preset. Keys the launcher does
not manage (tweaks from `bg3rtx.ini`, such as `rt_tmax=200`) can be added by hand; the
launcher keeps them when it saves. Delete the file to go back to the defaults.

## Troubleshooting

- **Nothing changes in the game.** Check that `bg3rtx.log` exists next to `bg3rtx.ini` after
  starting. If it does not, the game was not started through the launcher.
- **Noisy lighting.** Ray Reconstruction is off or not running: the game's upscaler is not set
  to DLSS, or the GPU is not an NVIDIA RTX. Use the Low or Medium preset, or set the game to
  DLSS.
- **Frame generation does nothing.** It needs an RTX 40 or newer GPU, Reflex on, and a game
  restart after switching it on.

## Command line (advanced)

Box light shadows (`ls_box`) stay off in every preset until the live box-light check passes
(`docs/P3_BOX_LIGHTS.md`).

```
BG3RTX.exe --preset ultra          write bg3rtx_user.ini with a preset (limited to your GPU)
BG3RTX.exe --launch                start the game with your saved settings, no window
BG3RTX.exe --launch --dry-run      show the program, arguments and environment it would use
BG3RTX.exe --detect                GPU capabilities and the suggested preset
BG3RTX.exe --dump-schema           the settings table as JSON
  --game <folder>  --clean (disable other overlays)  --out <file> (output to a file)  -- <game arguments>
```
