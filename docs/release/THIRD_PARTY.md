# Third-party components in BG3RTX

BG3RTX includes the following third-party software, unmodified. Their licence texts are in `LICENSES\`.

| Component | Files | Version | Licence | Text |
|---|---|---|---|---|
| NVIDIA DLSS Super Resolution and Ray Reconstruction | `bin\sl\nvngx_dlss.dll`, `bin\sl\nvngx_dlssd.dll` | from Streamline SDK 2.14.16 | NVIDIA RTX SDKs License | `LICENSES\NVIDIA-RTX-SDKs-DLSS.txt` |
| NVIDIA DLSS Frame Generation | `bin\sl\nvngx_dlssg.dll`, `bin\sl\sl.dlss_g.dll` | Streamline SDK 2.14.16 | NVIDIA RTX SDKs License; Streamline plugin MIT | both texts |
| NVIDIA Streamline | `bin\sl\sl.interposer.dll`, `sl.common.dll`, `sl.reflex.dll`, `sl.pcl.dll` | 2.14.16 | MIT | `LICENSES\Streamline-MIT.txt`, `LICENSES\Streamline-3rd-party-licenses.md` |
| NVIDIA Reflex runtime | `bin\sl\NvLowLatencyVk.dll` | Streamline SDK 2.14.16 | distributed with Streamline | `LICENSES\Streamline-MIT.txt` |
| NVIDIA DLSS / NGX SDK headers | compiled into `bin\VkLayer_bg3rtx.dll` | DLSS SDK | NVIDIA RTX SDKs License | `LICENSES\NVIDIA-RTX-SDKs-DLSS.txt` |
| Microsoft DirectX Shader Compiler | `bin\dxcompiler.dll` | Vulkan SDK 1.4.357.0 | University of Illinois/NCSA Open Source License | `LICENSES\DirectXShaderCompiler-LICENSE.txt` |
| Khronos Vulkan headers | compiled into the DLLs | Vulkan SDK 1.4.357.0 | Apache-2.0 OR MIT | `LICENSES\Vulkan-Headers-LICENSE.txt` |

The NVIDIA software is NVIDIA's property and is provided under NVIDIA's licence terms: you may not reverse
engineer, decompile or disassemble it, or remove its notices. DLSS works only on NVIDIA RTX GPUs.

NVIDIA, GeForce RTX, DLSS and Reflex are trademarks of NVIDIA Corporation. Baldur's Gate 3 is a trademark of
Larian Studios. BG3RTX is not made, endorsed or supported by NVIDIA or Larian Studios.

`data\assetdb\bg3rtx_assetdb.bin` holds lighting and material data derived from Baldur's Gate 3's own game files.
It works only with the game and is part of BG3RTX as a mod for it.
