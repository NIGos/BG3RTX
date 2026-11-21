## r3 lives: rr_post_jitter_align rejected, rt_sun_overlap no gain (branch claude/r3-core-live, rejected, 2026-11-21)

- r3-d3-jitter (Dialogue3, High, 0/1/0, `.1.034009`, 12/12 gates). Output-grade luma, 8 frames per state. Frame-to-frame
  |diff| x 1000 in the blurred background (A < 0.95), by C gradient: OFF-a / ON / OFF-b 0.76 / 1.59 / 0.79,
  0.89 / 2.52 / 0.95, 2.24 / 4.29 / 2.30. Fully blurred (A < 0.05), C-gradient pixels: 0.48 / 1.86 / 0.56 (x),
  0.49 / 2.08 / 0.48 (y): ON flickers 2-4x more on most of the image. The top 1% gradient bin and DoF transitions are
  lower with ON (11.5 vs 15.9 / 19.4; std 0.0040 vs 0.0050 / 0.0053), but the in-focus control falls just as much
  (0.0114 vs 0.0166 / 0.0195): that is the dialogue animation, not the key.
- Cause of the failed prediction: the premise was wrong. In OFF the output over the blurred background is already
  steady (|diff| 0.48 at C-gradient pixels vs 0.30 flat), and C itself moves little there (1.33 / 1.40 vs flat
  1.16 / 0.71). The game's DoF layers do not carry the full jitter there, so a whole-frame shift of A/C/B adds motion. A single global sign cannot be right; value 2 would add the same motion. The sign is not decided.
- Energy (float trace means, luma): scene before post 0.27016 / 0.26919 / 0.26736, RR output 0.29779 / 0.29829 /
  0.29471, output grade 0.3588 / 0.3584 / 0.3567: ON inside the OFF range.
- Decision: rr_post_jitter_align stays 0 in every preset (no claude/jitter-on). Open question for Ultra: `temporal_final`
  applies the same +jitter lookup (and `temporal` the jitter difference) to these A/C/B. That could add the same
  background motion under its 1/16 history. Not measured here; worth one 0/1 window at static Dialogue3 with the shift
  forced to 0 before trusting the Ultra convention.
- r3-s8-sun-overlap (S8 Ultra, `.1.030240`): rt passes OFF-a 7.53, ON 7.50, OFF-b 7.32 ms (medians of 3 periods). ON lies
  inside the OFF drift (0.21 ms): no gain beyond noise plus floor. The overlap ran: in ON the sun's per-pass line is
  gone and the GI trace median includes it (5.81 / 6.03 / 5.54 ms). Here the sun trace costs only 0.30 ms, so the gain
  is capped at 0.3 ms. The 9.72 / 9.63 / 9.47 ms quoted before are "gi: GPU cost" (whole GI interval), not the RT total.
  rt_sun_overlap stays 0.
- Summaries: `tools/live_batches_staged/r3_core/RESULT_20261121.json`, `analyze_jitter.py`. Raw K8 bursts and presented
  captures deleted (about 520 MB); the three float traces (11 MB) are kept.
