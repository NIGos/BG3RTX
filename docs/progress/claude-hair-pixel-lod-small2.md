## pl1 S8 small-heads live: results and job fix (branch claude/hair-pixel-lod-small2, 2026-11-10; no code change)

Run `pl1-s8-small-heads.1.211046` (S8 zoomed out, static camera, off / small keep 1/4 / 1/8 / off).
- INVALID cause: the job's 1/4 regex had one `/\d+` too many (7 level counts, the line has 6), so
  `488/0/305/0/0/0` could not match; the line itself was right (after the switch, "small keep 0.250"). Fixed in
  `tools/live_batches_staged/hair_pixel_lod/make_job.py`, checked against the run's four on-window lines (all match
  their window's pattern), live_flow --frozen 0 failing (1 readiness query added). The job now also sets
  dump_preview_f32=1 (float scene colour at the preview grid, ~10 MB per trace) so head energy is float, not PNG.
- Pick, as predicted: 1/4 window 488/0/305/0/0/0 groom frames (5 of 13 grooms, LOD 0 grooms, at level 2 = keep 1/4),
  head 6-53 px; 1/8 window the same 5 at level 3. Nothing at levels 1, 4, 5.
- Strand G-buffer GPU ms per window (60-frame lines): off-a 0.304 / 0.281 / 0.287, on 1/4 0.219 / 0.212, on 1/8
  0.193 / 0.193, off-b 0.293 / 0.307 / 0.307. 1/4 saves about 0.08 ms (-27%), 1/8 about 0.10 ms (-34%), beyond the
  off-a / off-b spread (0.02 ms); larger than the predicted +-0.03 ms.
- Memory: camera arrays 17.4 MB (1/4) and 33.1 MB (1/8, the 1/4 arrays stay resident: arrays are cached per LOD and
  level and freed only with the LOD or the key, 17.4 + 15.7); camera BLAS 82.1 MB (1/4) and 41.1 MB (1/8).
  - BLAS size scales with the kept segments: 1/8 keeps half of 1/4's strands, so half the BLAS (e.g. c10b0714 LOD 0:
    399406 of 1599844 segments at level 2). About 16 MB per groom at 1/4, ~41 B per segment.
  - Avoidable in part: the camera BLAS is built with ALLOW_UPDATE and never compacted (bvhsplit.cpp camera BLAS,
    "never compacted: it is rebuilt at every level change"), while the groom BLAS uses the slot compaction path. A
    compaction after its first build (update stays allowed) is the cheapest saving; the ratio for refittable LSS is
    not measured. Not done here (layer change, needs a live).
- PNG traces (scene colour 5c3fdabc preview, 1280 x 533; G-buffer RT2 class 5 as the hair mask; the six largest
  hair regions, 185-1449 px): float frame mean off-a 0.2909, 1/4 0.2916, 1/8 0.2902, off-b 0.2921 (within the
  off-off spread 0.4%). On the heads the noise floor is too wide for the predicted 0.5-3% effects: off-a vs off-b
  hair pixels 2313 vs 2249 pooled but up to 2x per head (head 0: 597 vs 276), preview luminance 0.422 vs 0.418
  (per head up to 18%), Laplacian HF 0.270 vs 0.253. 1/4: 1632 hair px, 0.412, HF 0.259; 1/8: 2467, 0.423, 0.262:
  all inside or at the off-off spread. The crops show why: the NPCs animate (heads turn) between shots taken 8 s
  apart, so one shot per state does not isolate the strand LOD. Verdict: no visible defect (no holes, no card
  fallback, no outline growth seen), image gain not resolved; brightness unchanged at frame level.
- Crops (off / 1/4 / 1/8, x4 nearest): `.scratch/pl1/head0_off_q_e.png` .. `head3_off_q_e.png`.
- Next if the image gain must be shown: several alternating shots per state in one run (or the pan burst at a
  frozen frame) with dump_preview_f32; otherwise the GPU saving alone favours keeping 1/4.
