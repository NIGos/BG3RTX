## Elastic capture drop regression (branch fix-elastic-drops, READY_FOR_LIVE, 2026-11-02)

- No commit or git metadata writes. No game launch or shader tools. Defaults
  remain ON; ABI unchanged; shaders/variants.txt request delta 0.
- su1 is INVALID. Its 394.2 MiB opaque peak includes rejected bytes, but final
  capacity 912 MiB hides 544 -> 720 -> 912 MiB growth in the same stats window.
  The old 24-frame shrink history expires inside the 120-frame log window.
- Primary RT capacity now stays >= effective fixed baseline (Ultra 1024 MiB).
  This preallocates space before an unpredictable draw, instead of trying an
  unsafe mid-recording replacement. Primary savings below baseline are lost;
  cascade shrinking remains enabled and unchanged.
- Growth restores 25% headroom proactively at a safe producer boundary; demand
  includes actual alpha reservation plus physical opaque bytes/drops. Retain
  120 samples plus margin across replacements; falling card cap cannot force
  shrink below already allocated reserve. Existing guard/fence checks remain.
- Elastic alpha split preserves both streams' fixed-baseline room, including
  the proportional-split threshold (483 -> 413 MiB opaque room before fix).
- CPU regression covers logged 394.2/118.5/364/912 MiB, synthetic instantaneous
  placement, baseline floor, 120-frame peak aging, proactive growth, cap drop,
  and the inn split threshold. Synthetic placement is not an exact GPU replay.
- Validation: explicit layer C++ build and 4/4 targeted CTests; defaults match.
  Separate fallback copy: generator/check and settings test, 983 checks pass.
- Alternative, NOT applied: docs/alternatives/fix-elastic-drops-default-off.patch.
  Semantic one-line fallback is rt_capture_elastic=0 in bg3rtx.ini; regenerate
  common/settings.h, retain kAutomaticKeys so presets inherit OFF, and update
  the automatic-key default test plus assert OFF in every effective preset.
- READY_FOR_LIVE, not a quality pass. Restart layer; authorized S8 smoke_ultra
  must show opaque buffer-full 0.00 in every pan-e/rotate-r and other window.
  GPU ms/energy/visual evidence remain unmeasured; no additional GPU commands.
