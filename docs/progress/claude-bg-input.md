## Background runs: the user's mouse no longer reaches the game (branch claude/bg-input, READY_FOR_LIVE, 2026-11-09)
- Cause: BG3 reads the mouse only through SDL2 2.30.6 window messages (no raw input, no relative mode;
  only SDL2.dll imports GetCursorPos/GetAsyncKeyState). Each runner-posted WM_MOUSEMOVE re-arms
  TrackMouseEvent; with the user's cursor outside the game (1066 visible client pixels in rv3/rv5),
  Windows answers with WM_MOUSELEAVE and SDL moves the game pointer to the REAL cursor and clears the
  mouse focus. With a still cursor (user away, rv3 .1.191231) the jumps cancel out; with the user
  moving it, the user's motion is added to the runner's drag and wheel (rv3 .1.203607, rv5 .1.204140:
  captured triangles flat at 1.3M in every phase vs 4.7M). Hover and inactive-window wheel also leaked.
- Fix (only BG3RTX_BACKGROUND=1 and the real foreground window belongs to another process): the runner
  marks every posted mouse message with wParam bit 0x0800 (`$BgMouseMark`); SDL2.dll's PeekMessageW/
  GetMessageW drop unmarked mouse messages and WM_MOUSELEAVE/HOVER (rewritten to WM_NULL), strip the
  mark from runner messages, and the game's SDL_GetGlobalMouseState returns the runner's last posted
  pointer and buttons. Keys untouched (real keys never reach an unfocused window). Game in front or
  normal play: unchanged (no hooks without the env var; mark stripped, nothing dropped when in front).
- Logs: `[bg] input filter on ... (peek 1 getmsg 1 global 1)`, `[bg] input filter active/idle`,
  every 10 s `[bg] dropped real input: mouse N leave N; runner mouse passed N; global polls answered N`.
- Test: CTest `bg_input_test` (classifier, button mask, every posted mouse message in auto-run.ps1 is marked).
- Next live (user at the PC, moving the mouse in another window): the wheel lowering and the rotation
  phases must change captured triangles as in the away run (about 4.7M, not flat 1.3M); `leave` grows
  with every posted move, `mouse` with the user's hover; the user's cursor never moves the camera.
- Not covered: SDL_WarpMouseInWindow still calls SetCursorPos if the game warps during a drag (not
  observed; would move the user's cursor, not the camera). A dropped leave leaves SDL's `mouse_tracked`
  set, so if the user later brings the game to the front SDL misses its mouse-leave (harmless in a test run).
