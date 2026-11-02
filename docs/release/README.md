# BG3RTX (beta)

BG3RTX adds ray-traced lighting to Baldur's Gate 3: ray-traced sun and local-light shadows, global
illumination, reflections, NVIDIA DLSS Ray Reconstruction and DLSS Frame Generation. It runs as a
Vulkan layer that `BG3RTX.exe` enables for the game process only. Nothing in the game folder, the
registry or your saves is changed.

This is a beta. Expect bugs. Please report them (see "Reporting a bug").

## Requirements

- Windows 10 or 11, 64-bit.
- **NVIDIA GeForce RTX GPU.** Ray Reconstruction needs any RTX card. Frame generation 2x needs an RTX 40 or
  newer, 3x and 4x need an RTX 50. AMD and Intel GPUs with ray tracing get the Low preset only, without
  Ray Reconstruction. They are untested.
- A recent NVIDIA Game Ready driver. The beta was tested with driver 621.01. Older drivers are untested.
- **Baldur's Gate 3 on Steam, Vulkan renderer** (`bin\bg3.exe`, not the DirectX 11 one). Tested with
  game version 4.1.1.7631656 (Patch 8, Hotfix 10). A new game patch can break BG3RTX until it is updated.
  GOG copies should work through **Browse...** in the launcher but are untested.
- About 150 MB of disk space, plus a shader cache of a few tens of MB in `%LOCALAPPDATA%\BG3RTX`.

## Install

1. Unzip the archive anywhere (not inside the game folder).
2. Double-click **Install.cmd**. It copies BG3RTX to `%LOCALAPPDATA%\Programs\BG3RTX` and adds
   **BG3RTX** to the Start menu. For another folder:
   `powershell -ExecutionPolicy Bypass -File install.ps1 -Destination "D:\Mods\BG3RTX"`.
   You can also skip the installer and run `bin\BG3RTX.exe` straight from the unzipped folder.
3. Start **BG3RTX** (`bin\BG3RTX.exe`). It finds Steam installs by itself; otherwise use **Browse...**
   and pick the folder that contains `bin\bg3.exe`.
4. Pick a preset (the window suggests one for your GPU) and press **Save and launch**.
5. In the game's Graphics options set the upscaler to **DLSS** (Quality recommended). Ray
   Reconstruction replaces the game's DLSS; it does not run with FSR, XeSS or TAA.

To start through Steam instead: game **Properties > General > Launch options**:
`"<BG3RTX folder>\bin\BG3RTX.exe" %command%`. Steam then starts the game with BG3RTX and your saved
settings, without a window.

**Update:** run the new version's Install.cmd into the same folder. Your settings (`bg3rtx_user.ini`) are kept.

**Uninstall:** run **Uninstall.cmd** in the install folder. It removes every BG3RTX file, the Start menu
entry and the shader cache. If you set the Steam launch option, clear it by hand.
`uninstall.ps1 -KeepSettings` keeps your `bg3rtx_user.ini`.

## Presets

| Preset | Meant for | What it does |
|---|---|---|
| Low | RTX 2060-3060, 4060; AMD/Intel with ray tracing | No ray-traced scene, no Ray Reconstruction: screen-space GI and contact shadows |
| Medium | RTX 3070/3080, 4060 Ti/4070 | Ray-traced opaque scene, RT GI, fast local-light shadows, glossy reflections |
| High | RTX 4080/4090, 5080 | Everything ray traced |
| Ultra (RTX 5090) | RTX 5090 | High plus more GI rays, shadowed lights, rougher reflections, ray-traced water, hidden-surface shading and a 58 fps rendered cap (116 shown with frame generation 2x) |

Only Ultra on an RTX 5090 has been measured (about 16.7 ms per rendered frame at 2560x1067 rendered to
3840x1600). The other presets are estimates for the GPUs listed. Every setting, its choices and which
changes need a game restart are in `docs\USER_SETTINGS.md`.

**Compare with the original image:** press **Ctrl+Shift+F9** in game to switch every ray-traced effect off
and on again. DLSS and frame generation stay as they are.

## Known limits (beta)

- Tested mostly in Act 1 (camp, Grymforge, Adamantine Forge) and a few Act 3 places (Water Queen's
  House, the Blushing Mermaid). Act 2 and most of Act 3 are not systematically tested.
- Long sessions (several hours) are not tested yet. The log is capped (two files of at most 64 MB).
- Frame generation on <-> off, and some Experimental levels, need a game restart (the window marks them).
- "Experimental" settings are being tested: they can cost a lot of GPU time or look wrong in places.
- Other Vulkan overlays (Steam, EOS, GOG) can conflict: tick "Disable other Vulkan overlays" in the
  launcher if the game crashes at start or frame generation does nothing. NVIDIA Smooth Motion is
  turned off automatically while frame generation is on.
- ReShade, other graphics mods and HDR are untested together with BG3RTX.
- The DirectX 11 renderer is not supported.

## Reporting a bug

BG3RTX writes `bg3rtx.log` in its install folder (`%LOCALAPPDATA%\Programs\BG3RTX` by default). At the
next game start it becomes `bg3rtx.prev.log`, so only the last two sessions are kept: copy the logs before
you start the game twice more. Send, on the mod page's Bugs tab:

1. `bg3rtx.log` and `bg3rtx.prev.log`, `bg3rtx_user.ini` (your settings) and `VERSION.txt` from the install folder.
2. Your GPU, driver version, and the preset or settings you changed.
3. Where it happens (act, place, time of day) and what you see. A screenshot with Ctrl+Shift+F9 off and
   on helps a lot.
4. For a frame generation or DLSS problem, also the `sl_logs` folder.

Never send a save unless you are asked for one. The diagnostic tool below copies only a save you
explicitly select. It uploads nothing.

### Diagnostic package

Double-click **Diagnostics.cmd** in the BG3RTX install folder (Windows PowerShell 5.1 is included
with Windows). It creates one timestamped `BG3RTX-diagnostics-*.zip` in that folder and prints its
contents and size. No save is included by default. To choose one optional save in a file dialog,
run `Diagnostics.cmd -IncludeSave`; cancel or answer No to keep it out. The dialog shows the save's
size and defaults to No at confirmation. Close the game first: save collection refuses while
`bg3.exe` or `bg3_dx11.exe` runs. The selected original is opened read-only and never changed.

The zip contains install-root `bg3rtx.log` and rotated/tagged logs, `bg3rtx.ini`, `bg3rtx_user.ini`,
`VERSION.txt`, the logs' `[cfg] knobs`/`keys` and device lines, OS version and a light GPU/driver
query (`Win32_VideoController`). Existing `.dmp` files in `captures\crash` and `.nv-gpudmp` files in
`captures\aftermath` are included if present. Crash dumps remain off for players; this tool never
enables them. Missing files are listed in `CONTENTS.txt`. It does not scan save folders or include
other capture files, shader caches, or `sl_logs`.

Text is redacted for Windows user names in profile paths and obvious credentials (tokens, API keys,
passwords and bearer headers). Binary dumps and a selected save are copied unchanged and **can contain
personal data that cannot be redacted**. Review the package before sharing it on the Bugs tab; no files
are uploaded automatically. Other personal text may remain. For scripted use, the same tool accepts
`powershell -NoProfile -ExecutionPolicy Bypass -File diagnostics.ps1 -InstallDir "D:\Mods\BG3RTX"`
and an explicitly selected `-SavePath "D:\Chosen\save.lsv"` instead of the dialog.

## Licences and credits

BG3RTX ships NVIDIA DLSS, DLSS Frame Generation, Reflex and Streamline, and Microsoft's DirectX Shader
Compiler. Their licences are in `THIRD_PARTY.md` and `LICENSES\`. NVIDIA, GeForce RTX and DLSS are
trademarks of NVIDIA Corporation. BG3RTX is not made, endorsed or supported by NVIDIA or Larian Studios.
