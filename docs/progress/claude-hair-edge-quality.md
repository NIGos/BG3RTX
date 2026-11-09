## Strand edge dithering: strand-aware dither resolve and analytic coverage (branch claude/hair-edge-quality, READY_FOR_LIVE, 2026-11-09)
- Diagnosis (docs/RTX_HAIR_PRIMARY.md 5n): binary sub-pixel strand coverage on the shared jitter, and the game's
  stencil dither resolve (7401d785, ported in rr_post.hlsl) mixing strands by the card dither tags the strand clones keep.
- Key `rr_hair_strand_aa` (knob exp_hair_strand_aa, Advanced, live 0/1/2, off): new request rr_strand_aa.hlsl dispatched
  instead of the resolve (layer/rrpost.cpp). 1: strand footprint untagged; 2: plus analytic coverage (found strands
  weighted by 1 / found probability, GBAA-style colour exchange). Key off: no existing request's inputs changed.
- Offline (tools/hair_groom/edge_stability.py, hair_groom_test test_strand_edge): edge flicker raw 0.0178, card-tag
  resolve 0.0117, analytic 0.0086; edge energy +0.03% / -4.2% / +1.3% vs the area reference.
- Rejected: decorrelated per-pixel ray positions (flicker 0.034 -> 0.056, card-edge loss).
- Layer edits: hair_primary.cpp logs each hair pipeline's stencil state (one block, 12 lines); ngx.cpp ngx_gbuffer_camera.
- Live: tools/live_batches/hair_edge he1-dialogue1-edge (Test Dialogue1, 0/2/0/2 + 1, PNG + K8 bursts), not queued.
- he1 #1 live: strands write no stencil; the game's resolve never touches hair (Xr = compose on every hair pixel), so
  mode 1 is moot; mode 2 changed no hair pixel (coverage path inactive on the GPU). Debug mode 3 and window f-debug
  added; bursts now static 6 s timed windows (the 60-frame window closed before the K8 burst completed).
- he1 #2: the record has no current seal at the post chain (f-debug); the strand data are now snapshotted at the lighting
  pass into a frame-parity image. Baseline burst flicker: edge band 0.067 vs hair core 0.032 (final image).
- he1 #3 freeze: lock-order inversion (rrpost g_mu -> ngx g_mu at the lighting snapshot vs ngx -> rrpost at the evaluate);
  fixed (camera read before rrpost's lock, none in the resolve), test added. No live rerun without the coordinator.
- he1 03:04-03:17: mode 2 applied in 3 of 8 aa trace frames (~930 ring px changed, ring HF -12%), energy -0.1..-0.2% within
  noise. Burst windows fixed (out of periodic gates; tighten keeps a burst window's declared 14 s).
- Snapshot moved to the scene G-buffer end (once per frame; the lighting hook fired in 188-590 of 600); fallbacks counted.
