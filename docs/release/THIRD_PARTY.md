# Third-party components in BG3RTX

Disk-only audit: 2026-11-02, branch `codex-licences`. Scope: the explicit file list in
`scripts/package.ps1`, its `-WithSharc` option, CMake include paths (read only), and runtime
loading in `layer/` and `tools/launcher/`. No build, package, compiler or game was run.

The verbatim texts collected for release are in `docs/release/licences/` in the source tree.
Their player-package location is `licences/` with the same relative paths. The package script
copies every file in this directory and validates the required list below before building or writing
a package. Packaging remains paused. See Missing below before distributing.
This inventory is not a statement that every release requirement has been satisfied.

## Packaged and compiled components

| Component | Release files / use | Version identified on disk | Licence / collected text |
|---|---|---|---|
| Microsoft DirectX Shader Compiler (DXC) | `bin/dxcompiler.dll`; runtime HLSL-to-SPIR-V; `dxc/dxcapi.h` compiled into layer | Selected fallback `C:\VulkanSDK\1.4.357.0\Bin\dxcompiler.dll`: file 1.9.0.5399, product `1.9.0.5399 (a107ba613)` | Full version-matched text missing. Installed API header says University of Illinois Open Source License (NCSA); binary-wide NCSA vs Apache-2.0 WITH LLVM-exception classification remains unverified. |
| Khronos Vulkan-Headers | Headers compiled into layer, core and launcher; no Vulkan SDK DLL is copied | `C:\VulkanSDK\1.4.357.0\Include\vulkan\vulkan_core.h`: `VK_HEADER_VERSION` 357, API 1.4.357 | Header SPDX: `Apache-2.0 OR MIT`. Version-matched upstream licence/notice bundle missing; full Apache 2.0 terms are available in the Nsight text below, with NVIDIA attribution (not a substitute for Khronos notices). |
| NVIDIA Streamline | `bin/sl/sl.interposer.dll`, `sl.common.dll`, `sl.dlss_g.dll`, `sl.reflex.dll`, `sl.pcl.dll`; SDK headers compiled into layer | SDK 2.14.16 (`include/sl_version.h`); all five DLL resources 2.14.16.0 | MIT, with exceptions described in the full SDK text: `Streamline-MIT.txt`; preserve `Streamline-3rd-party-licenses.md`. |
| NVIDIA DLSS SR, Ray Reconstruction and Frame Generation | `bin/sl/nvngx_dlss.dll`, `nvngx_dlssd.dll`, `nvngx_dlssg.dll` | All three DLL resources 310.9.16.0, from Streamline 2.14.16 production `bin/x64` | NVIDIA RTX SDKs License: `NVIDIA-DLSS-runtime.txt`, not Streamline MIT. |
| NVIDIA Reflex Low Latency | `bin/sl/NvLowLatencyVk.dll`; Streamline Reflex integration | From Streamline 2.14.16 production `bin/x64`; DLL has no file/product version resource | NVIDIA SDK agreement and Reflex supplement: `NVIDIA-Reflex.txt`, in addition to Streamline plugin MIT. |
| NVIDIA NGX / DLSS SDK headers | `nvsdk_ngx_vk.h`, `nvsdk_ngx_defs_dlssd.h` etc. compiled into layer | Local DLSS checkout tag v310.6.0, commit `d1bef2006b41eefd9d44b0a05f123993f3acbf3c`; NGX API macro 0x15 (1.5.0) | NVIDIA RTX SDKs License: `NVIDIA-RTX-SDKs-DLSS.txt`. Streamline's `external/ngx-sdk/license.txt` is byte-identical to `NVIDIA-DLSS-runtime.txt`. |
| NVIDIA Image Scaling notice | Notice supplied beside production Streamline DLLs; no standalone NIS DLL is in the package list | Bundled with Streamline 2.14.16 | Preserve bundled notice `NVIDIA-Image-Scaling-MIT.txt` conservatively; this does not assert BG3RTX implements NIS. |
| NVIDIA Nsight Aftermath SDK headers | Optional compile-time dependency of layer (`BG3RTX_AFTERMATH`); `GFSDK_Aftermath_Lib.x64.dll` can be loaded when `aftermath=1`, but is excluded from package | SDK folder 2026.3; API 2.27; main checkout's development DLL file 2.27.0.0, product 2026.3.0.0 (build 38538246) | `NVIDIA-Aftermath.txt` retained for SDK/header coverage. No Aftermath DLL redistribution is added by this audit. |

All nine worktree `bin/sl/*.dll` files were SHA-256 compared with the corresponding
`external/streamline-sdk-v2.14.16/bin/x64` files: **9 of 9 byte-identical**. The DLSS header
checkout version and the newer Streamline-bundled runtime version are deliberately recorded separately.
There is no `bin/dxcompiler.dll` in this worktree or the main checkout; the package script therefore
selects the SDK fallback above. Its SHA-256 is
`1ea6b8a639c43ca2d9e14712962180a887879773e994d8370a087907371be763`.
Re-audit when any selected binary, SDK or fallback changes.

### DXIL scope

`dxil.dll` is not copied by `scripts/package.ps1`, explicitly loaded by BG3RTX, or present in
this worktree's `bin/` or `C:\VulkanSDK\1.4.357.0\Bin`. DXC contains a `dxil.dll` string;
that alone does not establish whether this SPIR-V path loads it. No runtime check was authorized.
Other installed Windows SDK / NVIDIA App copies are not the selected package dependency.
Do not add an arbitrary one. If a DXIL validator is added or shown to load, record its actual
path, version and hash and obtain its matching distribution notices, including any separate
Microsoft binary redistribution terms. Current DXC findings do not establish the licence of
those other `dxil.dll` copies.

## Exact text provenance

Source paths are audit evidence on this machine, not player installation requirements.
Each file below was originally copied as bytes. Release texts are now normalised to LF;
only CRLF/CR newline bytes were changed. The hashes below describe the normalised files,
not the original source bytes.

| Collected filename (under `docs/release/licences/`) | Absolute source path | Version / provenance |
|---|---|---|
| `NVIDIA-RTX-SDKs-DLSS.txt` | `C:\Users\quali\Documents\Code Projects\BG3RTX\external\DLSS\LICENSE.txt` | DLSS SDK v310.6.0; commit d1bef2006b41eefd9d44b0a05f123993f3acbf3c |
| `Streamline-MIT.txt` | `C:\Users\quali\Documents\Code Projects\BG3RTX\external\streamline-sdk-v2.14.16\license.txt` | Streamline 2.14.16 |
| `Streamline-3rd-party-licenses.md` | `C:\Users\quali\Documents\Code Projects\BG3RTX\external\streamline-sdk-v2.14.16\3rd-party-licenses.md` | Streamline 2.14.16 |
| `NVIDIA-DLSS-runtime.txt` | `C:\Users\quali\Documents\Code Projects\BG3RTX\external\streamline-sdk-v2.14.16\bin\x64\nvngx_dlss.license.txt` | Streamline 2.14.16; DLSS DLLs 310.9.16.0 |
| `NVIDIA-Reflex.txt` | `C:\Users\quali\Documents\Code Projects\BG3RTX\external\streamline-sdk-v2.14.16\bin\x64\reflex.license.txt` | Streamline 2.14.16; agreement/supplement December 9, 2020 |
| `NVIDIA-Image-Scaling-MIT.txt` | `C:\Users\quali\Documents\Code Projects\BG3RTX\external\streamline-sdk-v2.14.16\bin\x64\nis.license.txt` | Notice bundled with Streamline 2.14.16 |
| `NVIDIA-Aftermath.txt` | `D:\BG3_Tools\nsight-aftermath-sdk-2026.3\LICENSE` | Aftermath SDK 2026.3; API 2.27 |
| `NVIDIA-Nsight-Graphics-Apache-2.0.txt` | `C:\Users\quali\Documents\Code Projects\BG3RTX\external\streamline-sdk-v2.14.16\NVIDIA Nsight Graphics SDK License (Apache 2.0).txt` | Nsight Graphics SDK notice bundled with Streamline 2.14.16; Apache 2.0, January 2004 |

| Collected filename | SHA-256 (LF-normalised release copy) |
|---|---|
| `NVIDIA-RTX-SDKs-DLSS.txt` | `21b5daec892b12bea692e66bc8fe45cf5902ccaf3a7b831e78050d8859881c37` |
| `Streamline-MIT.txt` | `c7782fdc4dfc64f7e6b09f37a694dcfa64034b712903986d252b13a08d5849cd` |
| `Streamline-3rd-party-licenses.md` | `7c24ec8f5abd0a7bb0fa4fc35021146fd3bc2a2e7b0ed61e5977aad72d64aea3` |
| `NVIDIA-DLSS-runtime.txt` | `d4216e39ebef5f9b50a6712ebb37beeb5379862a67733a9999c651f21592aaf0` |
| `NVIDIA-Reflex.txt` | `c1ec2b31a3a2cda27486665dd97b9693b409be90847789fd2138fc9b8e2bc41d` |
| `NVIDIA-Image-Scaling-MIT.txt` | `6efb198d6d184a4c80ab66641463b708339d7e00098e7c92caf12e9f178b53c9` |
| `NVIDIA-Aftermath.txt` | `4f35875cfab9eab4725f70b7b2355de5b5310d5f956319450ba3dcb50f2f65d6` |
| `NVIDIA-Nsight-Graphics-Apache-2.0.txt` | `8517178e5aab9335ca0c88d02efcba2f5d42bd894a5c4bedfdf8b99693110e88` |

## Required notice files

This list is read by `scripts/package.ps1`. Every listed filename must exist under
`docs/release/licences/`; missing files stop packaging, including with `-NoBuild`.
DXC and Vulkan placeholders below name the still-missing version-matched bundles;
adding a filename alone does not establish that the text covers the selected binary/headers.
Retain all subordinate notices as additional files in this directory.
`-ListOnly` validates and emits only the notice payload as JSON, without building or writing files.

- `NVIDIA-Aftermath.txt`
- `NVIDIA-DLSS-runtime.txt`
- `NVIDIA-Image-Scaling-MIT.txt`
- `NVIDIA-Nsight-Graphics-Apache-2.0.txt`
- `NVIDIA-RTX-SDKs-DLSS.txt`
- `NVIDIA-Reflex.txt`
- `Streamline-3rd-party-licenses.md`
- `Streamline-MIT.txt`
- `DirectXShaderCompiler-LICENSE.txt`
- `Vulkan-Headers-LICENSE.txt`

## Required notices and release actions

The authoritative conditions are the complete copied texts, including their supplements.
The following are action pointers, not replacement licence text:

- Keep Streamline's copyright/permission notice and its complete bundled third-party notices.
  That notices file covers Premake, nlohmann JSON, D3DX12.h, VSWhere, ImPlot, ImGui, tinyexr/OpenEXR,
  slang, RapidYAML, DirectX 12 Agility SDK headers, Vulkan headers and Nsight Graphics SDK.
  `NVIDIA-Nsight-Graphics-Apache-2.0.txt` supplies the full Apache 2.0 terms for the
  Nsight Graphics entry, whose bundled notice contains only the short Apache notice.
- DLSS/NGX RTX SDK licence section 2 governs distribution. Its supplement section 4 requires
  NVIDIA notification before commercial release, including a plug-in to a commercial application:
  `https://developer.nvidia.com/sw-notification`. The existing project decision on notification
  remains open; no notification was sent. Review the supplement's applicable marketing obligations.
- Reflex is supplied under a separate SDK agreement, not just MIT. Its supplement section 3.1
  calls for a NVIDIA Reflex feature reference following the SDK UI guidelines. Preserve the full
  agreement and supplement and review the distribution scope in supplement section 2 before release.
- The NVIDIA source-derivative notice conditions are in RTX SDK licence section 2(b) and Reflex
  agreement section 1.2(iii). Retain those exact conditions; this audit does not claim the SDK
  source was modified or manufacture a replacement notice.
- Streamline's SDK licence also references Nsight Perf terms for `sl_nvperf.h` / `sl_nvperf.dll`.
  Those are not in BG3RTX's package or include list. Do not infer that every Streamline SDK
  component is MIT or ship extra plugins without checking their own terms.

BG3RTX uses NVIDIA DLSS and NVIDIA Reflex technology. NVIDIA, GeForce RTX, DLSS and Reflex are
trademarks of NVIDIA Corporation. Baldur's Gate 3 is a trademark of Larian Studios. BG3RTX is
not made, endorsed or supported by NVIDIA or Larian Studios.

## Offline-only, paused and externally supplied components

- **LSLib: not shipped and not required to run the player package.** Offline asset/VT tools
  use it (`tools/assets/vt_alpha.ps1`); mesh probes pin v1.20.4. These tools and LSLib DLLs
  are excluded by the package's explicit list. LSLib is identified as MIT in the existing
  probe provenance, but its exact licence text was not found in the inspected SDK/cache
  locations. See the conditional Missing item below; no generic MIT text was substituted.
- **SHaRC 1.8.3: paused, not shipped in the ordinary package.** NVIDIA SDK headers under
  `external/sharc-1.8.3/include` are omitted and `sharc=0` in presets. BG3RTX's own SHaRC
  wrapper shaders are copied with `shaders/`; this is not the SDK header distribution.
  `-WithSharc` is a separate opt-in: it copies those headers and
  `external/sharc-1.8.3/License.md` as `LICENSES/SHaRC-NVIDIA-RTX-SDKs.md`. That exact local
  licence is present, but is not placed in this ordinary-package licence collection.
  Resume licence/release review before enabling or distributing SHaRC.
- **Opacity Micromap SDK 1.10.0:** only the opt-in offline CPU probe links `omm-lib`.
  The player package excludes its DLL/tools. Native Vulkan OMM code does not add an SDK
  binary dependency. NRD is mentioned in CMake's generic SDK description but is not an
  include/link/copy dependency of the release targets inspected.
- NVIDIA Image Scaling sample dependencies (old DXC, ImGui, GLFW, stb, tinyexr/miniz) under
  `external/DLSS/NVIDIAImageScaling/samples` are not included by BG3RTX release targets.
  Their presence on disk does not make the old sample DXC licence apply to the selected SDK DXC.
- `vulkan-1.dll`, GPU driver/NGX `_nvngx.dll`, Windows system DLLs and the game's own DLLs
  come from the user's system/game, not this package. `BG3ScriptExtender.dll` is detected
  for compatibility, not bundled or installed by BG3RTX. Driver/OS licence texts are not
  replaced by the Vulkan-Headers licence.
- MSVC runtime is statically selected by `CMAKE_MSVC_RUNTIME_LIBRARY`; no VC redistributable
  installer/DLL is copied. Microsoft toolchain redistribution terms remain applicable;
  no claim is made that NVIDIA or Vulkan licences cover Microsoft's runtime.
- `data/assetdb/bg3rtx_assetdb.bin` contains lighting/material/roof data derived from the
  game's files. No third-party SDK licence here grants rights to redistribute game data.
  The package script currently copies this database; game-data release policy is a separate open item.

## Missing

1. **Version-matched DXC distribution licence and third-party notices (release blocker).**
   `C:\VulkanSDK\1.4.357.0\Include\dxc\dxcapi.h` explicitly names the University of Illinois
   licence and `LICENSE.TXT`, but the installed SDK does not provide that full text.
   An older NCSA text exists at
   `external/DLSS/NVIDIAImageScaling/samples/third_party/DXC/license.txt`; it accompanies a
   different compiler and was not relabelled as the 1.9 binary licence. Obtain the full
   licence/notice bundle for DXC commit `a107ba613` from Microsoft's
   `https://github.com/microsoft/DirectXShaderCompiler` release/source distribution, or the
   exact LunarG SDK 1.4.357.0 component source/notice bundle via `https://vulkan.lunarg.com/`.
   Confirm NCSA vs `Apache-2.0 WITH LLVM-exception` from that version's authoritative files,
   including all subordinate notices. Store verbatim texts here before shipping.
2. **Vulkan-Headers version-matched licence/notice bundle (release blocker).** The installed headers state
   `Apache-2.0 OR MIT`, but `C:\VulkanSDK\1.4.357.0\Licenses\LICENSE.txt` is only a general
   SDK registry disclaimer, not the component licence. Streamline supplies an older
   2015-2023 Khronos short Apache notice and a full Apache text in the Nsight SDK file
   copied above, but neither establishes the complete notices for SDK headers 1.4.357.
   Obtain the `LICENSE*` files and any
   notices from `https://github.com/KhronosGroup/Vulkan-Headers` at `v1.4.357` / the exact
   SDK source revision. Record the chosen licence alternative and retain matching copyright
   notices; do not substitute an unrelated project's MIT text.
3. **Package wiring (implemented; packaging remains paused).** The script copies this
   inventory to `THIRD_PARTY.md` and every collected file to `licences/`. Missing required
   texts now fail before builds or package writes. The missing DXC and Vulkan bundles above
   still block packaging. No shader or game-data shipping policy was changed.
4. **LSLib MIT text (conditional; not a player-package blocker while excluded).** If LSLib
   or dependent offline tools are distributed, obtain the exact v1.20.4 `LICENSE` from
   `https://github.com/Norbyte/lslib/blob/v1.20.4/LICENSE`, plus the separate notices/terms
   for every bundled dependency (MIT for LSLib does not cover every portable-release DLL).
   No LSLib executable, licence or matching NuGet/vcpkg package was found in the inspected locations.

Search coverage: main checkout `external/` (including nested sample SDKs), Vulkan SDK
1.4.357.0 (including hidden/ignored licence filenames), NuGet packages under
`C:\Users\quali\.nuget\packages`, all three local vcpkg cache archives under
`C:\Users\quali\AppData\Local\vcpkg\archives` (Eigen/vcpkg helpers only), Program Files LLVM
and NVIDIA SDK folders, Windows Kits, and read-only `D:\BG3_Tools\downloads`,
`external_backup` and `nsight-aftermath-sdk-2026.3`. No network was used. Missing means
not found in these inspected locations, not proof that no copy exists anywhere on the computer.
