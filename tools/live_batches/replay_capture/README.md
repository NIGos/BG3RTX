# Replay capture batch (live_batch replay-capture), NOT authorized

Lean format of [../lean_regression/README.md](../lean_regression/README.md): one foreground launch (the user must be away), FG 2, keys toggled inside the run, diagnostics only in `diag-*` windows. One job, no PNG traces.

Writer: `replay_pack` (layer/replay.cpp + core/gi.cpp, ABI 25; tools/frame_replay.md "Live writer"). The run needs a layer + core build with it (game restart). Pending live evidence: READY_FOR_LIVE once authorized.

Question: one real gi_hit_direct frame for the offline harness (`bin\frame_replay.exe --pack <dir>`), so a hit-shading change can be iterated in seconds instead of a live batch.

1. `rc1-replay-s8-fg2` (Water Queens House, S8: marble floor, water, columns, hidden hits; 35 s): warm-up 15 s with `gi_hit_direct=1`; `diag-replay-pack` 15 s with `rt_stats=1` and `replay_pack=1` (one pack at the first gi_hit_direct dispatch of the window; rt_stats gives the live lines of the same window); restore 5 s (`replay_pack=0`, `gi_hit_direct=0`). Startup as m4r1 S8 (`rt_hit_material=1`, `gi_rt=1`, `gi_rt_fallback=0`), `dump_images=0`.

## Gates
- Valid run: every expect line and the live `GI hit direct` / `hit material` lines. Forbidden: `[replay] pack 1 failed` or `over budget`, `[replay] the image read shader ... did not build`. Derived minimums (S8 packs of 2026-11-01, tools/frame_replay.md "Size"): every real texture there has TRANSFER_SRC and is a raw copy, and only the layer's two 4 x 4 dummies (2D, array) are read by the shader, so:
  - `recorded at frame <f>: <n> image copies, <r> image reads ...`: n >= 27 (7 set-0 images and outputs + the 20 table images the first pack copied), r >= 2;
  - `written: <MB> MB (<n> images omitted, <r> read by shader) ...`: r >= 2 (not >= 100: that would need game textures without TRANSFER_SRC, which S8 does not have);
  - `textures: <MB> MB free under the cap for <n> droppable images of <MB> MB; kept ...; by binding ...: s1b2 <k>/<k> ...`: every table image kept (the fill keeps the small ones whatever a cache does).
- Size: `<MB>` at most 2048 (the hard cap). Report the `textures` line (free MB, MB kept and dropped per binding, the 10 largest with format, size, layers, mips, copy or read) and any `two views of one image (omitted)` note: they are the measured VT cache sizes. Expected: about 906 + 67 MB + the alpha cache b3[1] if it fits; the hit cache b14[1] (more than 1142 MB) dropped.
- Replay check (offline, after the run): `frame_replay --pack <dir>` must print `vs live giRaw: bit-equal ...` at least the 94.9% of the second pack (055812, no textures); report the share, the mean |diff| and the replay's `GI hit direct` / `hit material` lines next to the live 120-frame lines (per-frame rates). Hit material: `sampling` must not rise above the second pack's 125767; qualified > 0 only when every cache a row names was kept, which the textures line says beforehand. If b14[1] is dropped, the next step is a decision (a larger cap for this one job, or a tile capture of the pages the hits sample), not a rerun.
- Cost: capture only (one wait after the capture frame); no rendering change, no A/B.

Duration: plan 35 s; one launch, about 3 min (5 min timeout). Disk: the pack (at most 2 GB, the hard cap) plus logs, about 2 GB; delete the pack's raw files after the offline replay, keep its manifest and replay stats.
Start, results and rollback: as in ../lean_regression/README.md with this folder, prefix `rc1` and receipt `replay-capture.json`. Needs a layer + core build with the writer (game restart).

Offline check: `powershell -File tools\live_batches\replay_capture\dryrun.ps1 [-Out <scratch dir>]`. Last result 2026-11-01 (texture fill, new expect lines): all checks passed.
