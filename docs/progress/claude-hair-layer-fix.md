## Strand hair layer path: review fixes (branch claude/hair-layer-fix, READY_FOR_LIVE, 2026-11-08)
- Review of the layer path verified item by item (docs/RTX_HAIR_PRIMARY.md 5j); all confirmed except the core level-load
  counter (none exists; the image creation id covers handle reuse).
- Header version (1): the submit always applies the newest publish ordered before the G-buffer, rows remapped to the
  recorded slots; draws whose groom it lacks draw their card in the strand variant (card depth kept). No 2x memory.
- Mask holes (2): header slot mask of the swapping slots; the pixel trace skips other rows (3 skips), the probe traces
  privately. Count (3/4): colour, traced/hit and per-groom pixels for all four tag windows: C2 a_f measured again,
  dropouts always attributed.
- Coherence (6/18): "Strand hair on screen" On = primary + order + current + frame_tag + lod_keep + hold +
  tangent_world. The four separate knobs are gone (keys stay as ini diagnostics); passes/shadow_t/ao_t need the strand
  knob. Presets unchanged (strands off). settings_test test_strand_bundle.
- Also: lod_keep evicted-LOD restage and first-upload slot churn (5); cascade_drop gated on the core's RT sun persist
  flag (12, core/gi.cpp 5 lines); live-off latch fallback (13); LoadGuard image id (14); pool release when off (15);
  zero-length segments (17); watchdog and no-fallback orphan block (19); `header safety` period line (20).
- Gates: build -Test, shader identity (only the hair G-buffer and count requests change; variants.txt delta 0).
- Cost: unmeasured (no game). The probe's private trace is per fragment while a draw shows cards.
- User check: Test Dialogue1, strands on, characters walking in and out: no holes where hair overlaps, `header safety`
  mismatch 0, watchdog 0, shadow transmittance measured with shadow_t on.
