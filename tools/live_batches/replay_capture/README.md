# Replay capture batch (live_batch replay-capture), NOT authorized

Lean format of [../lean_regression/README.md](../lean_regression/README.md): one foreground launch (the user must be away), FG 2, keys toggled inside the run, diagnostics only in `diag-*` windows. One job, no PNG traces.

Writer: `replay_pack` (layer/replay.cpp + core/gi.cpp, ABI 25; tools/frame_replay.md "Live writer"). The run needs a layer + core build with it (game restart). Pending live evidence: READY_FOR_LIVE once authorized.

Question: one real gi_hit_direct frame for the offline harness (`bin\frame_replay.exe --pack <dir>`), so a hit-shading change can be iterated in seconds instead of a live batch.

1. `rc1-replay-s8-fg2` (Water Queens House, S8: marble floor, water, columns, hidden hits; 35 s): warm-up 15 s with `gi_hit_direct=1`; `diag-replay-pack` 15 s with `rt_stats=1` and `replay_pack=1` (one pack at the first gi_hit_direct dispatch of the window; rt_stats gives the live lines of the same window); restore 5 s (`replay_pack=0`, `gi_hit_direct=0`). Startup as m4r1 S8 (`rt_hit_material=1`, `gi_rt=1`, `gi_rt_fallback=0`), `dump_images=0`, and `replay_pack_cap_mb=3300` (one-off larger cap, user-approved 2026-11-01: the 071930 pack measured 909 MB without textures and the hit cache s1b3+b14[1] at 2053.7 MB, BC3 sRGB 32688 x 26352 x 2 layers, 2 mips; 976 + 2054 MB = 3.03 GB, so 3300 MB keeps every image with 270 MB to spare).

## Gates
- Valid run: every expect line and the live `GI hit direct` / `hit material` lines. Forbidden: `[replay] pack 1 failed` or `over budget`, `[replay] the image read shader ... did not build`. Derived minimums (S8 packs of 2026-11-01, tools/frame_replay.md "Size"): every real texture there has TRANSFER_SRC and is a raw copy, and only the layer's two 4 x 4 dummies (2D, array) are read by the shader, so:
  - `recorded at frame <f>: <n> image copies, <r> image reads ...`: n >= 27 (7 set-0 images and outputs + the 20 table images the first pack copied), r >= 2;
  - `written: <MB> MB (<n> images omitted, <r> read by shader) ...`: r >= 2 (not >= 100: that would need game textures without TRANSFER_SRC, which S8 does not have);
  - `textures: <MB> MB free under the 3300.0 MB cap for <n> droppable images of <MB> MB; kept ...; by binding ...: s1b2 <k>/<k> ...; s1b3+b14 2/2 ...`: every table image and both cache images kept (the 2054 MB hit cache included).
- Lighting inputs (sets 2-3, tools/frame_replay.md "Lighting inputs"): `lighting inputs: 19 of 19 slots bound; probe cubes <c> named by <r> records (...), <k> of <e> bound cube elements captured, ...; images kept <n>, ...; <MB> MB`, with c, r, k >= 1 and at least one declared sampler (set 3 b6). 19 of 19 is derived: every source binding is one the tiled lighting shader declares, so Vulkan requires it bound; S8 lists local probes at every shaded hit, so its records name cubes. The `textures` line must also show `s2b34(+b36) 1/1`, `s2b38 1/1`, `s3b1 1/1` (sky irradiance, DFG LUT, transmittance LUT kept: priority 4, before the hit cache). Probe cubes rank below the hit cache (priority 2) and take what the cap leaves: report their kept/dropped count and MB; dropped probe cubes are not a failure.
- Native ambient (offline, after the run; informational, not a gate): the post step runs `frame_replay --pack <dir> --native-ambient` and prints `native ambient means ...`, `PT far field ...`, `native vs PT far field: ...`, `per tile (16 x 9 ...)`, `sun transmittance: game LUT ...`. That is the answer to whether the native far-field term is too bright (ratio > 1) or too dark (< 1) at S8.
- Size: `<MB>` at most 3300 (`replay_pack_cap_mb`; about 3030 expected). Report the `textures` line (free MB, MB kept and dropped per binding, the 10 largest with format, size, layers, mips, copy or read) and any `two views of one image (omitted)` note: they are the measured VT cache sizes. Expected: about 976 + 2054 MB, nothing dropped by the budget.
- Replay check (offline, after the run): `frame_replay --pack <dir>` must print `vs live giRaw: bit-equal ...` at least the 94.9% of the second pack (055812, no textures); report the share, the mean |diff| and the replay's `GI hit direct` / `hit material` lines next to the live 120-frame lines (per-frame rates). Hit material: `sampling` must not rise above the second pack's 125767; qualified > 0 only when every cache a row names was kept, which the textures line says beforehand. With b14[1] kept: qualified > 0 and sampling (reason 6) at most the live rate (0 per frame at S8). The 071930 pack (hit cache dropped) gave qualified 0, sampling 124543 of 188603 attempted, bit-equal 94.99% (2026-11-01, chunked loader).
- Cost: capture only (one wait after the capture frame); no rendering change, no A/B.

Duration: plan 35 s; one launch, about 3 min (5 min timeout). Disk: the pack (at most 3300 MB, the job's cap; about 3.03 GB) plus logs, about 3.3 GB; the post step then frees the older packs.

## Keep only the newest pack (per scene)
Camp and inn packs with the cascade casters: [ambient/README.md](ambient/README.md) (`post_replay.ps1 -Scene`). Since
2026-11-01 the step deletes only older packs of the same scene tag (`scene.txt`; this job's packs are untagged), so
the S8 pack and the camp / inn packs do not delete each other.
The job's `post` step (`post_replay.ps1 -Log {log}`, run by the queue in the test tree after the run) does it:
1. finds this run's `[replay] pack 1 written: ... -> <dir>` (none: deletes nothing);
2. runs `bin\frame_replay.exe --pack <dir>` (output `<dir>\replay`, console in `replay.txt`);
3. the offline gate decides: the replay's `hit material (1 frames)` line has qualified > 0 and sampling at most the live rate (the ceiling of the live 120-frame lines' sampling per frame);
4. gate passed: every older `captures\replay-*` folder of that tree is deleted (file by file, then the empty folders; never a recursive delete; a folder with a reparse point or another subfolder is kept and named). Gate failed: nothing is deleted and the line says why; then the next step is a decision (keep the new pack for diagnosis or delete it by hand), not a rerun.
Its last lines are in the run's results. Offline check of the script: scratch tree with fake older packs, a synthetic pack (gate passes, two older packs deleted, a folder with an extra subfolder kept) and the real 071930 pack (gate fails, nothing deleted), 2026-11-01. The script must be in the test tree (merge to main first).
Start, results and rollback: as in ../lean_regression/README.md with this folder, prefix `rc1` and receipt `replay-capture.json`. Needs a layer + core build with the writer (game restart).

Offline check: `powershell -File tools\live_batches\replay_capture\dryrun.ps1 [-Out <scratch dir>]`. Last result 2026-11-01 (replay_pack_cap_mb=3300, s1b3+b14 2/2 expect, lighting inputs expect, post step with --native-ambient): all checks passed (estimate 2.6 min, 3310 MB). The post step's native-ambient part was checked offline on the synthetic packs (-DryRun, scratch tree): a pack without lighting inputs prints "native ambient not compared: ... no lighting inputs" and the gate is unchanged; the synthetic ambient pack prints the five lines.
