## rt_capture_wide_split on by default (branch claude/wide-split-on, 2026-11-09)

- rv2 (Water Queens House zoomed out, release Ultra, FG 3, RTX Hair, warm cache, idle GPU, 14:55) against rv1
  (same plan, key off, 14:47): RT capture opaque drops 0.3-2.0 -> 0.00 per frame in every phase; opaque peak
  1600 -> about 1190 MB; 88-byte strand-frame draws about 12/frame, 83 MB, 0 left out; RT passes 8.5-9.5 ->
  7.4-8.9 ms, GI 11.1-12.5 -> 9.5-11.7 ms, scene wait unchanged. Kept, on.
- The first rv2 (14:30) and the 14:37 rerun were confounded (a video render on the GPU; then the first launch
  after the build compiled 54 GI pipelines in 260 s). Structural counts matched rv1 in every run.
