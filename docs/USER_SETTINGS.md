# BG3RTX settings: user guide

Use `bin\BG3RTX.exe` to select the Vulkan game folder, choose Low, Medium, High or Ultra,
and save your settings. Custom indicates changes from a preset. In the game's Graphics options,
select DLSS for Ray Reconstruction. The launcher does not edit the game's graphics settings.
Hardware and dependency checks can restrict available choices; a suggested preset is not a GPU
or VRAM qualification. See the release README for compatibility and memory limits.

## Release controls

Lighting, Surfaces and Performance contain the controls below. Help & support contains compatibility,
memory information, overlay troubleshooting and local reports. Restart badges distinguish saved
settings from changes that need the next game start. Saving does not prove a setting is active.

| Page | Setting | Choices | Apply |
|---|---|---|---|
| Lighting | Ray Reconstruction | Off / On | Live, brief hitch; needs RTX and in-game DLSS |
| Lighting | Ray-traced geometry | Off / Opaque / Full | Restart |
| Lighting | Sun shadows | Screen-space / Ray-traced / Ray-traced + hidden roofs | Live |
| Lighting | Global illumination | Off / Screen-space / Ray-traced / Ray-traced High | Live |
| Lighting | Local light shadows | Off / Fast / Full | Live; Fast needs Ray Reconstruction |
| Lighting | RT lighting strength | 50-100%, step 5% | Live; blends indirect light, local shadows and reflections |
| Lighting | Off-screen bounce light | Off / Near / Far (6 m) | First enable or disable capture: restart |
| Lighting | Box light shadows | Off / On | Live |
| Lighting | Temporal depth of field and bloom | Off / On | Live |
| Lighting | Hair shadows and occlusion | Off / Stock hair + scalp | Enable: restart; Off: live |
| Surfaces | Reflections | Off / Glossy / Standard / Rough | Live |
| Surfaces | Water lighting | Off / Reflections / + Shadows / + Refraction | Enable/raise level: restart; Off: live |
| Performance | Frame generation | Off / 2x / 3x / 4x | Off/on: restart; multiplier changes: live |
| Performance | Rendered frame limit | Off / 30-240 fps, step 1 | Live; counts rendered frames, before frame generation |
| Performance | NVIDIA Reflex | Off / On / On + Boost | Live; frame generation needs On or Boost |

Visible hair remains the game's stock hair. Hair shadows and occlusion adds captured stock hair
and scalp to ray queries; it does not replace the visible hairstyle. First enabling capture can
compile shaders and pause or stutter. No replacement hair is included in this release.

Near bounce captures hidden materials, indirect sunlight/local light and emission (mode 1).
Far extends sunlight bounce to 6 m. Box On respects the game's shadow flags, including lights
whose shadows were intentionally disabled. Water levels include the lower levels.

## Presets and automatic behaviour

| Preset | Defaults before hardware/dependency fallback |
|---|---|
| Low | Screen-space sun/GI; RR, RT geometry, local shadows, reflections, water, bounce and hair capture Off |
| Medium | RR, Opaque geometry, RT sun/GI, Fast local shadows, Glossy reflections |
| High | RR, Full geometry, RT sun/GI, Full local shadows, Standard reflections |
| Ultra | Full geometry, hidden roofs, two-ray GI, Full local/box shadows, Rough reflections, water + Refraction, Far bounce/emission, stock hair + scalp and temporal post On |

All presets trace at Full resolution. Reflex is On and supported frame generation defaults to 2x.
The rendered cap is Off except Ultra's 58 fps. A cap is a limit, not a performance promise.
Opacity micromaps, dynamic micromaps, fine cutout LOD and offline masks apply automatically only
with Ultra and Full geometry, when hardware and local data support them. Otherwise native
fallback applies. Stock-hair pool trim follows stock-hair capture. Water camera correction follows
water. These automatic paths do not add controls.

Release mode resets hidden candidate overrides from older user INIs. Obsolete Far + reflections
maps to Far, Tilesets maps to Stock hair + scalp, Full + shadowed lights maps to Full, and legacy
box mode maps to On. Unknown unrelated keys are preserved. Use the launcher to select meaningful
options; old hidden settings do not enable unsupported release features.

## Compare in game

Press **Ctrl+Shift+F9** for the master RT toggle: all ray-traced lighting switches off/on together.
DLSS and frame generation stay unchanged. This compares lighting in the same view. Changes are
logged as `[ab]` in `bg3rtx.log`.

## Files and support

`bg3rtx.ini` contains defaults; `bg3rtx_user.ini` stores the selected preset and overrides.
`bg3rtx_launcher.ini` stores the game folder. Logs and `[cfg]` lines record effective configuration.
After a restart-required change, close and relaunch the game through BG3RTX.

Start from the launcher or set Steam launch options to
`"<BG3RTX folder>\bin\BG3RTX.exe" %command%`. Browse selects the folder containing `bin\bg3.exe`.
Disable other Vulkan overlays in Help & support if startup or frame generation fails.

Run `Diagnostics.cmd` for a local report ZIP. It uploads nothing and includes no save by default.
Review `CONTENTS.txt` and the ZIP before sharing. See README for privacy and manual reporting.
