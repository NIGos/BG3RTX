# BG3RTX settings: user guide

Use `bin\BG3RTX.exe` to select the Vulkan game folder, choose Low, Medium, High or Ultra,
and save your settings. Custom indicates changes from a preset. In the game's Graphics options,
select DLSS for Ray Reconstruction. The launcher does not edit the game's graphics settings.
Hardware and dependency checks can restrict available choices; a suggested preset is not a GPU
or VRAM qualification. See the release README for compatibility and memory limits.

## Release controls

Lighting, Surfaces and Performance contain the player controls below. Advanced is developer-only.
Help & support contains compatibility, memory information, overlay troubleshooting and local reports. Restart badges distinguish saved
settings from changes that need the next game start. Saving does not prove a setting is active.

| Page | Setting | Choices | Apply |
|---|---|---|---|
| Lighting | DLSS Ray Reconstruction | Off / On | Live, brief hitch; needs RTX and in-game DLSS |
| Lighting | Ray-tracing detail | Off / Solid surfaces / Include foliage | Restart |
| Lighting | Sun shadows | Visible surfaces / Ray-traced / Include hidden roofs | Live |
| Lighting | Indirect lighting | Off / Visible surfaces / Ray-traced / Ray-traced high | Live |
| Lighting | Local light shadows | Off / Fast / Full | Live; Fast needs Ray Reconstruction |
| Lighting | Mod lighting strength | 50-100%, step 5% | Live; blends indirect light, local shadows and reflections |
| Lighting | Off-screen bounce light | Off / Near / Far (6 m) | First enable or disable capture: restart |
| Lighting | Interior light shadows | Off / On | Live |
| Surfaces | Depth of field and bloom smoothing | Off / On | Live |
| Lighting | Hair shadows | Off / On | Enable: restart; Off: live |
| Surfaces | Reflections | Off / Glossy / Standard / Rough | Live |
| Surfaces | Water lighting | Off / Reflections / Reflections and shadows / Include refraction | Enable/raise level: restart; Off: live |
| Performance | Frame Generation | Off / 2x / 3x / 4x | Off/on: restart; multiplier changes: live |
| Performance | Rendered frame limit | Off / 30-240 fps, step 1 | Live; counts rendered frames, before frame generation |
| Performance | NVIDIA Reflex | Off / On / On + Boost | Live; frame generation needs On or Boost |

Visible hair remains the game's stock hair. With Hair shadows On, captured stock hair
and scalp take part in ray queries. First enabling capture can
compile shaders and pause or stutter. No replacement hair is included in this release.

Near bounce captures hidden materials, indirect sunlight/local light and emission (mode 1).
Far extends sunlight bounce to 6 m. Interior light shadows On respects the game's shadow flags, including lights
whose shadows were intentionally disabled. Water levels include the lower levels.

## Presets and automatic behaviour

| Preset | Defaults before hardware/dependency fallback |
|---|---|
| Low | Sun shadows and Indirect lighting: Visible surfaces; DLSS Ray Reconstruction, Ray-tracing detail, Local light shadows, Reflections, Water lighting, Off-screen bounce light and Hair shadows: Off |
| Medium | DLSS Ray Reconstruction: On; Ray-tracing detail: Solid surfaces; Sun shadows and Indirect lighting: Ray-traced; Local light shadows: Fast; Reflections: Glossy |
| High | DLSS Ray Reconstruction: On; Ray-tracing detail: Include foliage; Sun shadows and Indirect lighting: Ray-traced; Local light shadows: Full; Reflections: Standard; automatic OMM and offline masks |
| Ultra | DLSS Ray Reconstruction: On; Ray-tracing detail: Include foliage; Sun shadows: Include hidden roofs; Indirect lighting: Ray-traced high (two rays); Local light shadows: Full; Interior light shadows: On; Reflections: Rough; Water lighting: Include refraction; Off-screen bounce light: Far (6 m); Hair shadows and Depth of field and bloom smoothing: On; automatic OMM and offline masks |

All presets trace at Full resolution. Reflex is On and supported frame generation defaults to 2x.
The rendered cap is Off except Ultra's 58 fps. A cap is a limit, not a performance promise.
Opacity micromaps (OMM), dynamic micromaps, fine cutout LOD and offline masks apply automatically only
with High and Ultra and Ray-tracing detail set to Include foliage. Hardware and local data must
support them. Otherwise native fallback applies. Stock-hair pool trim follows stock-hair capture. Water camera correction follows
water. These automatic paths do not add controls.

Release mode resets hidden candidate overrides from older user INIs. Obsolete Far + reflections
maps to Far (6 m), Tilesets maps to Hair shadows On, Full + shadowed lights maps to Full, and legacy
box mode maps to On. Unknown unrelated keys are preserved. Use the launcher to select meaningful
options; old hidden settings do not enable unsupported release features.

## Compare in game

Press **Ctrl+Shift+F9** for the master RT toggle: all ray-traced lighting switches off/on together.
DLSS and frame generation stay unchanged. This compares lighting in the same view. Changes are
logged as `[ab]` in `bg3rtx.log`. This shortcut does not open a settings menu.

Enable **Show experimental options** in the launcher to see the Experimental page. These choices
are off in presets. **F10** flips the last Experimental choice changed in the launcher.
It does not toggle all experiments or replace the master RT toggle.

## Files and support

`bg3rtx.ini` contains defaults; `bg3rtx_user.ini` stores the selected preset and overrides.
`bg3rtx_launcher.ini` stores the game folder. Logs and `[cfg]` lines record effective configuration.
After a restart-required change, close and relaunch the game through BG3RTX.

Start from the launcher or set Steam launch options to
`"<BG3RTX folder>\bin\BG3RTX.exe" %command%`. Browse selects the folder containing `bin\bg3.exe`.
Disable other Vulkan overlays in Help & support if startup or frame generation fails.

Run `Diagnostics.cmd` for a local report ZIP. It uploads nothing and includes no save by default.
Review `CONTENTS.txt` and the ZIP before sharing. See the [packaged README](release/README.md) for privacy and manual reporting.
