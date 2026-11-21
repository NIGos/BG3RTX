## r5 live: Ultra's temporal jitter terms are not a release defect; value 3 stays a developer check (branch claude/r5-ultra-shift, kept off, 2026-11-21)

- r5-d3-ultra-shift (Dialogue3, Ultra, rr_post_jitter_align 0 / 3 / 0, `.1.043959`, 12/12 gates). Output-grade luma,
  8 frames per state, frame-to-frame |diff| x 1000 (OFF-a / ON / OFF-b):
  - fully blurred background (A < 0.05), the metric that rose 4x on High (r3): x 0.267 / 0.314 / 0.230,
    y 0.189 / 0.172 / 0.173, flat 0.217 / 0.232 / 0.302. ON inside the OFF range: no added motion to remove.
    The 1/16 history absorbs the shift.
  - background with A < 0.95, by C-gradient bin: 2.24 / 2.34 / 3.10, 1.20 / 0.84 / 1.20, 1.98 / 1.15 / 1.47,
    6.39 / 3.19 / 3.33, 9.90 / 4.53 / 8.31; DoF-transition std 7.55 / 6.52 / 8.63. These are lower with ON, but the
    OFF repeats differ by up to 2x in the same bins, and the in-focus control (animation) by 2x (14.9 / 17.7 / 31.5).
    These pixels border the animated characters, so the drop is not separable from animation with one burst per state.
- Energy: output grade 0.35866 / 0.35794 / 0.36020 (ON 0.2% under the OFF range, within the 0.5% floor). Float traces:
  scene before post 0.26707 / 0.26646 / 0.27070, RR output 0.32196 / 0.32025 / 0.32324. Both are recorded before the
  output grade, where the key does not act, so their spread is the run's noise.
- Against predicted.json: the "shift wrong" case needed blurred_x / blurred_y below the OFF range. They are not, so
  the prediction is not met. No release defect is shown. No claude/ultra-shift-on. rr_post_jitter_align=3 stays a
  developer check. Only a counterbalanced multi-pair run in a scene without character animation could settle the
  partly blurred bins.
- Summaries: `tools/live_batches_staged/r5_ultra_shift/RESULT_20261121.json`; analyze_jitter.py now also reads the
  RGBA32F temporal history. Raw K8 bursts and presented captures deleted; the three float traces are kept.
