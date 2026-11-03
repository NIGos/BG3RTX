## Water surface analysis (branch water-shader-analysis, planned, 2026-11-03)
- Audited three S8/S1 FS dumps, Materials.pak templates, compose/reflect/hit shading and retained S8 evidence offline.
- Native AA already exists: roughness floor .09, derivative variance cap .18; reflection is incorrectly coupled to opacity.
- Exact/Schlick F at 80 degrees: .3454/.3974; pool template T plateaus at .5; no physical RGB extinction mapping.
- Confirmed historical S8 shadow removal 46.665% of mean C, clamped 0, compose .113 ms; no isolated glint-energy claim.
- Added M5.6: direct glint, interface split, path ramp, spectral medium, foam; all future keys off, costs estimated.
- See docs/WATER_SURFACE_ANALYSIS.md; live S8 pool static/Q/E A/B still needed. Old 113740 pack absent; GI pack lacks water inputs.
- Docs only: no game, git metadata writes, commit, shader/ABI changes or variant request delta (0).
