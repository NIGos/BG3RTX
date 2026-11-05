## Game upscaler guard: BG3RTX.exe puts the game's DLSS setting back (branch claude/confident-diffie-fb694f, READY_FOR_LIVE, 2026-11-05)

- Incident: after the RenoDX DLSS5 ReShade addon was installed, two player sessions logged
  `[ngx] no DLSS feature has been created through _nvngx.dll`. Cause confirmed offline: the game's
  `graphicSettings.lsx` (10:02) had lost `ResolutionUpscaleType` and `DLSS2Mode`; the 08:28 backup had 3 (DLSS)
  and 2. BG3 rewrites the file from defaults after a start where DLSS failed (the addon's first launches exited
  after 2-5 s), and every later start then runs with no upscaler.
- Not the cause: the game-folder `nvngx_dlss*.dll`. With fg=2 the game's NGX Init is stubbed (fg_game_ngx=2) and
  Streamline's NGX loads our `bin\sl` snippets (ReShade.log lists `bin\sl\nvngx_dlss.dll` and `nvngx_dlssd.dll`).
  The game-folder copies are NVIDIA-signed 310.9.16. A path-list hook of the game's NGX Init was not built: it
  matters only with fg=0, and the driver's Init exports take the static client's undocumented arguments.
- Fix: `game_dlss_guard` (bg3rtx.ini, default 1 since the user go of 2026-11-05; same rule as ensure-settings.py). With it at 1 and rr=1, BG3RTX.exe sets
  `ResolutionUpscaleType=3` before every game start, and `DLSS2Mode=2` only when absent (the player's DLSS mode is
  kept). It backs the file up to `graphicSettings.lsx.bg3rtx-bak` first, like `scripts/ensure-settings.py`.
  Launcher only: no layer, core, shader or ABI change.
- Tests: `launcher_launch_test` covers the reset file, idempotence, an FSR file, CRLF, an unknown file, and an
  end-to-end run on a temporary LOCALAPPDATA (off by default, rr=0, repair with backup).
- Live (not run, needs the user's go): start BG3RTX.exe with `game_dlss_guard=1` on a copy of the reset
  graphicSettings.lsx. Gate: the game creates DLSS (`[ngx] upscaler for feature ...: Ray Reconstruction`), no
  `no DLSS feature` line. Then decide whether the guard goes on in the presets.
