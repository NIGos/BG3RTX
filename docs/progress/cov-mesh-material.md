## Pak mesh material catalogue (branch cov-mesh-material, BLOCKED_OFFLINE, 2026-11-03)
- Added `tools/assets/mesh_materials.py`: exact VisualBank UUID / ObjectID / LOD ->
  material UUID, native overrides, texture resources and template bindings / defaults.
  Outputs are exclusive-create only; existing shared asset DB files are never changed.
- Generated locally: `data/assetdb/mesh_materials_cov_mesh_material_v2.json`,
  223,381,989 bytes; 38.1 s, 0 parse failures. 165,802 submesh / LOD rows,
  32,696 materials, 24,006 textures, 1,063 templates. Four conflicting IDs,
  58 ambiguous rows, 3 unresolved rows and 4 missing templates remain explicit.
  Initial v1 retained because this task permits only additions in the shared DB.
- Runtime fallback is NOT implemented. `rt_hit_material_paks` is proposed only.
  Existing mesh keys hash Vulkan handles / ranges, not resource IDs or content.
  Never-drawn pool meshes also lack G-buffer-derived UV / tangent basis; the
  descriptor cache has no texture UUID -> current image-view bridge. LSLib absent.
  Exact prerequisites and fail-closed integration design: TRACK_D_REFLECTIONS 14.
- Replay packs lack pool resource IDs / per-instance shader-family attribution.
  S8 25% and S6 49% missing-record shares are audit evidence, not a family ranking.
  Read-only inventory freezes 659 active rows across five packs; existing blocker
  counters are absent / zero. This is a baseline, not evaluated-energy proof.
  No new-row share, evaluated energy or GPU cost improvement is claimed.
- No runtime / shader / ABI / preset changes; shader request delta 0.
  Existing qualified-row execution remains unchanged; no new-row energy gate passed.
- Draft S8 / S6 / inn / camp single-launch OFF / ON / OFF jobs and predicted.json:
  `tools/live_batches/cov_mesh_material`. BLOCKED_OFFLINE, unqueued, unauthorized.
  Finish identity, basis, descriptors and replay equality before any live request.
- Validation: catalogue tests 3 / 3; preservation tests 7 / 7; CTests 2 / 2.
  Draft dryrun passed static checks and fake runs 4 / 4; temporary Python-launcher
  shim required (`py` absent). Estimate 19.6 min / 760 MB including scene loads.
  No game launch, commit, merge, push or source replay-pack write.
