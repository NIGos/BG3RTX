# BG3RTX settings: user guide

Use `bin\BG3RTX.exe` to select the game folder and choose Low, Medium, High or Ultra.
**A setting applies the moment you click it**: there is no Save button. Each change is written to
`bg3rtx_user.ini` at once and the running game picks it up. The strip at the bottom of the window names the
change, anything that changed with it, and offers **Undo**. Custom means you changed something from a preset;
"Reset to" next to the preset brings the preset back. In the game's Graphics options,
select DLSS as the upscaler for Ray Reconstruction. Before Play, when Ray Reconstruction is On,
the launcher restores DLSS in `graphicSettings.lsx` and backs up that file as `.lsx.bg3rtx-bak`.
It keeps your existing DLSS quality mode, or selects Quality if none is stored.
Set `game_dlss_guard=0` in `bg3rtx_user.ini` to disable this repair.
Hardware and dependency checks can restrict the available choices. A suggested preset is not a GPU
or VRAM qualification. BG3RTX is tested only on an RTX 5090. See the release README for compatibility
and memory limits.

## How changes apply

- **At once.** Most settings change the running game as you click. No badge is shown for these.
- **After a game restart.** A setting with an amber "Restart" badge (or "Restart to turn on") is written at once
  but takes effect at the next game start. While the game runs, a line under the header lists what is
  waiting ("Restart the game to apply: Water lighting, Hair shadows") and those rows show "Restart pending".
- **Dependencies.** Turning something on also turns on what it needs. Turning something off also lowers what
  needs it. Both happen at the click and are listed next to the control with Undo. A setting lowered this way
  comes back when you turn its prerequisite on again in the same launcher session. A sub-setting whose parent
  is off (RTX Hair detail and motion stability) is dimmed and keeps its value.
- **If the file cannot be written** (for example, it is open in another program) nothing changes and a red
  line offers Retry.
- **Play** starts the game with the settings as they are.

## Release controls

Select a row to see what it does, its GPU cost, when it takes effect, what it needs and what each preset sets.

| Page | Group | Setting | Choices | Takes effect |
|---|---|---|---|---|
| Lighting | Ray tracing | Ray-tracing detail | Off / Solid surfaces / Include foliage | Restart |
| Lighting | Ray tracing | DLSS Ray Reconstruction | Off / On | At once, may briefly pause; needs an RTX GPU and DLSS in game |
| Lighting | Ray tracing | Mod lighting strength | 50-100%, step 5% | At once; blends indirect light, local shadows and reflections |
| Lighting | Shadows | Sun shadows | Visible surfaces / Ray-traced / Include hidden roofs | At once |
| Lighting | Shadows | Local light shadows | Off / Fast / Full | At once; Fast needs Ray Reconstruction |
| Lighting | Shadows | Interior light shadows | Off / On | At once; needs Local light shadows |
| Lighting | Bounced light | Indirect lighting | Off / Visible surfaces / Ray-traced / Ray-traced high | At once |
| Lighting | Bounced light | Off-screen bounce light | Off / Near / Far (6 m) | On or off: restart; Near/Far: may briefly pause |
| Lighting | Hair | Hair shadows | Off / On | On: restart; Off: at once |
| Lighting | Hair | RTX Hair (Beta) | Off / On | At once, also with its key |
| Lighting | Hair | RTX Hair detail at distance | Full / 1/4 (Standard) / 1/8 (Sharper) | At once |
| Lighting | Hair | RTX Hair motion stability | Off / On | On: restart; Off: at once |
| Surfaces | Reflections | Reflections | Off / Glossy / Standard / Rough | At once |
| Surfaces | Water | Water lighting | Off / Reflections / Reflections and shadows / Include refraction | On or higher level: restart; lower or Off: at once |
| Surfaces | Post effects | Depth of field and bloom smoothing | Off / On | At once; needs Ray Reconstruction |
| Performance | Frame rate | Frame Generation | Off / 2x / 3x / 4x | On or off: restart; 2x/3x/4x: at once |
| Performance | Frame rate | NVIDIA Reflex | Off / On / On + Boost | At once; Frame Generation needs On or On + Boost |
| Performance | Frame rate | Rendered frame limit | Off / 30-240 fps, step 1 | At once (when you release the slider); counts rendered frames; On starts at 58 fps |
| Performance | Frame rate | Low-latency scene | Off / On | On: restart; Off: at once; On in High and Ultra. Removes most of the scene wait (3.5 to 0.3 ms at Water Queens House, 8.5 to 2.9 ms at the inn, Ultra); frame time unchanged on Ultra in both (GPU-bound), about 0.5 ms saved on High at Water Queens House; 0 fallbacks, same brightness. Not yet measured in CPU-bound scenes |
| Performance | Tracing speed | Faster bounce light tracing | Off / On | At once; same image, on in every preset |
| Performance | Tracing speed | Bounce trace reordering | Off / By hit kind / By hit kind and object | At once, may briefly pause |
| DLSS 5 | DLSS 5 | DLSS 5 Neural Rendering (Experimental) | Off / On, runtime file | Restart to activate; its key toggles live after activation |
| Experimental | Frame Generation | Frame generation pacing test | Standard / Deferred | At once |
| Experimental | Frame Generation | Multi-frame presentation test | Standard / Driver metering | Restart |
| Experimental | Water | Water surface reflections | Off / On | On: restart; Off: at once; needs Water lighting |
| Experimental | RTX Hair | Strand hair edge resolve | Off / Strands unresolved / Analytic coverage | At once; needs RTX Hair |
| Help & support | Problem reports | Detailed logging | Off / On | Restart |

Tracing speed is collapsed by default: both options are on in every preset and do not change the image.
The Experimental page has the switch **Show experimental options**; its options are off in every preset and
hidden until you turn the switch on. Turning it off sets them all back to Off (Undo restores them).
**Home** shows what needs attention, the preparation of shaders and RTX Hair styles, the preset and the
in-game keys. **Hotkeys** changes the in-game keys. **Help & support** has the game folder, the graphics check,
reports, tips and the version.

Visible hair is the game's stock hair. With Hair shadows On, the game's hair and scalp cast
ray-traced shadows. The first start after turning it on compiles shaders and can pause or stutter.
No replacement hair files are included. **Lighting > RTX Hair** (beta, Off in every preset; turns on and off
while playing, also with Ctrl+Shift+F7) shows hair as ray-traced strands: a style gets strands after the game has
shown it once and the launcher has converted it (Home > Preparation). A style that is not converted yet stays as the
game's hair, hair on distant characters can look softer, and a head that comes into view shows the game's hair for
a few frames (about 3) first.

Off-screen bounce light also carries light from glowing surfaces. Far extends sunlight bounce to 6 m.
Interior light shadows respect the game's shadow flags, including lights whose shadows were
intentionally disabled. Water levels include the lower levels.

## Presets and automatic behaviour

| Preset | Defaults before hardware/dependency fallback |
|---|---|
| Low | Sun shadows and Indirect lighting: Visible surfaces; DLSS Ray Reconstruction, Ray-tracing detail, Local light shadows, Reflections, Water lighting, Off-screen bounce light and Hair shadows: Off |
| Medium | DLSS Ray Reconstruction: On; Ray-tracing detail: Solid surfaces; Sun shadows and Indirect lighting: Ray-traced; Local light shadows: Fast; Reflections: Glossy |
| High | DLSS Ray Reconstruction: On; Ray-tracing detail: Include foliage; Sun shadows and Indirect lighting: Ray-traced; Local light shadows: Full; Reflections: Standard; Low-latency scene and RTX Hair motion stability: On; automatic opacity micromaps and cutout masks |
| Ultra | DLSS Ray Reconstruction: On; Ray-tracing detail: Include foliage; Sun shadows: Include hidden roofs; Indirect lighting: Ray-traced high (two rays); Local light shadows: Full, with faster light sampling in busy interiors; Interior light shadows: On; Reflections: Rough; Water lighting: Include refraction; Off-screen bounce light: Far (6 m); Hair shadows and Depth of field and bloom smoothing: On; Rendered frame limit: 58 fps; Low-latency scene and RTX Hair motion stability: On; automatic opacity micromaps and cutout masks |

All presets trace at full resolution. Reflex is On and supported Frame Generation defaults to 2x.
The rendered frame limit is Off except for Ultra's 58 fps. A limit is a ceiling, not a performance promise.
Opacity micromaps and offline cutout masks apply automatically only with High and Ultra and
Ray-tracing detail set to Include foliage. Hardware and the locally prepared masks must support
them; otherwise the standard path is used. Water camera correction follows Water lighting, and the
far indirect-light roof fix is always on. Video memory for ray tracing is sized by what the scene needs and
shrinks again when the need falls (always on). These automatic paths have no controls.
RTX Hair is Off in every preset; RTX Hair detail at distance is Full in every preset. RTX Hair motion
stability only acts when RTX Hair is on.

Release mode resets hidden developer overrides from older user INIs. Obsolete Far + reflections
maps to Far (6 m), Tilesets maps to Hair shadows On, Full + shadowed lights maps to Full, and the
old box mode maps to On. Unknown unrelated keys are preserved. Use the launcher to choose options;
old hidden settings do not enable unsupported release features.

## Compare in game

The **DLSS 5** page has its own On/Off control, runtime file picker and runtime status.
DLSS 5 is experimental (the row carries the badge), Off in every preset, and requires Ray Reconstruction. Enable it
and select your x64 `nvngx_dlssnr.dll` version 310.8 or 310.9 before starting the game.
Automatic discovery checks the game folder's `bin`; NVIDIA-signed candidates take precedence.
The launcher stages the selected file in its own `bin/sl` before Play, checks its signature and
SHA-256, and logs custom or unsigned runtimes that NVIDIA may reject. The runtime is not bundled.
Changing activation or the runtime needs a restart.

**Ctrl+Shift+F8** toggles DLSS 5 Neural Rendering off/on live when it was enabled at game start.
This switches the runtime bypass, without restarting or changing Ray Reconstruction.

On **Hotkeys**, select a field and press Ctrl, Shift and/or Alt with F1-F12, A-Z or 0-9.
A single key is also accepted. **Clear** disables a key; **Reset to** restores its default.
Duplicate keys and the reserved Ctrl+Shift+F10 are rejected. A key change applies at once.

By default, press **Ctrl+Shift+F9** in game to switch all ray-traced lighting off and on together.
DLSS and Frame Generation stay unchanged, so you compare lighting in the same view. Each switch is
logged in `bg3rtx.log`. This shortcut does not open a settings menu.

**Ctrl+Shift+F10** switches the last Experimental option you changed in the launcher off and on, but only
for options that apply without a restart.

## Files and support

`bg3rtx.ini` contains defaults; `bg3rtx_user.ini` stores the selected preset and overrides.
`bg3rtx_launcher.ini` stores the game folder. The log records the effective configuration.
After a change that needs a restart, close the game and start it again through BG3RTX.
`bg3rtx_user.ini` is rewritten on every change in the launcher; edit it by hand only while the launcher is closed.

Start from the launcher or set the Steam launch option to
`"<BG3RTX folder>\bin\BG3RTX.exe" %command%`. **Choose folder…** in Help & support (or on Home when the game was not found) selects
the folder that contains `bin\bg3.exe`. If the game does not start or Frame Generation does nothing,
turn off other overlays (Steam, Discord, GOG Galaxy) for one launch.

**Detailed logging** in Help & support is off by default in every preset; turn it on, restart the game and reproduce the problem before creating a bug report.

Use **Create a report…** on Home or in Help & support, or run
`Diagnostics.cmd`, for a local report ZIP. Nothing is uploaded and no save is included by default.
Tick **Include my latest save** in the report dialog (Diagnostics.cmd asks; answer Y) to add your
newest save and its thumbnail under `saves/<save folder>/`: it helps reproduce the problem but contains
your character and progress. The save is copied read-only; over 200 MB it is left out and named in `CONTENTS.txt`. Review `CONTENTS.txt` and the ZIP before
sharing. See the [packaged README](release/README.md) for privacy and manual reporting.
