## build.ps1 prefers VS 2022 (branch build-pin-vs2022, 2026-11-07)

- VS Build Tools 2026 was installed next to VS 2022 on 2026-11-07 13:14; `vswhere -latest` then picked its
  environment while the build trees cache the 14.44 compiler, so every compile failed with STL1001.
- `scripts/build.ps1` now asks vswhere for `[17.0,18.0)` first and falls back to the latest only without VS 2022.
- Moving to the VS 2026 toolset stays a separate step (clean build trees, full RC verification).
- Check: a forced launcher recompile from a plain shell builds cleanly (no STL1001).
