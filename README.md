# BG3RTX

Research framework for adding ray-traced lighting, DLSS Ray Reconstruction and
real DLSS frame generation to Baldur's Gate 3 (Vulkan renderer, `bg3.exe`).

It is a Vulkan layer that runs from this repository and never modifies the game folder
or the registry. Inside the layer, a core DLL and HLSL shaders reload while the
game is running.

## Players

Use the packaged README and installer for installation and play. The source copy is
[docs/release/README.md](docs/release/README.md). It covers **Install.cmd**, the launcher,
requirements and support. See [the settings guide](docs/USER_SETTINGS.md) for player controls.

## Developers

Build from a PowerShell development environment with MSVC, Ninja and the Vulkan SDK:

```powershell
.\scripts\build.ps1 -Test          # build and run CTests
```

Start with [the session handoff](docs/HANDOFF_SESSION.md) and
[the release report](docs/PROGRESS_CODEX.md). Read [the architecture](docs/ARCHITECTURE.md)
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
