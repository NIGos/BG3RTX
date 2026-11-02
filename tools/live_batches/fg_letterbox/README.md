# fg_letterbox: FG in a swapchain larger than the game's output (READY_FOR_LIVE, not authorized)

One job, `fl1-ultra-s8-borderless-fg2`, S8 Water Queens House, one launch, 5 phases, 75 s (about 3 min with the load).
It is the live check before `fg_letterbox` defaults to 1 (commit 2c448ee, held by the coordinator).

- Display: `-Borderless` (scripts/auto-run.ps1, this job only). Real-present runs are otherwise windowed
  (`Enter-BackgroundDisplay -Windowed`, FakeFullscreenEnabled=0). With it the game runs borderless at the
  3840x2160 desktop with its 3840x1600 output, so the composite pass is 3840x2160 (the gf1 141602 case). The
  player's display values are restored after the run as for every background run. The game covers the screen
  and takes focus (`-Foreground`): the user must not be at the PC.
- The tree that runs it must contain the `-Borderless` switch (this branch); without it auto-run.ps1 rejects
  the argument.
- Launch keys: the smoke_ultra set (preset ultra, fg=2, the qualified experimental keys), `fg_letterbox=1`,
  `dump_images=0`, traces 0.
- Phases (live key set by the plan): warmup (1), on-a (1, gated), off (`fg_letterbox=0`), settle (1, 5 s, ungated:
  FG cooldown after the toggle), on-b (1, gated).
- Expect: `[sl] fg_letterbox=1: UI composite pass 3840x2160`, `[sl] game output 3840x1600 in a 3840x2160 swapchain
  pass (fg_letterbox): backbuffer subrect`, the `fg_letterbox=0` toggle line.
- Every-window gates: DLSS-G `status 0x0`, displayed/rendered `2.00`, `api errors 0 (last 0)` in on-a and on-b;
  RT capture drops 0, cascade buffer full 0 (when present) and scene-wait 0 timeouts in on-a, off and on-b.
- Not gated: FG in `off` (bindings found in on-a may persist). Not covered: UI ghosting at the bars (no PNG).
- Prediction: `predicted.json` (FG ratio 2.00 in on-a and on-b).
- Offline: `powershell -NoProfile -ExecutionPolicy Bypass -File tools\live_batches\fg_letterbox\dryrun.ps1`.
