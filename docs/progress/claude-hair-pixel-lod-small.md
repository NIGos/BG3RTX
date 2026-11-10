## Pixel LOD for heads of 35-60 px (branch claude/hair-pixel-lod-small, offline, 2026-11-10; key rt_hair_pixel_lod off)

- Why: hl1 200821 (S8 zoomed out) had every head at 6-53 px, so the 60 px floor left all 13 grooms at camera level 0.
- Tool: `tools/hair_groom/pixel_lod.py --ladder` (the study tool, brought over with its sweep mode): study beard 16fa158c,
  heads 20-60 px, 32 jittered frames, every camera level the layer can trace (today's LOD keep / 2^j down to 1/32), the
  continuous widths k 0.5-0.8, and a coverage-conserving radius per level (radii x c, c fitted so mean coverage equals
  the truth). About 2 minutes, writes ladder.json.
- Gate (derived, per size): coverage bias, silhouette growth and bulk energy may exceed today's by at most
  max(today's LOD step, split-half noise of today and of the case); a gain is coverage noise or HF beyond that noise.
  Per-pixel coverage RMS is reported, not gated: it rises with every HF gain by construction.
- Finding: the coverage and silhouette error depend on the keep, not the head size. Bias / silhouette at every size
  20-60 px: keep 1/4 +0.5..+0.8% / +0.1..+2.1% (today's LOD 1 -> 2 step), 1/8 +1.8..+2.9% / +3..+7.5%, 1/16
  +5.3..+5.7%, 1/32 +15% (+15..+17%); the study's 80-150 px numbers fit the same curve. "Fails under 60 px" was the k
  rule asking for 1/32 there.
- Per size (today LOD 1 = keep 1/2 -> keep 1/4; HF, coverage noise, bias, silhouette):
  - 20 px: today is already LOD 2 (1/4); 1/8 fails (+2.9% / +3.3%). Unchanged.
  - 30 px: 0.045 -> 0.051, 0.184 -> 0.182, +0.5%, +0.2%: no gain beyond noise. Unchanged.
  - 35 px: 0.050 -> 0.058, 0.183 -> 0.175, -0.3 -> +0.6%, +1.0 -> +2.1%: pass (per-frame HF gain).
  - 40 px: 0.051 -> 0.059, 0.165 -> 0.164, 0.0 -> +0.7%, -0.2 -> +0.1%: pass.
  - 50 px: 0.054 -> 0.068, 0.161 -> 0.154, -0.2 -> +0.7%, +0.7 -> +2.0%: pass.
  - 60 px: 0.054 -> 0.071, 0.153 -> 0.146, -0.2 -> +0.5%, +0.6 -> +1.7%: pass.
  - 1/8 at 35-60 px: HF 0.068-0.087, noise 0.125-0.164, but +2.2..+2.4% / +3.1..+4.2%: fails everywhere.
  - Coverage-conserving radius (c 0.53-0.97 by keep): passes at some sizes only (30, 35, 50 px) and is a per-groom
    calibration: rejected. Cards for scale (study, 40 px): HF 0.147, coverage -47%.
- Rule (common/hair_groom_budget.h pixel_lod_level): heads of 35 (kPixelLodSmallHeadPx, the smallest passing size)
  to 60 px run the same k pick with the ladder stopped at keep 1/4 of the full groom (LOD 1 -> level 1, LOD 0 ->
  level 2, LOD 2 -> 0); under 35 px level 0; 60 px and over unchanged. Dev knob `rt_hair_pixel_lod_small_keep`
  (0.25; 0.125 for the look A/B; 0 = level 0 under 60 px as before), shown in the period line ("small keep").
- Key off: unchanged (the pick runs only with rt_hair_pixel_lod on; no allocation). No shader change (no file under
  shaders/ or any include touched), variants delta 0. CTest: hair_groom_asset_test checks the step-1 sizes.
- Live staged, not queued: `tools/live_batches_staged/hair_pixel_lod` (pl1-s8-small-heads): S8, hl1's wheel step,
  static camera, off / on (1/4) / on (1/8) / off, period line, strand G-buffer GPU ms, one PNG trace per window;
  predicted.json first; live_flow --frozen 0 failing (2 readiness queries added to the lint snapshot).
- What the live should show: 1-6 grooms at level 1-2 with small keep 0.250, no outline growth, GPU ms within
  +-0.03 ms; strand structure slightly finer than today (subtle at 1280 px). 1/8 is the visible step and the user's call.
- Open: one groom (a beard); the 60 px boundary jumps from keep 1/4 to the k rule's 1/32 (+15% coverage, as before
  above 60 px: by the same finding the >= 60 px rule overcovers too, a decision for the coordinator).
