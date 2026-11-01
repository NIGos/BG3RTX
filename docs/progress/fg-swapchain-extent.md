## FG inputs when the swapchain is larger than the game's output (branch fg-swapchain-extent, READY_FOR_LIVE, 2026-11-01)

- Bug: borderless / fake fullscreen on a 3840x2160 desktop with the game at 3840x1600 never got FG. The UI composite
  draws into the 3840x2160 swapchain and its hudless/UI images are 3840x1600; sl_note_draw matched them by the pass
  extent, so no input was found (gf1 141602: no "[sl] hudless" line, "FG decision ... missing 0xf", displayed 1.00).
- Fix `fg_letterbox=0/1` (live key, layer, default 0): the game's output extent is the pass extent, else the largest
  sampled image inside the pass covering more than a quarter of it (common/fg_extent.h); hudless/UI matched at it;
  the backbuffer tag gets the subrect where the output lands (the composite viewport, else centred) and logs
  "[sl] game output WxH in a WxH swapchain pass". Equal sizes: unchanged. The PQ UI record stays equal-size only.
- Test: fg_extent_test (extent choice, letterbox/pillarbox/scaled, viewport/centred subrect). build -Test passes.
- Live check needed: a 3840x2160 desktop run with fg_letterbox=1 (FG decision want 2, displayed/rendered 2.00, no UI
  ghosting at the letterbox bars). Then default 1.
