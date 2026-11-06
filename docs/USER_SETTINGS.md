# BG3RTX settings: user guide

Use `bin\BG3RTX.exe` to select the game folder, choose Low, Medium, High or Ultra,
and save your settings. Custom means you changed something from a preset. In the game's Graphics options,
select DLSS as the upscaler for Ray Reconstruction. Before Play, when Ray Reconstruction is On,
the launcher restores DLSS in `graphicSettings.lsx` and backs up that file as `.lsx.bg3rtx-bak`.
It keeps your existing DLSS quality mode, or selects Quality if none is stored.
Set `game_dlss_guard=0` in `bg3rtx_user.ini` to disable this repair.
Hardware and dependency checks can restrict the available choices. A suggested preset is not a GPU
or VRAM qualification. BG3RTX is tested only on an RTX 5090. See the release README for compatibility
and memory limits.

## Release controls

Lighting, Surfaces, Performance and DLSS 5 contain the player controls below.
Advanced contains the all-ray-tracing keyboard shortcut.
Help & support contains the game folder, compatibility, memory information, overlay advice and local
reports. Each card shows how a change applies: "Applies after saving", "May briefly pause" or
"Restart to apply". After saving, the footer counts the changes that wait for the next game start.

| Page | Setting | Choices | Apply |
|---|---|---|---|
| Lighting | Ray-tracing detail | Off / Solid surfaces / Include foliage | Restart |
| Lighting | DLSS Ray Reconstruction | Off / On | Live, may briefly pause; needs an RTX GPU and DLSS in game |
| Lighting | Sun shadows | Visible surfaces / Ray-traced / Include hidden roofs | Live |
| Lighting | Indirect lighting | Off / Visible surfaces / Ray-traced / Ray-traced high | Live |
| Lighting | Off-screen bounce light | Off / Near / Far (6 m) | On or off: restart; Near/Far: may briefly pause |
| Lighting | Local light shadows | Off / Fast / Full | Live; Fast needs Ray Reconstruction |
| Lighting | Interior light shadows | Off / On | Live; needs Local light shadows |
| Lighting | Hair shadows | Off / On | On: restart; Off: live |
| Lighting | Mod lighting strength | 50-100%, step 5% | Live; blends indirect light, local shadows and reflections |
| Surfaces | Reflections | Off / Glossy / Standard / Rough | Live |
| Surfaces | Water lighting | Off / Reflections / Reflections and shadows / Include refraction | On or higher level: restart; lower or Off: live |
| Surfaces | Depth of field and bloom smoothing | Off / On | Live; needs Ray Reconstruction |
| DLSS 5 | DLSS 5 Neural Rendering (experimental) | Off / On, runtime picker and status | Restart to activate; shortcut toggles live after activation |
| Performance | Frame Generation | Off / 2x / 3x / 4x | On or off: restart; 2x/3x/4x: live |
| Performance | NVIDIA Reflex | Off / On / On + Boost | Live; Frame Generation needs On or On + Boost |
| Performance | Rendered frame limit | Off / 30-240 fps, step 1 | Live; counts rendered frames, before Frame Generation |
| Experimental | Water surface reflections | Off / On | On: restart; Off: live; needs Water lighting |

Visible hair is the game's stock hair. With Hair shadows On, the game's hair and scalp cast
ray-traced shadows. The first start after turning it on compiles shaders and can pause or stutter.
No replacement hair is included in this release. The Experimental page shows only Water surface
reflections in this release; strand hair returns there with its post-release update.

Off-screen bounce light also carries light from glowing surfaces. Far extends sunlight bounce to 6 m.
Interior light shadows respect the game's shadow flags, including lights whose shadows were
intentionally disabled. Water levels include the lower levels.

## Presets and automatic behaviour

| Preset | Defaults before hardware/dependency fallback |
|---|---|
| Low | Sun shadows and Indirect lighting: Visible surfaces; DLSS Ray Reconstruction, Ray-tracing detail, Local light shadows, Reflections, Water lighting, Off-screen bounce light and Hair shadows: Off |
| Medium | DLSS Ray Reconstruction: On; Ray-tracing detail: Solid surfaces; Sun shadows and Indirect lighting: Ray-traced; Local light shadows: Fast; Reflections: Glossy |
| High | DLSS Ray Reconstruction: On; Ray-tracing detail: Include foliage; Sun shadows and Indirect lighting: Ray-traced; Local light shadows: Full; Reflections: Standard; automatic opacity micromaps and cutout masks |
| Ultra | DLSS Ray Reconstruction: On; Ray-tracing detail: Include foliage; Sun shadows: Include hidden roofs; Indirect lighting: Ray-traced high (two rays); Local light shadows: Full, with faster light sampling in busy interiors; Interior light shadows: On; Reflections: Rough; Water lighting: Include refraction; Off-screen bounce light: Far (6 m); Hair shadows and Depth of field and bloom smoothing: On; Rendered frame limit: 58 fps; automatic opacity micromaps and cutout masks |

All presets trace at full resolution. Reflex is On and supported Frame Generation defaults to 2x.
The rendered frame limit is Off except for Ultra's 58 fps. A limit is a ceiling, not a performance promise.
Opacity micromaps and offline cutout masks apply automatically only with High and Ultra and
Ray-tracing detail set to Include foliage. Hardware and the locally prepared masks must support
them; otherwise the standard path is used. Water camera correction follows Water lighting, and the
far indirect-light roof fix is always on. These automatic paths have no controls.

Release mode resets hidden developer overrides from older user INIs. Obsolete Far + reflections
maps to Far (6 m), Tilesets maps to Hair shadows On, Full + shadowed lights maps to Full, and the
old box mode maps to On. Unknown unrelated keys are preserved. Use the launcher to choose options;
old hidden settings do not enable unsupported release features.

## Compare in game

The **DLSS 5** page has its own On/Off control, runtime file picker and runtime status.
DLSS 5 is experimental, Off in every preset, and requires Ray Reconstruction. Enable it
and select your x64 `nvngx_dlssnr.dll` version 310.8 or 310.9 before starting the game.
Automatic discovery checks the game folder's `bin`; NVIDIA-signed candidates take precedence.
The launcher stages the selected file in its own `bin/sl` before Play, checks its signature and
SHA-256, and logs custom or unsigned runtimes that NVIDIA may reject. The runtime is not bundled.
Changing activation or the runtime needs a restart.

**Ctrl+Shift+F8** toggles DLSS 5 Neural Rendering off/on live when it was enabled at game start.
This switches the runtime bypass, without restarting or changing Ray Reconstruction.

Under **Keyboard shortcuts**, change the RT shortcut on **Advanced** and the NR shortcut on
**DLSS 5**. Select the capture field and press Ctrl, Shift and/or Alt with F1-F12, A-Z or 0-9.
A single key is also accepted. **None** disables a shortcut; **Reset to default** restores it.
Duplicate shortcuts and the reserved Ctrl+Shift+F10 are rejected. Save to apply.

By default, press **Ctrl+Shift+F9** in game to switch all ray-traced lighting off and on together.
DLSS and Frame Generation stay unchanged, so you compare lighting in the same view. Each switch is
logged in `bg3rtx.log`. This shortcut does not open a settings menu.

Turn on **Show experimental options** in the launcher to see the Experimental page. These options
are off in every preset. **Ctrl+Shift+F10** switches the last Experimental option you changed in the
launcher off and on, but only for options that apply without a restart. The only Experimental option
in this release, Water surface reflections, needs a restart, so Ctrl+Shift+F10 has no effect yet.

## Files and support

`bg3rtx.ini` contains defaults; `bg3rtx_user.ini` stores the selected preset and overrides.
`bg3rtx_launcher.ini` stores the game folder. The log records the effective configuration.
After a change that needs a restart, close the game and start it again through BG3RTX.

Start from the launcher or set the Steam launch option to
`"<BG3RTX folder>\bin\BG3RTX.exe" %command%`. **Choose game folder…** in Help & support selects
the folder that contains `bin\bg3.exe`. If the game does not start or Frame Generation does nothing,
turn off other overlays (Steam, Discord, GOG Galaxy) for one launch.

**Detailed logging** in Help & support is off by default in every preset; turn it on, restart the game and reproduce the problem before creating a bug report.

Use **Report a problem…** in Help & support, or run `Diagnostics.cmd`, for a local report ZIP.
Nothing is uploaded and no save is included by default. Review `CONTENTS.txt` and the ZIP before
sharing. See the [packaged README](release/README.md) for privacy and manual reporting.
