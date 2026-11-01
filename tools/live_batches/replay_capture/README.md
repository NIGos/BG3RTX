# Replay capture batch (live_batch replay-capture), NOT authorized

Lean format of [../lean_regression/README.md](../lean_regression/README.md): one foreground launch (the user must be away), FG 2, keys toggled inside the run, diagnostics only in `diag-*` windows. One job, no PNG traces.

Writer: `replay_pack` (layer/replay.cpp + core/gi.cpp, ABI 25; tools/frame_replay.md "Live writer"). The run needs a layer + core build with it (game restart). Pending live evidence: READY_FOR_LIVE once authorized.

Question: one real gi_hit_direct frame for the offline harness (`bin\frame_replay.exe --pack <dir>`), so a hit-shading change can be iterated in seconds instead of a live batch.

1. `rc1-replay-s8-fg2` (Water Queens House, S8: marble floor, water, columns, hidden hits; 35 s): warm-up 15 s with `gi_hit_direct=1`; `diag-replay-pack` 15 s with `rt_stats=1` and `replay_pack=1` (one pack at the first gi_hit_direct dispatch of the window; rt_stats gives the live lines of the same window); restore 5 s (`replay_pack=0`, `gi_hit_direct=0`). Startup as m4r1 S8 (`rt_hit_material=1`, `gi_rt=1`, `gi_rt_fallback=0`), `dump_images=0`.

## Gates
- Valid run: every expect line, including `[replay] pack 1 recorded at frame <f>: <n> image copies, <r> image reads (<b> BCn decoded, <MB> MB in <p> pieces), ...` with r >= 1 and `[replay] pack 1 written: <MB> MB (<n> images omitted, <r> read by shader), frame <f>, pass gi_hit_direct, instances <n> -> <dir>` and the live `GI hit direct` / `hit material` lines. Forbidden: `[replay] pack 1 failed` or `over budget`, `[replay] the image read shader ... did not build`.
- Size: `<MB>` at most 2048 (the hard cap, tools/frame_replay.md "Size"); report the omitted image count and the manifest's first notes (omitted per reason: no TRANSFER_SRC or SAMPLED, format, view, layout, size, budget; images copied / read by shader, BCn decoded and their MB; the omitted VkFormats; soup / pool whole or spans).
- Replay check (offline, after the run): `frame_replay --pack <dir>` must print `vs live giRaw: bit-equal ...`. The live GI uses this frame's pc.frame seed, so the same SPIR-V on the same GPU should give bit-equal giRaw where no omitted image is read; report the share and the mean |diff|, and the replay's `GI hit direct` and `hit material` lines next to the live 120-frame lines (per-frame rates, not equal counts). With the textures read, the replay's `hit material` must show qualified > 0 and the `sampling` reason near 0 (the first pack, without them: qualified 0, sampling 125705 of 189768); its qualified share should approach the live one (live 48%: 8409465 of 17355319).
- Cost: capture only (one wait after the capture frame); no rendering change, no A/B.

Duration: plan 35 s; one launch, about 3 min (5 min timeout). Disk: the pack (at most 2 GB, the hard cap) plus logs, about 2 GB; delete the pack's raw files after the offline replay, keep its manifest and replay stats.
Start, results and rollback: as in ../lean_regression/README.md with this folder, prefix `rc1` and receipt `replay-capture.json`. Needs a layer + core build with the writer (game restart).

Offline check: `powershell -File tools\live_batches\replay_capture\dryrun.ps1 [-Out <scratch dir>]`. Last result 2026-11-01: all checks passed.
