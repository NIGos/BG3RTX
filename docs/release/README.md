# BG3RTX (beta)

BG3RTX is licensed under GPL-3.0-or-later with the NVIDIA SDK exception (user decision 2026-11-04).
See [LICENSE](LICENSE) and [LICENSE-EXCEPTION.md](LICENSE-EXCEPTION.md); third-party terms are in [THIRD_PARTY.md](THIRD_PARTY.md).

BG3RTX adds ray-traced lighting to Baldur's Gate 3: ray-traced sun and local-light shadows, global
illumination, reflections, NVIDIA DLSS Ray Reconstruction and DLSS Frame Generation. It runs as a
Vulkan layer that `BG3RTX.exe` enables for the game process only. Nothing in the game folder, the
registry or your saves is changed.

This is a beta. Expect bugs. Please report them (see "Reporting a bug").

## Requirements

- Windows 10 or 11, 64-bit.
- **NVIDIA GeForce RTX GPU.** This beta targets RTX cards. Ray Reconstruction supports RTX cards.
  Frame generation 2x needs RTX 40 or newer; 3x and 4x need RTX 50.
  AMD and Intel GPUs are not qualified for this beta.
- An NVIDIA Game Ready driver. The recorded test driver is 621.01. Other versions are untested.
  This is a test reference, not a verified minimum driver version.
- **Baldur's Gate 3 on Steam, Vulkan renderer** (`bin\bg3.exe`, not the DirectX 11 one). Tested with
  game version 4.1.1.7631656 (Patch 8, Hotfix 10). A new game patch can break BG3RTX until it is updated.
  GOG copies should work through **Browse...** in the launcher but are untested.
- Disk space for the extracted archive plus a growing shader cache in `%LOCALAPPDATA%\BG3RTX`.
  Final package size and long-session cache growth are not yet qualified.

## Install

1. Unzip the archive anywhere (not inside the game folder).
2. Close the game and the BG3RTX settings window. Double-click **Install.cmd**.
   Administrator rights are not needed. It copies BG3RTX to `%LOCALAPPDATA%\Programs\BG3RTX` and adds
   **BG3RTX** to the Start menu. For another folder:
   `powershell -ExecutionPolicy Bypass -File install.ps1 -Destination "D:\Mods\BG3RTX"`.
   You can also skip the installer and run `bin\BG3RTX.exe` straight from the unzipped folder.
3. Start **BG3RTX** (`bin\BG3RTX.exe`). It finds Steam installs by itself; otherwise use **Browse...**
   and pick the folder that contains `bin\bg3.exe`.
4. Pick a preset (the window suggests one for your GPU) and press **Save**, then **Play**.
5. In the game's Graphics options set the upscaler to **DLSS** (Quality recommended). Ray
   Reconstruction replaces the game's DLSS; it does not run with FSR, XeSS or TAA.

To start through Steam instead: game **Properties > General > Launch options**:
`"<BG3RTX folder>\bin\BG3RTX.exe" %command%`. Steam then starts the game with BG3RTX and your saved
settings, without a window.

**Update:** close the game and settings window. Run the new version's Install.cmd into the same folder.
For a custom folder, repeat the command with the same `-Destination`. Your settings (`bg3rtx_user.ini`)
are kept. Reinstalling is safe. Existing files replaced on the first install are kept in
`.bg3rtx_backup` until uninstall. Do not delete that folder.

**Uninstall:** run **Uninstall.cmd** in the install folder. It removes every BG3RTX file, the Start menu
entry and the shader cache. It restores files backed up before installation and keeps unrelated files.
Close the game and settings window first. If you set the Steam launch option, clear it by hand.
`powershell -ExecutionPolicy Bypass -File uninstall.ps1 -KeepSettings` keeps your `bg3rtx_user.ini`.
Neither script changes the game, saves or registry. For a portable copy, remove its extracted folder
and clear any Steam launch option; there is no installed Vulkan layer to unregister.

## Presets

| Preset | Relative cost | What it does |
|---|---|---|
| Low | Lowest | Screen-space GI and contact shadows. No RT scene or Ray Reconstruction. |
| Medium | Higher | Opaque RT scene, RT GI, fast local-light shadows and glossy reflections. |
| High | High | Adds alpha-tested geometry, including foliage, to the RT scene. |
| Ultra | Highest | Two-ray GI, hidden roofs, Full local/box shadows, Rough reflections, water + Refraction, Far (6 m) bounce/emission, stock hair/scalp shadows and temporal depth of field/bloom. Rendered cap 58 fps. |

Reference measurements come from an RTX 5090, not a GPU support matrix or an FPS guarantee.
The final release configuration still needs integrated smoke and long-session acceptance.
Every control, its choices and restart requirements are in `docs\USER_SETTINGS.md`.
Lighting, Surfaces and Performance contain player controls; Help & support contains compatibility,
memory and reporting help. Visible hair remains the game's stock hair.

### VRAM guidance

No VRAM capacity or preset has completed memory qualification. GPU model suggestions do not
measure free driver budget. A recorded port long view used 31.7 GB against a 30.6 GB budget
on a 32 GB card, so even that capacity is not guaranteed safe. There are no qualified 12/16/24 GB
preset recommendations, and RTX 30/40 acceptance remains open. Start with Low and raise settings
cautiously. Lower resolution/preset if stalls, missing geometry or crashes appear, then restart:
allocations from the higher preset may remain. The game, other applications and FG need headroom.

### Included features and measured costs

These scene-specific GPU pass measurements are not additive and do not predict your frame rate.
Stock hair stays visible; no replacement hair is part of this release.

| Feature | Recorded evidence / cost on the test system |
|---|---|
| RT sun/local shadows, GI, reflections and RR | Existing release paths; no universal per-feature cost receipt |
| Ultra opacity micromaps and cutout masks | Camp: 1.17 G compared rays, zero violations; RT 8.65-8.75 versus 9.37-10.08 ms; local data/hardware fallback applies |
| Box light shadows respecting game shadow flags | S6: local lighting +0.13 ms; signed energy delta 0.0 within repeat noise |
| Off-screen bounce/emission, Far (6 m) | S8/camp: GI trace +3.2/+3.7 ms; energy -2.01%/-2.42% (shading change) |
| Stock hair/scalp shadows and occlusion | Recorded RT +2.3-6.3 ms and GI +3.2-8.8 ms; capture pool trim reduces used mesh pool 26-50%, with no numeric GPU saving measured |
| Water reflections, shadows, refraction and camera correction | wf9 08:59 qualified: static ON equals OFF, turning screen hits about 6.7% to 17%; final compose median 0.154 ms absolute, not an incremental cost |
| Temporal depth of field and bloom | Ultra promotion accepted on user visual verdict; isolated pass 0.152-0.153 ms, about 250 MiB reference memory; paired statistical quality/energy tests did not pass |

Water surface Fresnel is not promoted. Generated rows, hidden-water shading, SHaRC, reduced
trace resolution and the rejected GPU candidates are excluded from the release feature set.

**Compare with the original image:** press **Ctrl+Shift+F9** in game to switch every ray-traced effect off
and on again. DLSS and frame generation stay as they are.

## Known limits (beta)

- Tested mostly in Act 1 (camp, Grymforge, Adamantine Forge) and a few Act 3 places (Water Queen's
  House, the Blushing Mermaid). Act 2 and most of Act 3 are not systematically tested.
- Long sessions (several hours) are not tested yet. The log is capped (two files of at most 64 MB).
- Frame generation Off/on, geometry and first capture activation need a restart (see settings guide).
- First enabling stock hair/scalp capture compiles shaders. Expect a pause or stutter while compilation finishes.
- VRAM pressure is a particular risk on 12-16 GB cards. Long sessions and level changes can retain memory,
  even on larger cards. See VRAM guidance above.
- The water flicker seen at the port is also present in the game's own water. BG3RTX does not fix it.
- Other Vulkan overlays (Steam, EOS, GOG) can conflict: tick "Disable other Vulkan overlays" in the
  launcher if the game crashes at start or frame generation does nothing. NVIDIA Smooth Motion is
  turned off automatically while frame generation is on.
- ReShade, other graphics mods and HDR are untested together with BG3RTX.
- The DirectX 11 renderer is not supported.

## Reporting a bug

BG3RTX writes `bg3rtx.log` in its install folder (`%LOCALAPPDATA%\Programs\BG3RTX` by default). At the
next game start it becomes `bg3rtx.prev.log`, so only two log files are kept. Long sessions also rotate logs.
Collect a report immediately after the problem, before launching again.

1. Run **Diagnostics.cmd** from the installed BG3RTX folder. It creates a local report ZIP and prints
   its location and contents. Nothing is uploaded. Open `CONTENTS.txt` and review the ZIP before sharing.
2. Attach the ZIP on the Nexus mod page's Bugs tab. Add your GPU and VRAM size, driver version,
   preset, game version, and any other graphics mods.
3. Describe the steps to reproduce, act and place, expected result and actual result.
   A screenshot or short video helps. Ctrl+Shift+F9 can show whether an effect depends on BG3RTX.

Diagnostics includes logs, settings, version, GPU/OS details and existing crash dumps. It does not
enable crash dumps or include a save by default. Binary dumps can contain personal data.
Only if asked, run `Diagnostics.cmd -IncludeSave` to choose one `.lsv` file and confirm its inclusion.
The original is opened read-only. Saves and dumps cannot be redacted automatically.

If Diagnostics.cmd is missing or fails, attach these files manually:

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
Compiler. Their licences are in `THIRD_PARTY.md` and `licences\`; bundled fonts carry their OFL in `bin\ui\fonts\`. NVIDIA, GeForce RTX and DLSS are
trademarks of NVIDIA Corporation. BG3RTX is not made, endorsed or supported by NVIDIA or Larian Studios.
