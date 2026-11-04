# Third-party components in BG3RTX

BG3RTX uses GPL-3.0-or-later with the NVIDIA SDK exception, approved by the user on 2026-11-04.
The package root includes `LICENSE` and `LICENSE-EXCEPTION.md` (the same files as at the source root).
In the player package, both files are beside this inventory. Third-party components retain their own terms below.

Disk-only audit updated: 2026-11-03, branch `release-licences`. Scope: the explicit file list in
`scripts/package.ps1`, its `-WithSharc` option, CMake include paths (read only), and runtime
loading in `layer/` and `tools/launcher/`. Only disposable packaging tests were run; no player package, compiler or game was run.

The verbatim texts collected for release are in `docs/release/licences/` in the source tree.
Their player-package location is `licences/` with the same relative paths. The package script
copies every file in this directory and validates the required list below before building or writing
a package. Distribution remains paused pending owner decisions. See remaining release actions below before distributing.
This inventory is not a statement that every release requirement has been satisfied.

## Packaged and compiled components

| Component | Release files / use | Version identified on disk | Licence / collected text |
|---|---|---|---|
| Microsoft DirectX Shader Compiler (DXC) | `bin/dxcompiler.dll`; runtime HLSL-to-SPIR-V; `dxc/dxcapi.h` compiled into layer | Selected fallback `C:\VulkanSDK\1.4.357.0\Bin\dxcompiler.dll`: file 1.9.0.5399, product `1.9.0.5399 (a107ba613)` | University of Illinois/NCSA Open Source License, plus included third-party terms: `DirectXShaderCompiler-LICENSE.txt` and `DirectXShaderCompiler-ThirdPartyNotices.txt`, from commit `a107ba613`. See DXC classification below. |
| Khronos Vulkan-Headers | Headers compiled into layer, core and launcher; no Vulkan SDK DLL is copied | `C:\VulkanSDK\1.4.357.0\Include\vulkan\vulkan_core.h`: `VK_HEADER_VERSION` 357, API 1.4.357 | Header SPDX: `Apache-2.0 OR MIT`; Apache-2.0 alternative selected for these headers. Version-matched `Vulkan-Headers-LICENSE.txt`, `Vulkan-Headers-Apache-2.0.txt` and `Vulkan-Headers-MIT.txt` retained from tag `v1.4.357`. |
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

### DXC classification

At commit `a107ba613`, `LICENSE.TXT` opens with the headings `LLVM Release License`
and `University of Illinois/NCSA Open Source License`. This resolves the selected
DXC classification to NCSA with the file's included third-party terms, rather than
`Apache-2.0 WITH LLVM-exception`. The file also contains a section explicitly marked
`THE FOLLOWING LICENSES ARE FOR BUILD AND TEST DEPENDENCIES ONLY.` Both that complete
file and `ThirdPartyNotices.txt` are retained verbatim; no terms are rewritten or
removed, and this classification does not extend to other compiler versions or DXIL.

### DXIL scope

`dxil.dll` is not copied by `scripts/package.ps1`, explicitly loaded by BG3RTX, or present in
this worktree's `bin/` or `C:\VulkanSDK\1.4.357.0\Bin`. DXC contains a `dxil.dll` string;
that alone does not establish whether this SPIR-V path loads it. No runtime check was authorized.
Other installed Windows SDK / NVIDIA App copies are not the selected package dependency.
Do not add an arbitrary one. If a DXIL validator is added or shown to load, record its actual
path, version and hash and obtain its matching distribution notices, including any separate
Microsoft binary redistribution terms. Current DXC findings do not establish the licence of
those other `dxil.dll` copies.

## Launcher WebView2 and fonts

The launcher statically links Microsoft WebView2 SDK **1.0.4258.31** loader and
compiles its API headers from the local NuGet package; no SDK or browser runtime
is copied into `external/` or the player payload. The installed Evergreen runtime
is detected at startup; absent runtime uses native controls. Collected SDK
`LICENSE.txt` and `NOTICE.txt` are redistributed as the two WebView2 files below.
Source: `C:/Users/quali/.nuget/packages/microsoft.web.webview2/1.0.4258.31/`.
Their bytes are unchanged. This does not distribute the
Evergreen runtime or grant an automatic download/install action.

Bundled Lato fonts and full SIL OFL travel together in `bin/ui/fonts/`; their
version, authors, reserved name and SHA-256 identities are in its README/manifest.
No local OFL display serif was supplied: headings use the installed Georgia/serif
fallback, which is not redistributed. No Google Fonts or network font loading.

## Exact text provenance

Source paths are audit evidence on this machine, not player installation requirements.
All collected licence files are verbatim byte copies of their source files. SHA-256 values
below describe those exact bytes, including original line endings. Inventory prose uses LF.

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
| `DirectXShaderCompiler-sample-LICENSE.txt` | `C:\Users\quali\Documents\Code Projects\BG3RTX\external\DLSS\NVIDIAImageScaling\samples\third_party\DXC\license.txt` | Old DLSS NIS sample compiler only; NCSA/LLVM text, **not** the selected DXC 1.9 licence |

| Collected filename | SHA-256 (verbatim release copy) |
|---|---|
| `NVIDIA-RTX-SDKs-DLSS.txt` | `dc2778a3283427285984cdb5b3f7f03eae7d8a06057f672f2999c5ee7fd4f67d` |
| `Streamline-MIT.txt` | `7b6f23e7d6f3ad6292f9308d2b42cdc3d82ae4e9b2abb55f230279d83bedd43d` |
| `Streamline-3rd-party-licenses.md` | `cb251639994465f31d2178c16acedf1ff4f9cef1c370a09782ed36d4c86c1dca` |
| `NVIDIA-DLSS-runtime.txt` | `3027f23ca5a46dd9cb8183fbd522983a86f64d7daac5982912bf9f214671f294` |
| `NVIDIA-Reflex.txt` | `ebf83c07fb3b2939908c3795d887afde3161c89a28ba391724efc784ce1bdabe` |
| `NVIDIA-Image-Scaling-MIT.txt` | `f80ecfbce8a84a5b4c1c59dc5a9f0ee9cf5a989c9fba3a9486ee874f7595a454` |
| `NVIDIA-Aftermath.txt` | `606b69fb994b63e2122711694cde23a656bf861a8a27c2bab42acf5897230d4b` |
| `NVIDIA-Nsight-Graphics-Apache-2.0.txt` | `44595e5ff02a0f1f0fae737e63ecb6fb70a3e9afac4e8878f9b083565013b893` |
| `WebView2-LICENSE.txt` | `0af8f1b807512aae39c2ac1aa4d0cae65cabecb6fd554b8439a5162a0d6eca55` |
| `WebView2-NOTICE.txt` | `106423785c5b7eba0a8e61d1837f2132e9c828e20ad530f565d981c1df60dd90` |



| Collected filename | SHA-256 (verbatim release copy) |
|---|---|
| `DirectXShaderCompiler-sample-LICENSE.txt` | `88a72ac3a05a055a4344b32f87b5f2bfe1330710eeb8531068dc452c78402e03` |


### Version-matched upstream texts integrated on 2026-11-03

The coordinator supplied these official upstream downloads in `licences/upstream/`.
This integration moved them to the package filenames below without changing bytes;
no network was used during integration. The Vulkan `LICENSE.md` is renamed only for
package compatibility. Apache-2.0 is selected for the dual-licensed compiled headers;
the upstream overview, both full alternatives and their copyright notices are retained.

| Collected filename | Source URL | Commit / tag |
|---|---|---|
| `DirectXShaderCompiler-LICENSE.txt` | https://raw.githubusercontent.com/microsoft/DirectXShaderCompiler/a107ba613/LICENSE.TXT | DXC commit `a107ba613` |
| `DirectXShaderCompiler-ThirdPartyNotices.txt` | https://raw.githubusercontent.com/microsoft/DirectXShaderCompiler/a107ba613/ThirdPartyNotices.txt | DXC commit `a107ba613` |
| `Vulkan-Headers-LICENSE.txt` | https://raw.githubusercontent.com/KhronosGroup/Vulkan-Headers/v1.4.357/LICENSE.md | Vulkan-Headers tag `v1.4.357` |
| `Vulkan-Headers-Apache-2.0.txt` | https://raw.githubusercontent.com/KhronosGroup/Vulkan-Headers/v1.4.357/LICENSES/Apache-2.0.txt | Vulkan-Headers tag `v1.4.357` |
| `Vulkan-Headers-MIT.txt` | https://raw.githubusercontent.com/KhronosGroup/Vulkan-Headers/v1.4.357/LICENSES/MIT.txt | Vulkan-Headers tag `v1.4.357` |

| Collected filename | SHA-256 (verbatim release copy) |
|---|---|
| `DirectXShaderCompiler-LICENSE.txt` | `27a49e35d1da96eba18fba54bc882667ff0ff8c0254f16f2b6e165d605ba7df8` |
| `DirectXShaderCompiler-ThirdPartyNotices.txt` | `19512a5d0a015ef16d167272c164da49a604614a786206212a80ad486ed0be6d` |
| `Vulkan-Headers-LICENSE.txt` | `95ad366d23fadf701d355bc45fb8b82ae2d700239471d35d41286ac3b08ff903` |
| `Vulkan-Headers-Apache-2.0.txt` | `cfc7749b96f63bd31c3c42b5c471bf756814053e847c10f3eb003417bc523d30` |
| `Vulkan-Headers-MIT.txt` | `1ca3502222d967f3be5751c55f6b7ee735b5383909c3b501495f54b216dbf227` |

## Required notice files

This list is read by `scripts/package.ps1`. Every listed filename must exist under
`docs/release/licences/`; missing files stop packaging, including with `-NoBuild`.
The DXC and Vulkan files below are the version-matched upstream texts recorded above.
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
- `WebView2-LICENSE.txt`
- `WebView2-NOTICE.txt`
- `DirectXShaderCompiler-LICENSE.txt`
- `DirectXShaderCompiler-ThirdPartyNotices.txt`
- `Vulkan-Headers-LICENSE.txt`
- `Vulkan-Headers-Apache-2.0.txt`
- `Vulkan-Headers-MIT.txt`

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
  The package script copies this database. User decision 2026-11-04: the release ships the game-derived asset database (8 MB) at the user's own risk on game-data rights; this supersedes the 2026-11-02 plan to generate it on the player's PC.

## Packaging audit update (2026-11-04)

Stream C audited the current explicit payload using `-DryRun -OutDir` in worktree scratch.
This mode plans all runtime files, checks source availability and diffs planned destinations
against its files.txt; it copies no binaries or game data. Release-pack2 built the lean layer
with `scripts/build.ps1 -ReleaseLayer` at BelowNormal: diagnostics OFF, RelWithDebInfo.
The refreshed audit has 229 files, 0 missing inputs and 0 manifest differences, including
both project licence files at the root. This is not final archive acceptance. Fonts/OFL and branding
provenance remain included. No notices were altered and no distribution terms were approved.
The selected SDK DXC remains a packaging-machine source, not a player SDK requirement.

## Package dependency coverage (2026-11-03)

`scripts/package-third-party.json` owns the ten third-party DLL destinations and
notice dependencies, plus the six compiled SDK components. The packer reads this
manifest before any build/write and copies binaries only through it. Missing notice
files remain fatal; fixture-only texts in tests never enter the release collection.
All existing notice files ship under `licences/` and therefore enter generated `files.txt`.
`-ListOnly` and `-DryRun` validate collected notices;
`-DryRun -OutDir <scratch>` additionally audits the full payload without copying it.
This does not approve distribution.

| Component | Version | Licence | Packaged file | Local source path |
|---|---|---|---|---|
| WebView2 static loader / headers | 1.0.4258.31 | Microsoft WebView2 SDK terms and notice | `licences/WebView2-LICENSE.txt`, `licences/WebView2-NOTICE.txt` | `C:/Users/quali/.nuget/packages/microsoft.web.webview2/1.0.4258.31/LICENSE.txt`, `NOTICE.txt` |
| Lato Regular / Semibold | 2.015 | SIL OFL 1.1 | `bin/ui/fonts/OFL.txt` | Repository `assets/fonts/OFL.txt`; font provenance `assets/fonts/README.md` |
| Python and redistributable installers | Not shipped | Not applicable to payload | None | Python is a test/build tool only; no Python, pip, VC redist installer or runtime DLL in explicit payload |
| MSVC static CRT | Installed MSVC 14.50.35717; package does not pin toolchain | Microsoft toolchain terms | Linked into project binaries; installed Redist.txt is a URL pointer, not the redistribution grant | `C:/Program Files (x86)/Microsoft Visual Studio/18/BuildTools/Licenses/1033/Redist.txt`; static selection in `CMakeLists.txt`; grant review remains open |

The old sample DXC licence is retained verbatim as reference evidence only. It does
not satisfy `DirectXShaderCompiler-LICENSE.txt`. No unrelated MIT/Apache text was
substituted for Vulkan-Headers. SHaRC opt-in still copies its exact local `License.md`
separately; no ordinary package promotion or redistribution approval is implied.

## Missing

1. **DXC and Vulkan notice collection resolved (2026-11-03).** The five version-matched
   upstream files are present, hashed above and required by the package manifest.
   The old sample DXC licence remains reference evidence only.
2. **Package wiring implemented; release remains paused.** The script copies this
   inventory and every collected text. All five new texts are mandatory before
   builds or package writes. Project licence is GPL-3.0-or-later with the NVIDIA SDK exception
   (user decision 2026-11-04). NVIDIA notification, Microsoft runtime
   terms and game-data redistribution decisions remain open; see RELEASE_OFFLINE_STATUS.md.
3. **LSLib MIT text (conditional; not a player-package blocker while excluded).** If LSLib
   or dependent offline tools are distributed, obtain the exact v1.20.4 `LICENSE` from
   `https://github.com/Norbyte/lslib/blob/v1.20.4/LICENSE`, plus the separate notices/terms
   for every bundled dependency (MIT for LSLib does not cover every portable-release DLL).
   No LSLib executable, licence or matching NuGet/vcpkg package was found in the inspected locations.

Search coverage: main checkout `external/` (including nested sample SDKs), Vulkan SDK
1.4.357.0 (including hidden/ignored licence filenames), NuGet packages under
`C:\Users\quali\.nuget\packages`, all three local vcpkg cache archives under
`C:\Users\quali\AppData\Local\vcpkg\archives` (Eigen/vcpkg helpers only), Program Files LLVM
and NVIDIA SDK folders, Windows Kits, and read-only `D:\BG3_Tools\downloads`,
`external_backup` and `nsight-aftermath-sdk-2026.3`. No network was used in that local search or this integration; the coordinator later
supplied the five upstream files recorded above. Missing means
not found in these inspected locations, not proof that no copy exists anywhere on the computer.
