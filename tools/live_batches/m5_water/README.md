# M5 water live batch (live_batch m5-water), NOT authorized

Lean format of [../lean_regression/README.md](../lean_regression/README.md): one foreground launch (the game takes focus and the pointer: the user must be away), FG 2, the key toggled inside the run, 20 s diag-off `cost-*` windows, one PNG per 8 s `shot-*` / `diag-view-*` phase, no float bursts. Receipt `m5-water.json`; the plan goes to test-main `captures\live_batches\m5_water\plans`.

Done: `m5w1-water-geometry-s1-fg2` (M5.1 `rt_water_geometry`, branch m5-water) ran at the S1 docks on 2026-11-01: 99% of water draws captured, 1.45M triangles, build 0.27 ms, depth write off. Its files are in `done/` (not part of the batch any more).

1. `m5w2-water-reflections-s1-fg2` (QuickSave_3784, S1 docks, 140 s): [PLAN_M5](../../../docs/PLAN_M5_WATER.md) M5.2 `rt_water_reflections` (branch m5-water-rf), with `rt_water_geometry=1` in every phase but `restore` (the A/B isolates the reflection pass). Warm-up OFF, then `precheck-rf-on` (15 s, key on): its `[water] reflections` expect line needs traced frames and a water pixel share above 0.00%, so a view without the docks water is INVALID, never a pass. Then OFF-a / ON / OFF-b, each a cost window plus a PNG, and two debug-view PNGs over the water: `diag-view-reflect` (`rt_debug=22`, the reflection radiance where the screen resolves it, magenta sky misses, dark blue other unresolved rays) and `diag-view-coverage` (`rt_debug=21`: green water in front of the bed, blue over an empty G-buffer, yellow at the bed, red behind it).

## Gates (read the deciding numbers in the log)
- Valid run: all expect lines, no forbid line, RR create Success, every `[sl] DLSS-G` line after warm-up `presented per app frame 2 ... 2.00`, `TLAS ... none 0`. `[water] capture` still `supported draws` > 0 with `failed 0`, `overflow 0` (the M5.1 gates).
- Image: the key is measured, not composed, so `shot-on` must equal `shot-off-a` within the `shot-off-b` minus `shot-off-a` spread (mean and max per channel; QA detectors as in lean_regression). Any larger difference is a FAIL.
- Coverage: `behind bed 0` (visible water cannot be behind the bed it covers; the tolerance is derived: depth_tol at the bed's normal over the DLSS jitter plus kTlasRel). `water px` against the forward pass's changed-pixel share of COVERAGE gap 1 (89.2% of S1 changed by the water draw: review, not tuned). `shift on` (the build camera and the lighting camera were both known).
- Reflections, reported (no pass threshold yet: the first measurement of the observable): `screen hit`, `hidden`, `off screen`, `miss` shares of the rays, `aborted` (expect 0), `invalid 0`, `normal fallback`, `mean E` (Schlick with F0 0.0196 at the docks: grazing views raise it), `mean screen L`, `mean E*L`. The `diag-view-reflect` PNG must show the docks / shore reflected the right way up on the water (a wrong normal or shift shows as noise or a mirrored offset); `diag-view-coverage` mostly green on the water, no red.
- Cost: `[water] reflections GPU` median and p90 (the two dispatches); cost-on minus cost-off frame time and `rt passes: GPU cost` against the cost-off-a / cost-off-b spread. Quality before frame rate: report, no budget gate.

Not in this batch: composition into the forward water result (M5.3, plan in PLAN_M5 M5.2 "Composition plan"); motion (one moving run with the key toggled comes with M5.3, where the shift and the one-frame water latency matter to the image).

Duration: plan 140 s; one launch, about 5 min (at most 8 min of timeouts). Disk: 5 PNG traces at about 60 MB plus logs, about 0.3 GB.
Start, results and rollback: as in ../lean_regression/README.md with this folder, prefix `m5w` and receipt `m5-water.json` (test-main must be the m5-water-rf commit under test).

Offline check: `powershell -File tools\live_batches\m5_water\dryrun.ps1 [-Out <scratch dir>]`. Last result 2026-11-01 (m5w2, after merging main 3e08c03): all checks passed.
