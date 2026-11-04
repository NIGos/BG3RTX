# BG3RTX settings: user guide

BG3RTX adds ray-traced lighting, DLSS Ray Reconstruction and DLSS Frame Generation to Baldur's
Gate 3 (Vulkan renderer). You set it up with `bin\BG3RTX.exe`, the settings window and launcher.

## Settings window and first run

The preset selector and Save, Discard, Play and Close stay visible while the section scrolls.
Overview contains game selection and hardware information. Graphics groups Lighting and Surfaces;
Performance explains frame generation, the rendered cap and Reflex. Advanced contains qualified
shading options. Experimental is closed by default: opening it also makes experimental choices
available in the existing cards. No option is duplicated. Developer is absent unless the merged
configuration explicitly has `dev=1`. Hidden custom values stay preserved without exposing their names.

Preset changes preview every changed card before replacing edits. Reset entire preset uses the
same preview. Hidden or hand-edited custom values are preserved. Discard returns to the last saved
settings and game selection. Closing with unsaved changes offers Save, Discard or Cancel.
Save previews dependency adjustments; you may enable required settings or lower dependent choices.
Nothing is silently promoted to a qualified preset.

First run has Welcome, game selection, compatibility review and a DLSS reminder. Continue setup
resumes from the last completed page after interruption. Existing settings are previewed, never
replaced automatically. You can finish without a game folder; Play stays disabled until a valid
Vulkan game folder is selected. The launcher never changes the game's graphics configuration.

The footer separates unsaved edits from saved settings. A restart warning survives Save and
launcher reopen during the same game process, using an atomic `bg3rtx_ui.ini` beside the mod
configuration. It clears for a different game process or when saved settings return to the process
baseline. There is no runtime acknowledgement: Saved never means proven Active. Play is disabled
when the game is running and never restarts it. A failed state write leaves edits available.
Long errors have a Details / report button; saved settings remain saved if launch fails.

Support offers Report a problem, View log, Open mod folder and overlay troubleshooting.
**Report a problem is an integration hook in this candidate**: the B2 service implements local
collection. If absent, the button clearly says nothing was collected. Restore is unavailable until
setup provides a verified backup. No automatic upload or save-file access is part of this launcher.

Tab and Shift+Tab traverse native controls; arrow keys operate tabs, choices and sliders. Focusing a
scrolled control brings it into view. The launcher uses B1's split-lantern emblem, private Lato font,
charcoal/parchment palette and system colours in high contrast. No font is installed system-wide.

## Starting the game

- **From the settings window:** pick your settings, then **Play**. The launcher
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
it back on, regardless of the last saved Experimental choice: sun and local shadows, GI,
reflections, water, RT hair, emissive light, and the radiance cache. You then
see the game's own lighting in the same frame and view. DLSS and frame generation are not
touched, so the comparison is lighting only. Each press is logged in `bg3rtx.log` as `[ab]`.
Press **Ctrl+Shift+F10** to flip the last saved eligible Experimental choice instead. Without
a saved eligible choice, F10 does nothing. Its changed keys are logged as `A/B: <key>=<value>`.
`ab_hotkey=0` in `bg3rtx_user.ini` turns both hotkeys off. The queue's `ab_off` master toggle is unchanged.

## Presets

The window opens on your GPU and suggests a preset:

| Preset | Meant for | What it does |
|---|---|---|
| Low | RTX 2060-3060, 4060; AMD/Intel with ray tracing | No ray-traced scene, no Ray Reconstruction. Screen-space GI and contact shadows |
| Medium | RTX 3070/3080, 4060 Ti/4070 | Ray-traced scene without alpha-tested objects, RT GI, fast local-light shadows, glossy reflections |
| High | RTX 4080/4090, 5080 | Ray-traced lighting and full geometry; hardware suitability is estimated |
| Ultra (RTX 5090) | RTX 5090 | High plus Ray-traced High GI, sun shadows with hidden roofs, rough reflections, RT water + Refraction, Off-screen bounce light Far (6 m), frame cap 58 (116 shown with frame generation 2x) |

The presets below RTX 5090 are estimates and have not been measured on those GPUs yet. Settings
your GPU cannot run are marked "(not supported)" and the presets skip them. For example, an
RTX 30 card gets no frame generation, and AMD/Intel get no Ray Reconstruction.

Changing any setting makes the preset "Custom". **Reset entire preset** brings the preset back.

## Settings

Each feature card describes its visual effect. **Details and requirements** gives cost provenance
and apply requirements. GPU and VRAM figures marked Estimate are reference costs, not a prediction
for your GPU. Unknown means no qualified measurement is available. The card says how a change
reaches a running game:

| Setting | Choices | Applies |
|---|---|---|
| Ray Reconstruction | Off / On | live, brief hitch |
| Ray-traced geometry | Off / Opaque only / Full | Off/Opaque live; **Opaque <-> Full needs a restart** |
| Sun shadows | Screen-space / Ray-traced / Ray-traced + hidden roofs (Ultra) | live |
| Global illumination | Off / Screen-space / Ray-traced / Ray-traced High | live |
| Local light shadows | Off / Fast / Full | live |
| Reflections | Off / Glossy / Standard / Rough | live |
| Frame generation | Off / 2x / 3x / 4x | 2x/3x/4x live; **Off <-> on needs a restart** |
| RT lighting intensity | 50-100 % | live |
| Frame cap (rendered fps) | Off / 30-240 | live |
| NVIDIA Reflex (Advanced) | Off / On / On + Boost | live |

**Advanced levels (on in Ultra, off in Low, Medium and High).** Each entry is one choice with
levels; a higher level includes the lower ones and writes all of their ini keys. Ultra uses the
level in brackets.

| Setting | Choices | Needs | Applies |
|---|---|---|---|
| RT water (Ultra: + Refraction) | Off / Reflections / + Shadows / + Refraction | Ray-traced geometry | **raising a level: restart**; lowering live |
| Off-screen bounce light (Ultra: Far) | Off / Near / Far (6 m) / Far + reflections | Full geometry and RT GI; top level also needs Reflections | **restart to first enable or disable capture**; changes between enabled levels may hitch |

Near includes hidden-surface materials, bounced light and emission from glowing hidden surfaces.
Far extends sunlight bounce to 6 m. Far + reflections is experimental and in no preset.
Reference costs on RTX 5090 at 4K-class output: Near ~+0.6 ms, Far ~+4 ms, top level ~+6 ms.
Sun shadows with hidden roofs uses cascade-copy casters for geometry the camera hides, including
roofs and canopies (+0.3 to +1.3 ms). Box light shadows were reverted on 2026-11-03:
they may hide artist-unshadowed lights in Grymforge; investigation pending. They stay off in every
preset, including Ultra, and remain visible in Experimental. Measured reference cost when enabled:
+0.32 ms at the Adamantine Forge (S6), +0.20 ms at the inn (S7).

**Experimental (being tested, off in every preset).**

| Setting | Choices | Needs | Applies |
|---|---|---|---|
| Box light shadows | Off / Shadowed / All | Ray-traced geometry and Local light shadows | live |
| Native coverage | Off / Hair + scalp / + Tilesets | Full geometry | **restart** |
| Offline cutout masks | Off / On | Full geometry; locally prepared masks | live |

What each level writes: Off-screen bounce light Near sets `rt_hit_material`,
`rt_hit_material_instanced`, `rt_hit_material_wind_rgb`, `gi_hit_direct`, `gi_hit_sun`,
`rt_hit_emission` and `rt_emissive` to 1. Far adds `gi_far=1`, `gi_far_m=6`;
Far + reflections also enables `rf_hit_shade`, `rf_hit_gi`. Lower levels reset the higher levels.
RT water Reflections = `rt_water_geometry`,
`rt_water_reflections`, `rt_water_compose`; + Shadows adds `rt_water_shadows`, `rt_water_ls_shadows`,
`rt_water_game_shadow`; + Refraction adds `rt_water_refraction`. Native coverage Hair + scalp =
`rt_native_hair`, `rt_native_scalp`; + Tilesets adds `rt_native_tileset`.

**Developer only** (shown when `dev=1`, the developer `bg3rtx.ini`): Experimental lighting
reference (`rt_hit_reference`), Local light sampling exact-k (`ls_k`), Current cascade sun shadows
(`rt_sun_cascade_current`), Physical hair ray origins (`rt_hair_physical_origin`),
Opacity micromaps (`rt_omm`), Preserve cutout capture on overflow (`rt_alpha_spill`),
and Local light shadows "Full + shadowed lights". The three capture/hair options remain hidden
until qualified. Hand-edited all-hit emission (`rt_hit_emission=2`) is preserved as a custom value.
**No entry (automatic):** `fg_letterbox=1` (frame generation in a swapchain larger than the game's
output; equal sizes are unchanged), the hit-material buffer reuse, unlocked captures and lean hooks
(default 1, keys kept for A/B), `rt_scene_wait=1`; the foliage rows and the alpha-tested static mesh
rows (`rt_hit_material_pool_alpha=1`) come with Off-screen bounce light; capture buffer sizes follow the scene.
`bg3rtx.log` is capped at 64 MB: the earlier part moves to `bg3rtx.prev.log`, which also keeps the previous
session's log.

- **Dependencies.** Every ray-traced effect needs "Ray-traced geometry", and reflections also
  need global illumination. A red marker shows that a dependency is off; the effect cannot
  work until it is on. **Save** lowers a choice whose required setting is off (the status line
  names it).
- **Files from older versions.** A `bg3rtx_user.ini` written before these levels loads as the
  highest level any of its keys selects (for example `rf_hit_shade=1` reads as Off-screen bounce light Far + reflections). Saving writes that level's keys; the marker shows "restart required"
  when one of the added keys needs a game start. Old materials-only files map to Near;
  old emission and far-field keys map to Near and Far respectively. Saving completes the selected
  level. All-hit emission stays custom and survives an unrelated save.
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

Box light shadows (`ls_box`) stay off in every preset, including Ultra; Experimental retains manual choices.

```
BG3RTX.exe --preset ultra          write bg3rtx_user.ini with a preset (limited to your GPU)
BG3RTX.exe --launch                start the game with your saved settings, no window
BG3RTX.exe --launch --dry-run      show the program, arguments and environment it would use
BG3RTX.exe --detect                GPU capabilities and the suggested preset
BG3RTX.exe --dump-schema           the settings table as JSON
  --game <folder>  --clean (disable other overlays)  --out <file> (output to a file)  -- <game arguments>
```
