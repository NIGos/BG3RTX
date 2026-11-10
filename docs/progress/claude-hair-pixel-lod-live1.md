## rt_hair_pixel_lod first live: inconclusive, a level-pick bug left 12 of 13 grooms at level 0 (branch claude/hair-pixel-lod-live1, analysis only, 2026-11-10)

- Data: hl1-s8-late-arrival runs 180301 and 180949 (repeat), S8 static zoomed-out camera, bursts off 99310 / on 99311 per
  run (16 frames, `tools/hair_groom/temporal_burst.py` reader and HF metric). The camera is identical in all 4 bursts
  (P11 1.921); characters idle and walk between bursts, so only the static front character is a clean control.
- px per radian is right: 1067 / 2 x P11 1.921 = 1025 (vertical fov 0.961 rad). The `[sl] fov 0.838` line is the
  load-screen camera (P11 2.246), logged only for the first 3 frames.
- "Head px" is not the study's head size: `layer/bvhsplit.cpp:3393` takes the groom bounds diagonal / distance (:1420),
  the study (`pixel_lod.py` HEAD_M) a 0.25 m head height. The front head is about 64 px tall (0.25 m at 3.97 m) but its
  groom logs 177-180 px: the diagonal reads 1.5-3x high, so the 60 px floor (kPixelLodMinHeadPx) acts about 2x too low.
- Head sizes by the code's measure (LOD 0 = diagonal over 92 px, LOD 1 = 31-92, LOD 2 = under 31): 5 grooms over 92 px,
  3 in 31-92, 5 under 31 (both runs). By real head height about 4-6 heads are 45-65 px: the isometric medium case sits
  right at the 60 px floor, not mostly below it.
- Bug, why only one groom changed: `groom_r_median` (bvhsplit.cpp:1333-1339) returns 0 without host radii, so d0 = 0 and
  `pixel_lod_level` returns 0 (:3391-3395). Host radii are kept only for LODs uploaded with the key on (:1260); the
  re-decode in `groom_cam` (:1288-1298) runs only at level > 0, which needs d0 > 0 first. Only 36576f20 LOD 1 was
  uploaded during the key-on part 1 of both runs and kept its cached r_median. The "0.00" d0 values are this bug.
- The changed groom (LOD 1, level 4, 9593 of 151794 segments) is not identified for certain. Best match in run 1: the
  dark-haired head at render x 786-867, y 430-493. Off/on there: albedo HF 0.458/0.598, accumulated albedo HF
  0.130/0.206, final HFacc 0.0115/0.0186, temporal std/mean 0.047/0.028, mean 0.332/0.336 (SEM 0.0005), coverage -0.7%,
  albedo change per frame 0.64/0.61 (study: 0.96-0.99 to 0.51-0.79). In run 2 that character is elsewhere.
- Control (front head, LOD 0, unchanged), off/on: std 0.0285/0.0197 (r1), 0.0251/0.0120 (r2); HFacc 0.0135/0.0150,
  0.0153/0.0206; mean +0.66%, +0.59%. Off vs off between runs: std 0.0285/0.0251, HFacc 0.0135/0.0153, mean 0.4%. All
  hair: mean +1.7% (r1), +1.2% (r2) against off/off 2.8%. The candidate's std drop (-41%) is within the control's
  (-31% to -52%); its HF rise (+62%) exceeds the control's (+11% to +35%), from one run only.
- HF scale: the study's 0.09-0.15 (cards) is offline shading HF before RR, not comparable with live final HF (0.02-0.03,
  after RR). Closest: accumulated albedo HF, 0.08-0.10 on unchanged strand hair, 0.21 on the candidate.
- One LOD 1 draw fell to cards in each on burst (758b r1, e851 r2); in r2 before the key went on, so not the key.
- Strand G-buffer GPU is noise: r1 0.30-0.34 off, 0.517/0.458 on; r2 0.28-0.40 off, 0.281/0.282 on.
- Conclusions: (1) the live run does not test the design: the r_median bug and the diagonal head size decide which
  grooms change. Fix both before another live (r_median at upload whatever the key; 0.25 m x ppr / distance or a scaled
  floor). (2) Where it acted, the predicted structure gain (accumulated HF up), no energy or coverage shift beyond noise.
  (3) Paused, key off. Crops: `.scratch/pixel_lod_live1/b4_*.png` (main checkout, not committed).
- The trail / blur in motion found during this analysis (strand motion vectors camera-only on most strand pixels) is
  followed up in docs/progress/claude-hair-mv.md and docs/RTX_HAIR_PRIMARY.md 5x.
