# PAUSED: game-image lifetime fix (VUID-vkDestroyImage-image-01000)

## Done and verified
- **Root cause.** The game's lifetime is fine: it destroys each image about 4
  frames after its last use. With `life_log=1`, our present fence for that frame
  had always signalled by then. The cause was in `layer/rrpost.cpp`: each step
  wrote only the bindings it uses into a ring of 256 descriptor sets. A recycled
  set kept stale views of game images in its other bindings. A pending command
  buffer that bound such a set made the validation layer report 01000. By the
  spec this is also a real command-buffer invalidation, even though the GPU never
  read those bindings.
- **Fix 1** (`rrpost.cpp` `run()`): every binding is written on every use.
  Bindings a step doesn't use get fillers of ours (2D image, 3D image, 256 B
  buffer).
- **Fix 2, safety net** (`tracker.cpp` `life_*`, plus `cmd.cpp`, `layer.cpp`,
  `host.h`):
  - Each recording notes the game objects it references. These are folded in
    at submit, and every present adds a fence on the present queue.
  - The image, view and buffer destroy hooks wait (bounded at 2 s) for the
    fence that covers the object's last submission.
  - The device-idle hook retires everything submitted before it.
  - Logs: `[life]`; set `life_log=1` for per-destroy decisions.
- **Fix 3** (`streamline.cpp`): `sl_note_view_destroyed`. When the game destroys
  a current DLSS-G tag's view, the code waits on DLSS-G's inputs fence. The game
  destroys the view before the image.
- **Validation** (`captures/life_baseline` vs `captures/life_fix2`, foundry,
  Water Queens House, Grymforge): `0xf2d29b5a` is gone in all three scenes. The
  remaining IDs match the baseline exactly; there are no new IDs.
- **Stall:** 0 ms. The safety net never had to wait in 3 validation runs and 2
  `-Move` runs, and no destroy was "unfenced". Bookkeeping costs 0.011 to
  0.016 ms CPU per frame plus one empty submit per present.
- **`-Move`:** 2 runs of 421 s each, foundry, panning. No exits, no
  `evaluate failed`, no faults (`captures/life_move`).
- **Earlier "unexplained exits":** there are no bg3 Application Error events in
  the last 2 days. Each launch logs a pair of nvlddmkm 153 events, which look
  benign.
- Docs updated: `docs/TRACK_A_RR.md` ("Lifetime of the game's images",
  robustness, limits) and the `docs/QUALITY.md` RR-before-post row.

## In progress / not verified
- The third `-Move` run was cancelled by the pause.
- Non-finite values were not re-checked on this build: the `-Move` runs timed
  out before taking their 3 traces. The trace directories were pruned.
- After the docs were written, 2 `-Move` runs were done, not 3. Update the
  TRACK_A "Stall cost" line, which says "3 `-Move` runs".

## Next steps
1. One or two more `-Move` runs (`scripts/auto-run.ps1 -Move -Background -NoBuild`),
   for example `-Traces 1`, so a trace is taken. Then check its non-finite
   count in the analyze output. Investigate any exit (Application log,
   `bg3rtx.log` tail).
2. Fix the run count in TRACK_A "Stall cost" (2 or the new total).
3. Report the leftover risk to the alphatest owner: the RT passes accept a
   scene 1 frame old, so they can bind the previous alpha set's game texture
   views. `alphatest.cpp` should drop the views of destroyed images.
4. Squash or retitle the WIP commit and remove this file.
