# M5 water live batch (live_batch m5-water), NOT authorized

Lean format of [../lean_regression/README.md](../lean_regression/README.md): one foreground launch (the game takes focus and the pointer: the user must be away), FG 2, the key toggled inside the run, 20 s diag-off `cost-*` windows, one PNG per 8 s `shot-*` phase, no float bursts, no diagnostics needed (the `[water]` lines are logged every `rt_timing_frames`=120 whenever the key is on). Receipt `m5-water.json`; the plan goes to test-main `captures\live_batches\m5_water\plans`. Replaces the `mm2` draft of [../m4_m5_first](../m4_m5_first/README.md) (its log contract predates the implementation).
1. `m5w1-water-geometry-s1-fg2` (QuickSave_3784, S1 docks, 154 s): [PLAN_M5](../../../docs/PLAN_M5_WATER.md) M5.1 `rt_water_geometry` (branch m5-water). Warm-up OFF, then `precheck-water-on` (15 s, key on): its `[water] capture` / `coverage` / `families` expect lines need supported draws > 0, so a view without the docks water (the save once labelled the foundry) is INVALID, never a pass. Then OFF-a / ON / OFF-b, each a cost window plus a PNG, and a Q/E turn pair with the key on (lifetime under motion, soup growth).

## Gates (read the deciding numbers in the log)
- Capture, ON windows: `supported draws` > 0 with `failed 0` and `overflow 0`; `unknown` and `excluded` reported with their FS in the `families` line (an unknown forward water FS there is the next audited pair, not a failure). Offline expectation at S1: two draws per frame, `7b44a567` huge_PixelLighting and `1d4bff2e` flowmap.
- Coverage: `captured ... of ... water draws` and `triangles ... of ... supported` at 100% (a lower share names its skip counter); `sampled draws` with `non-finite 0`, `collapsed 0` (collapsed = no camera UBO); `height` negative (the water below the camera) with a spread of the wave amplitude, not tens of metres.
- Depth: `depth write on 0` confirms the offline answer (water is depth-tested, not written; D24S8 identical before/after the water pass in all ten traces). A nonzero count is reported with its FS: the M5.1 coverage pass would then compare against that depth.
- Image: no shading consumer exists, so `shot-on` must equal `shot-off-a` within the `shot-off-b` minus `shot-off-a` spread (mean and max per channel; QA detectors as in lean_regression). Any larger difference is a FAIL (the key must change no colour).
- Cost: `[water] build GPU` median and p90 (BLAS + TLAS of the water draws, triangles per build reported: the size of the "huge" plane is unknown offline); cost-on minus cost-off frame time and `rt passes: GPU cost` against the cost-off-a / cost-off-b spread.
- Pass: valid run (all expect lines, no forbid line), RR create Success, every `[sl] DLSS-G` line after warm-up `presented per app frame 2 ... 2.00`, `TLAS ... none 0`, and every gate above.

Not in this batch: the per-pixel coverage pass (water TLAS vs opaque depth, `rt_debug` view) needs the water scene in the core (ABI 25), the next M5.1 step.

Duration: plan 154 s; one launch, about 5 min (at most 8 min of timeouts). Disk: 3 PNG traces at about 60 MB plus logs, about 0.2 GB.
Start, results and rollback: as in ../lean_regression/README.md with this folder, prefix `m5w` and receipt `m5-water.json` (test-main must be the m5-water commit under test).

Offline check: `powershell -File tools\live_batches\m5_water\dryrun.ps1 [-Out <scratch dir>]`. Last result 2026-11-01: all checks passed.
