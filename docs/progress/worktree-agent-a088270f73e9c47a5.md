## D-EM projected check: device-local counters, sliced, timed (branch worktree-agent-a088270f73e9c47a5, offline, READY_FOR_LIVE, 2026-11-01)

- Finding (inferred from m4p3-s6-fg2 112657): `rt_emissive` 0 -> 1 raised the 300-frame average `gi: GPU cost`
  1.65 -> 10.4 ms with flat pass medians: one frame in 16 about 140 ms more. That frame ran the calibration's
  projected check (em_debug.hlsl mode 0, every `rt_em_check_frames=16`): up to 3 emissive ray queries per pixel
  plus 4-14 atomics per pixel into the host-visible `emStats` (PCIe). A periodic hitch for anyone with rt_emissive=1.
- Not a pure diagnostic: `rt_em_calib` scales the approximate families from it, and `rt_hit_emission` skips an
  approximate family until it is calibrated. So it stays on by default, made cheap instead of turned off.
- Change (core + em_debug.hlsl, no ABI, no layer change): counters in a core device-local buffer, reduced per
  workgroup and per wave/record, then a `flush` entry adds the non-zero words into `emStats` (plain stores; the
  host's layout and reads are unchanged). New key `rt_em_check_slices=16`: one interleaved 4x4 slice per frame,
  every pixel still once per 16 frames; 1 = the old full-screen run. The calibration gates count full checks
  (`em::calib_checks`, `calib_px_per_check`, unit-tested); `scripts/qa/em_gate.py` scales N the same way.
- New line per rt_timing_frames: `[em] check GPU cost (N frames): R runs (V views), X ms per run median (check
  pass, max, timed), Y ms/frame; P pixels per run; schedule ...`. The cost is still inside `gi: GPU cost`.
- Expected: per run well under 1 ms (1/16 of the rays, no PCIe atomics), no spike. Not measured yet.
- host_atomics_lint: the D-EM stats entry left the host-visible baseline (14 -> 13 known); `emDev` is device.
- Shaders: only em_debug.hlsl changed (rt_emissive=1 only); every other shader is untouched, so byte-identical.
- Live check (em_forge `ef1-af-em-fg2`, +36 s): phases check-off / check-full (slices 1) / check-sliced (16),
  12 s each; dry run passed. Gate: check-sliced max per run < 1 ms; `gi: GPU cost` mean in check-sliced within
  the check-off spread plus the logged ms/frame; approx families still show `scale` != 1.000.
- Note for readers of the layer's `[em] projected check` / `ours/game per family`: "frames" and bright px per
  frame are now per run (1/16 of the pixels). Docs: TRACK_D_EMISSIVE.md 13.24.
