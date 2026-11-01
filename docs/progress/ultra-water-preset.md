## Ultra preset: RT water "+ Refraction" (branch ultra-water-preset, READY_FOR_LIVE, 2026-11-01)
- Stage 3 of the M5 water promotion (m5_promotion passed at S8, Flymm and camp). Redone on main's one
  "RT water" entry (common/settings.h kKnobs), not the old 8-entry commit 7cff2bb (not merged).
- RT water leaves Experimental: Advanced, before the Experimental section; presets {Off, Off, Off,
  + Refraction}. Ultra writes all 7 water keys = 1 (game-shadow replacement included); other presets 0.
- Startup rules: required_on adds sharc_device=1 (64-bit shader integers) next to rt=1; turning compose,
  shadows, local shadows, game shadow or refraction on needs a restart, off is live (key_change_needs_restart).
- settings_test test_ultra_water: Ultra writes every water key, other presets none, dependencies met in
  Ultra, sharc_device=0 unmet, restart rules, GUI order. The 11-01 user ini migration now reads the water
  keys from the preset (one restart marked fewer). Schema regenerated.
- Jobs: m5p1..m5p3 and su1 drop the explicit water keys (preset=ultra sets them; the plans keep their
  per-phase toggles). The test tree must hold this branch.
- Merge only after a smoke_ultra run passes with it. Cost: the water chain 0.53-0.67 ms (m5_promotion
  cost-on), up to 520 MB of water records.
