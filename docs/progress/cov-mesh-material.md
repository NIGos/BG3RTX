## Pak mesh catalogue and content identity (branch mesh-identity, BLOCKED_OFFLINE, 2026-11-03)
- Step 1 delivers host-side sampled content candidate IDs, behind
  rt_mesh_content_id=0. Off computes no IDs; existing material joins are unchanged.
- Class-A captured meshes and audits use existing mapped raw float3 positions
  and uint8/16/32 indices. Device-local or unsupported inputs stay unknown.
  No GPU readbacks, shader changes, ABI bump or preset promotion. Request delta 0.
- Canonical v1 hashes referenced vertex span, index count and 32 ordered
  index/position samples. An index scan derives the span, not allocation size.
  Handles, offsets, padding, index widths and instance transforms are excluded.
  Re-captures read current bytes, so buffer reuse cannot reuse a cached ID.
- Pool meshes retain the host-only ID. Two adjacent capture sets count equal
  sampled IDs and observed hash collisions; no handle identity enters that check.
  Log: [mesh id] content ids: N meshes, M collisions, K stable across 2 captures.
  Prints after two sets, every 120 sets and when disabling the key.
- Tests: moved buffers/layouts/index widths match; sampled position/index changes
  differ; invalid ranges/formats/NaN reject; forced hash collisions are counted.
  Synthetic sampled mutations: 100,000 meshes, 0 hash collisions.
  Unsampled mutations can alias. This is not exact mesh equality or a pak join.
- CPU cost: 8.318 us per captured mesh for span scan, 32 samples, hash and
  capture registry (4,096 vertices / 12,288 indices, cached CPU RAM).
  This excludes Vulkan mapping lookup/locks and uncached host-memory costs.
- Offline counterpart blocked: lslib_tools() returns empty; LSLib.dll / GR2
  conversion tools are absent. mesh_materials.py reads LSF metadata only.
  Need exact ObjectID / LOD / submesh raw positions and ordered indices.
  Contract and remaining decoder inputs: tools/assets/mesh_content_id.md.
- Existing v2 catalogue retained: 165,802 rows, 32,696 materials, 24,006 textures,
  1,063 templates; 4 conflicting IDs, 58 ambiguous rows, 3 unresolved rows.
- Left: full canonical equality and collision rejection, UV/tangent basis,
  texture UUID-to-view bridge, replay row equality, then authorized live gates.
  No qualified-share, evaluated-energy or GPU cost improvement is claimed.
- Live check (not run): enable rt_mesh_content_id with rt_mesh_capture for two
  capture sets in one scene and inspect the log; unmapped meshes remain unknown.
- Validation: C++ build passed; all 121 CTests passed; git diff --check passed.
  Progress is 35 lines, UTF-8 without BOM. No game launch,
  commit, merge, push, shared catalogue write or source replay-pack write.
