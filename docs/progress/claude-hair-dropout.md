## One-frame strand dropouts: counter and camera gate (branch claude/hair-dropout, offline, off, 2026-11-08)
- Also lands tools/hair_groom/temporal_burst.py (pan burst temporal metrics) from claude/hair-temporal.
- Cause found by reading (not yet proven live): the scene build's camera is stored under frame_index F, the groom
  publish inside the same build reads frame_index after a present on another thread advanced it, so the header has no
  build camera; every strand fragment discards (hp_rebase) while the swap gates pass: a frame without hair.
- Counter `[hair primary] dropouts (last 300 frames)`: swapped G-buffer draws whose strand pass wrote under a tenth of
  their card pixels, by first explaining check, first 4 frames, plus publishes without the build camera. CPU only,
  from the existing marker counts.
- Key `rt_hair_primary_camera_gate` (knob exp_hair_primary_camera_gate, Advanced, live, off): the build's own camera,
  and headers without one are never applied (cards that frame). No shader change, no ABI change.
- Live check: RTX_HAIR_PRIMARY 5h.
