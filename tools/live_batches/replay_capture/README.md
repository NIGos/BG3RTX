# Replay capture batch (live_batch replay-capture), NOT authorized, BLOCKED on the writer

Lean format of [../lean_regression/README.md](../lean_regression/README.md): one foreground launch (the user must be away), FG 2, keys toggled inside the run, diagnostics only in `diag-*` windows. One job, no PNG traces.

Blocked: the `replay_pack` writer is not built yet (design in [../../frame_replay.md](../../frame_replay.md), "Live writer"). Until it is, the `[replay] pack 1 written` expect line cannot appear and the run is INVALID by design. Do not queue it before that branch is merged.

Question: one real gi_hit_direct frame for the offline harness (`bin\frame_replay.exe --pack <dir>`), so a hit-shading change can be iterated in seconds instead of a live batch.

1. `rc1-replay-s8-fg2` (Water Queens House, S8: marble floor, water, columns, hidden hits; 35 s): warm-up 15 s with `gi_hit_direct=1`; `diag-replay-pack` 15 s with `rt_stats=1` and `replay_pack=1` (one pack at the first gi_hit_direct dispatch of the window; rt_stats gives the live lines of the same window); restore 5 s (`replay_pack=0`, `gi_hit_direct=0`). Startup as m4r1 S8 (`rt_hit_material=1`, `gi_rt=1`, `gi_rt_fallback=0`), `dump_images=0`.

## Gates
- Valid run: every expect line, including `[replay] pack 1 written: <dir>, <bytes> bytes (<n> images omitted), frame <f>, pass gi_hit_direct, instances <n>` and the live `GI hit direct` / `hit material` lines. Forbidden: `[replay] pack 1 failed` or `over budget`.
- Size: `<bytes>` below 1 GB (tools/frame_replay.md "Size"); report the omitted image count.
- Replay check (offline, after the run): `frame_replay --pack <dir>` must print `vs live giRaw: bit-equal ...`. The live GI uses this frame's pc.frame seed, so the same SPIR-V on the same GPU should give bit-equal giRaw where no omitted image is read; report the share and the mean |diff|, and the replay's `GI hit direct` and `hit material` lines next to the live 120-frame lines (per-frame rates, not equal counts).
- Cost: capture only (one wait after the capture frame); no rendering change, no A/B.

Duration: plan 35 s; one launch, about 3 min (5 min timeout). Disk: the pack (< 1 GB) plus logs, about 1 GB.
Start, results and rollback: as in ../lean_regression/README.md with this folder, prefix `rc1` and receipt `replay-capture.json`. Needs a layer + core build with the writer (game restart).

Offline check: `powershell -File tools\live_batches\replay_capture\dryrun.ps1 [-Out <scratch dir>]`. Last result 2026-11-01: all checks passed.
