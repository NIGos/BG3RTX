## Visibility ray flags audit (branch opt-rayflags, REJECTED, 2026-11-05)

- Audited all RayQuery / TraceRayInline sites in shaders/gi and shaders/rt at f3d3c41a (also main).
- No safe release visibility change: every binary scene query already accepts the first accepted hit.
- Full site inventory, caller bounds and exclusions: docs/RAYFLAGS_AUDIT_20261105.md.
- Closest-hit consumers need distance/material; occluder statistics need the nearest blocker.
- Alpha candidate resolution and candidate-cap/unknown handling remain unchanged.
- Shader request delta: 0. No key, prewarm, live job, queue submission or game launch.
- GPU cost: not measured; no implementation candidate. No commit (coordinator owns commits).
- Validation: scripts/build.ps1 -Test built successfully; 177/178 CTests passed. Water shader gate
  could not open the shared SQLite cache; isolated retry with BG3RTX_SHADER_CACHE under
  .scratch/rayflags-shader-cache passed 1/1 in 25.84 s. All 178 tests passed across these runs.
- Rendering sources match main exactly (git diff --exit-code main -- shaders common core layer bg3rtx.ini).
