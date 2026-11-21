# BG3RTX

Ray-traced lighting for Baldur's Gate 3 (Vulkan renderer, `bg3.exe`): ray-traced sun and local-light shadows,
indirect lighting (GI), reflections and water, with NVIDIA DLSS Ray Reconstruction, DLSS Frame Generation and Reflex.
It runs as a Vulkan layer that its launcher, `BG3RTX.exe`, enables for the game process only. Nothing in the game
folder, the registry or your saves is changed.

Current release: **0.9.6-beta** (public beta). What is new: [release notes](docs/release/RELEASE_NOTES_0.9.6.md).
The full player guide is the packaged README, [docs/release/README.md](docs/release/README.md); every launcher
control is in [the settings guide](docs/USER_SETTINGS.md).

## Players

### Features

- **Sun shadows**: ray-traced, with foliage on High and Ultra and hidden roofs on Ultra.
- **Local light shadows**: torches, candles, fires and lamps cast ray-traced shadows.
- **Interior light shadows** (Ultra): they respect the game's own shadow flags.
- **Indirect lighting**: light bounces between surfaces; Ultra adds off-screen bounce light up to 6 m.
- **Reflections**: glossy to rough surfaces; on Ultra also water reflections, shadows and refraction.
- **Hair shadows** (Ultra): the game's own hair and scalp cast ray-traced shadows. The hair you see is unchanged.
- **RTX Hair** (Beta, off in every preset): hair drawn as ray-traced strands. See below.
- **DLSS Ray Reconstruction**, **DLSS Frame Generation** (2x, 3x or 4x, depending on the card) and **NVIDIA Reflex**.
- **Depth of field and bloom smoothing** on Ultra.

### Requirements

- Windows 10 or 11, 64-bit.
- An NVIDIA GeForce RTX card. **Tested only on RTX 5090.** Other RTX cards should work but are untested.
  Frame Generation 2x needs RTX 40 or newer; 3x and 4x need RTX 50. AMD and Intel cards are not supported.
- An NVIDIA Game Ready driver. The test driver is 621.01: a reference, not a minimum.
- Baldur's Gate 3 on Steam with the **Vulkan** renderer (`bin\bg3.exe`), tested with game version 4.1.1.7631656.
  GOG copies should work through **Choose folder…** in the launcher but are untested.
- **DLSS** selected as the upscaler in the game's Graphics options. Ray Reconstruction does not run with FSR,
  XeSS or TAA.
- Microsoft WebView2 is optional. Without it the launcher opens its basic window.

### Install

Download BG3RTX from the **Files** tab of the BG3RTX page on Nexus Mods. There are no downloads on GitHub: this
repository is the source code. Two downloads hold the same files. Close the game first.

- **Setup.exe** (`BG3RTX-0.9.6-beta-Setup.exe`): installs for your Windows user only, without administrator rights,
  to `%LOCALAPPDATA%\Programs\BG3RTX`, and adds a Start menu entry. It is not signed, so Windows SmartScreen may warn.
- **ZIP** (`BG3RTX-0.9.6-beta.zip`): unzip it anywhere, not inside the game folder. Run `bin\BG3RTX.exe` from there
  (portable), or double-click **Install.cmd** to copy it to `%LOCALAPPDATA%\Programs\BG3RTX` with a Start menu entry.

Then start **BG3RTX**. It finds Steam installs by itself; otherwise use **Choose folder…** and pick the folder that
contains `bin\bg3.exe`. Pick a preset and press **Play**. A setting applies the moment you click it: there is no
Save button. To start from Steam instead, set the launch option `"<BG3RTX folder>\bin\BG3RTX.exe" %command%`.

**Update:** close the game and the launcher, then run the new Setup.exe or the new Install.cmd into the same folder.
Your settings (`bg3rtx_user.ini`) are kept.

**Uninstall:** Setup version: Windows Settings > Apps > BG3RTX > Uninstall. ZIP installed with Install.cmd: run
**Uninstall.cmd** in the install folder. Portable copy: delete its folder. If you set the Steam launch option,
clear it by hand. Uninstalling never touches the game or your saves.

### Presets

| Preset | What it does |
|---|---|
| Low | Screen-space indirect light and contact shadows. No ray-traced scene, no Ray Reconstruction. |
| Medium | Ray-traced sun shadows and indirect light for solid surfaces, Fast local light shadows, Glossy reflections, Ray Reconstruction. |
| High | Adds foliage to the ray-traced scene, Full local light shadows, Standard reflections, Low-latency scene. |
| Ultra | Two-ray indirect light, hidden roofs, interior light shadows, Rough reflections, water with refraction, Far (6 m) off-screen bounce light, hair shadows, depth of field and bloom smoothing. Rendered frame limit 58 fps. |

All presets use Reflex On and, where the card supports it, Frame Generation 2x. Costs were measured on one RTX 5090
and are not a frame-rate promise. There are no tested video memory recommendations for other cards: start with Low
and raise settings step by step.

### RTX Hair (Beta)

RTX Hair draws hair as individual ray-traced strands instead of the game's flat hair cards. It is off in every
preset. Turn it on in the launcher (**Lighting > RTX Hair**) or in game with **Ctrl+Shift+F7**; it switches while
you play. A hair style gets strands after the game has shown it once and the launcher has converted it (Home,
Preparation). Styles that are not converted keep the game's hair. Its cost depends on how much hair is on screen
and is not measured across scenes yet.

### Hotkeys

| Key (default) | In game |
|---|---|
| Ctrl+Shift+F9 | Switches all ray-traced lighting off and on, to compare the same view. DLSS and Frame Generation stay as they are. |
| Ctrl+Shift+F7 | Switches RTX Hair off and on. |
| Ctrl+Shift+F8 | Switches DLSS 5 Neural Rendering off and on, when it was enabled at game start (experimental, you provide its runtime). |

Change them on the launcher's **Hotkeys** page.

### Known issues

- Tested only on an RTX 5090 with driver 621.01, mostly in Act 1 and a few Act 3 places. Act 2, most of Act 3 and
  sessions of several hours are not systematically tested.
- Video memory is the main risk, especially on 12 to 16 GB cards. If you see stalls, missing geometry or crashes,
  lower the preset or resolution and restart the game.
- The first game start after installing can spend 15 to 50 seconds longer creating shaders. Later starts reuse them.
- RTX Hair limits, and the full list: [release notes](docs/release/RELEASE_NOTES_0.9.6.md#known-issues).
- To report a bug: **Create a report…** in the launcher (Home, or Help & support), then attach the ZIP on the Bugs
  tab of the BG3RTX page on Nexus Mods. Nothing is uploaded automatically.

### Licence

BG3RTX is free software under **GPL-3.0-or-later** with an additional permission for the NVIDIA SDK libraries it
ships: [LICENSE](LICENSE), [LICENSE-EXCEPTION.md](LICENSE-EXCEPTION.md). Third-party components and their licences
are in [THIRD_PARTY.md](THIRD_PARTY.md). This repository (https://github.com/NIGos/BG3RTX) is the complete source code. `granny2.dll` is never shipped or downloaded: the optional "Convert all
styles" for RTX Hair needs a copy you provide. NVIDIA, GeForce RTX and DLSS are trademarks of NVIDIA Corporation.
BG3RTX is not made, endorsed or supported by NVIDIA or Larian Studios.

## Developers

Inside the layer, a core DLL and HLSL shaders reload while the game is running. Build from a PowerShell
development environment with MSVC, Ninja and the Vulkan SDK:

```powershell
.\scripts\build.ps1 -Test          # build and run CTests
```

Start with [the session handoff](docs/HANDOFF_SESSION.md) and
[the progress report](docs/PROGRESS_CODEX.md). Read [the architecture](docs/ARCHITECTURE.md)
for the host/core boundary, [QA](docs/QA.md) for checks and [the docs index](docs/README.md)
for effect designs.

Agents must launch the game only through `scripts/test-queue.ps1`. Live batches need
explicit user steering. Do not start the runner while the user plays or while
`captures/queue/USER_PLAYING` exists. See [AGENTS.md](AGENTS.md) for the full rules.
Use the Vulkan renderer (`bg3.exe`).

| Do this | Effect |
|---|---|
| `Ctrl+Shift+F10` in game, or create an empty `trace.request` file in the repo root | records one frame to `captures/<stamp>/frame.json` |
| `py scripts\analyze_trace.py --disasm` | writes `report.md` and `graph.dot` and decompiles the frame's shaders |
| `.\scripts\build.ps1 -Watch` | rebuilds the core on every save; the game swaps it in at the next present |
| edit anything under `shaders\` | the shader recompiles and its pipeline is rebuilt in game |
| edit `bg3rtx.ini` | re-read within about a second |
| check `bg3rtx.log` | output from the host, core and shader compiler; it can be read while the game runs |
