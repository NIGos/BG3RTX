# BG3RTX

Research framework for adding ray-traced lighting, DLSS Ray Reconstruction and
real DLSS frame generation to Baldur's Gate 3 (Vulkan renderer, `bg3.exe`).

It is a Vulkan layer that runs from this repository and never modifies the game folder
or the registry. Inside the layer, a core DLL and HLSL shaders reload while the
game is running.

## Quick start

```powershell
.\scripts\build.ps1                 # first build (MSVC + Ninja + Vulkan SDK)
.\scripts\launch-bg3.ps1            # start bg3.exe with the layer enabled
```

Use the **Vulkan** renderer (`bg3.exe`). The DX11 executable doesn't go through Vulkan layers.

| Do this | Effect |
|---|---|
| `Ctrl+Shift+F10` in game, or create an empty `trace.request` file in the repo root | records one frame to `captures/<stamp>/frame.json` |
| `py scripts\analyze_trace.py --disasm` | writes `report.md` and `graph.dot` and decompiles the frame's shaders |
| `.\scripts\build.ps1 -Watch` | rebuilds the core on every save; the game swaps it in at the next present |
| edit anything under `shaders\` | the shader recompiles and its pipeline is rebuilt in game |
| edit `bg3rtx.ini` | re-read within about a second |
| `.\scripts\auto-run.ps1` | unattended loop: build, launch on the latest save, auto-capture, close, analyse (~1 min). Headless by default (display can be off); `-RealPresent` for real presentation, `-Move` to pan the camera during the capture, `-Set key=value` for ini overrides |
| check `bg3rtx.log` | output from the host, core and shader compiler; it can be read while the game runs |

To check that the layer works without starting BG3, run any Vulkan program:
`.\scripts\launch-bg3.ps1 -Exe "$env:VULKAN_SDK\Bin\vkcube.exe"`.

See [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) and [docs/ROADMAP.md](docs/ROADMAP.md).
