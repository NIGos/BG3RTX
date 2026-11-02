# Nexus beta: release check, package, licences, what remains (stage 6 first pass, 2026-11-02)

Developer document (not shipped). Player documents: [README.md](README.md) and [THIRD_PARTY.md](THIRD_PARTY.md)
(both go into the package). Earlier check: [../RELEASE_CHECK_20261101.md](../RELEASE_CHECK_20261101.md).

## 1. Release defaults audit

Method: every `cfg_*("key", literal)` in `layer/` and `core/` (565 call sites), its effective value in a
player package (`data/release.ini` > `bg3rtx.ini` > literal), filtered on diagnostic names, then read at the
call site. Player package = repository `bg3rtx.ini` + `data/release.ini` + the lean layer (`BG3RTX_DIAGNOSTICS=OFF`).

**Already off in the package:** `dev=0` (trace hotkey, developer GUI entries), `dump_shaders=0`, every debug view,
stats, check and capture key (45 keys in `release.ini`), `crash_dump=0`, `aftermath` (0, DLL not shipped),
`sl_verbose` (0), the `sl_dev` DLLs (not shipped). Keys only in code (`auto_trace`, `fg_snap`, `assetdb_join`,
`dev_fault_bindings`) default to 0 and act only when set.

**Fixed:** `rt_hit_reference=0` pinned in `release.ini` (developer-only debug reference; 0 in `bg3rtx.ini`
already, so nothing a player sees changes). Packaging bugs in the old `package.ps1`: it shipped no `common/`
headers, which 21 shader files include (`gi_trace_rt`, `rt_local_shadows`, `rt_reflections`, ...), so the default
RT path could not compile in a player package; and no asset database. Both fixed (section 2).

**Kept on (deliberate):**

| Key / behaviour | Shipped | Why kept |
|---|---|---|
| `ab_hotkey=1` | on | documented player A/B (Ctrl+Shift+F9) |
| `rt_mesh_audits=2`, `rt_em_calib=1`, `rt_em_check_*` | on | correctness: stale-mesh eviction, emissive calibration (sliced, cheap) |
| `dev_fault=1` (code literal) | on | `VK_EXT_device_fault` details in the log after a device lost: the most useful line of a crash report |
| `watchdog_s=5` (literal), `fg_state_every=60` | on | one log line per 5 s; shows where a hang happened |
| `bg3rtx.log` | always | ~3 KB/s, truncated each launch, **no size cap**: ~11 MB/hour (long sessions, section 4) |
| `sl_logs\` | always | Streamline at its default level; needed for FG reports |
| core hot reload | on | `bin\hot\` copy + mtime poll; also isolates a core fault (core disabled, game keeps running) |
| `trace.request` file | honoured | a trace only when the user creates that file on request; no hotkey with `dev=0` |
| Experimental GUI entries (4) | visible | intended: off in every preset below Ultra; labelled |

**Doubtful (not changed, user decision):**
1. `ls_box=0` and `rt_emissive=0` in `release.ini`: same values as `bg3rtx.ini`, and the GUI layer overrides
   them (Ultra writes `rt_emissive=1` through Emission at ray hits). They are quality keys in a list of developer
   keys; removing them changes nothing for players. Remove (cleaner) or keep (harmless)?
2. Developer-only knobs `ls_k`, `rt_sun_cascade_current`, `rt_hit_emission=2`: hidden with `dev=0` but honoured
   when typed into `bg3rtx_user.ini`. Not pinned: they are quality keys, parked by user decision.
3. `crash_dump=0`: kept off (decision of 11-01); the diagnostic package (section 5) is the planned path.
4. Log size: no cap. A cap (for example 50 MB, then only warnings) is a small layer change; needs a restart test.

## 2. Package and install

`scripts/package.ps1 [-Version 0.1.0-beta1] [-NoBuild] [-NoZip] [-WithSharc] [-Diagnostics]`: refuses while
bg3.exe runs, runs `build.ps1 -Test` (BelowNormal), builds the lean layer in `build-nodiag\`, assembles
`dist\BG3RTX-<version>\` from an explicit list, writes `files.txt` and `VERSION.txt`, zips it, and zips the PDBs
into `dist\BG3RTX-<version>-symbols.zip` (keep it; do not upload). Contents: see the script header. The
`common\` headers are found by following the shaders' `#include`s.

Install model (found in `tools/launcher/launch.cpp`): **no implicit layer and no registry**. `BG3RTX.exe` starts
`bin\bg3.exe` with `VK_ADD_LAYER_PATH=<install>\bin` and `VK_INSTANCE_LAYERS=VK_LAYER_BG3RTX_host` for that
process only; everything else lives next to the launcher (`root_dir()` = parent of `bin\`). So:
- `install.ps1`: copies `files.txt` to `%LOCALAPPDATA%\Programs\BG3RTX` (or `-Destination`), refuses a game
  folder or Larian's profile folder, removes files an older version had, keeps `bg3rtx_user.ini`, writes
  `install_manifest.txt` + `.bg3rtx_install`, adds a Start menu shortcut, prints the Steam launch option.
- `uninstall.ps1` (`Uninstall.cmd` runs a temp copy): removes the manifest's files, what BG3RTX writes
  (`bg3rtx*.log`, `bg3rtx_user.ini` unless `-KeepSettings`, `bg3rtx_launcher.ini`, `bin\hot`, `sl_logs`,
  `captures`), the shortcut, `%LOCALAPPDATA%\BG3RTX\shader_cache`, then empty folders. Never follows links,
  refuses any path in `Larian Studios` or a game folder. `-WhatIf` lists without deleting. It cannot clear a
  Steam launch option (Steam's own config); it tells the user to.

## 3. Licences (inventory: THIRD_PARTY.md)

Shipped: DLSS SR/RR (`nvngx_dlss`, `nvngx_dlssd`), DLSS-G (`nvngx_dlssg`, `sl.dlss_g`), Streamline (MIT),
Reflex runtime, DXC (`dxcompiler.dll`, NCSA), Vulkan headers (compiled in), the asset DB. Not shipped: SHaRC
headers, Aftermath, OMM SDK, `sl_dev`, Nsight Perf. Licence texts copied: `DLSS\LICENSE.txt`,
`streamline\license.txt`, `3rd-party-licenses.md`. **Nothing found forbids redistributing what ships.** Obligations
and flags:
- NVIDIA RTX SDKs License: object-code distribution inside an application with material added functionality
  (met); terms at least as protective (licence text shipped); NVIDIA marks in credits (README); no endorsement
  claim (README). **Notify NVIDIA before a commercial release** (supplement 4): a free beta is arguably not
  commercial; notifying at developer.nvidia.com/sw-notification is cheap. User decision.
- **SHaRC (flag):** runtime HLSL needs its headers as source; the RTX licence grants object-code distribution only.
  Not shipped (`sharc=0` everywhere, no GUI entry). Before SHaRC ships: precompiled SPIR-V or NVIDIA's permission.
- **DXC and Vulkan-Headers texts missing on disk (flag):** save them as `external\licences\DirectXShaderCompiler-LICENSE.txt`
  and `Vulkan-Headers-LICENSE.txt` (from their GitHub repos); `package.ps1` warns until then.
- **Asset DB (flag):** numbers extracted from Larian's `.pak` files (no textures or meshes). Check Larian's mod
  terms; the alternative (build on the player's PC with Python 3.14) is heavy.
- **Our own licence (flag):** the repository has none. It must not put the NVIDIA parts under an open-source
  licence (RTX licence 4e).
- OMM SDK: no licence file on disk; not shipped, fetch it before `omm-lib.dll` is ever needed at run time.

## 4. What remains before a public beta

- **Long sessions:** a 2-4 hour run per preset (memory: mesh pool, hit-material pool, shader cache; log size;
  FG stability; VRAM growth across level loads). Needs live steering.
- **Acts 2-3:** Act 2 (Shadow-Cursed Lands, Moonrise, Gauntlet of Shar) has no live coverage; its saves need the
  mod-warning handling (TEST_SCENES). Act 3 has S7/S8/S14 only.
- **Non-5090 GPUs:** presets below Ultra are estimates; at least one RTX 40 and one RTX 30 run.
- **Fresh-PC install test:** unzip, Install.cmd, launch via window and via Steam, uninstall, check nothing is
  left (no VULKAN_SDK on that PC: DXC must come from `bin\`).
- `docs/USER_SETTINGS.md` still says Experimental entries are off in every preset; RT water, Hidden-surface
  shading, Emission and Far-field moved to the main list and into Ultra.
- Game-version check: log `bg3.exe` ProductVersion and warn on an unknown one.
- SE bridge (`mods/BG3RTX_Bridge`): not packaged; optional, needs the Script Extender.

## 5. Diagnostic package mode (design only, not built)

`BG3RTX.exe --report` (and a "Create bug report" button) builds `BG3RTX-report-<stamp>.zip` on the Desktop:
- `bg3rtx.log` (+ the previous one: the layer would rename `bg3rtx.log` to `bg3rtx.prev.log` at start instead of
  truncating), `sl_logs\`, `VERSION.txt`, `bg3rtx_user.ini` and the effective `[cfg] keys` line of the last log;
- GPU info: `--detect` output, driver version, OS build, display modes (no serials, no user names: paths are
  rewritten to `%LOCALAPPDATA%`);
- a crash dump only if one exists: `crash_dump` stays 0; the report offers "turn crash dumps on for the next
  session" (writes `crash_dump=1` to `bg3rtx_user.ini`, the next report takes `captures\crash\*.dmp` and turns it off);
- **a save only when the user picks one** in a file dialog started in `Savegames\Story`: the folder is copied
  with read-only opens (`FILE_SHARE_READ`, no write handle, never moved, renamed or touched; Steam Cloud syncs that
  folder), size shown first, and the user confirms. BG3RTX never lists or reads saves on its own;
- a `manifest.txt` listing every file in the zip, shown to the user before saving. Nothing is uploaded.
