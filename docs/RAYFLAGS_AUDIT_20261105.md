# Visibility ray flags audit, 2026-11-05

Branch opt-rayflags; inspected HEAD/main f3d3c41a8bb919f22aed334ca42ab2438dd5621c.
Result: no eligible release-path change. All binary visibility queries already terminate
on the first accepted intersection. No rt_ray_flags_vis key or A/B job is warranted.
Shader request delta: 0. No rendering code changed.

## Query sites and current flags

Flags below combine the RayQuery template and TraceRayInline runtime argument.
`SKIP` means RT_SKIP_PROCEDURAL: skip procedural primitives unless native hair is enabled.
`FIRST` means RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH. No query invokes a closest-hit
shader: these are inline queries, with committed data read explicitly by their caller.
Adding RAY_FLAG_SKIP_CLOSEST_HIT_SHADER would not remove any such reads or shader work.

| File:line (declaration; initialization) | Current flags | Decision / data required |
| --- | --- | --- |
| shaders/rt/rt_common.hlsli:1133;1153-1156 | SKIP + (RT_OCC_ON ? 0 : FIRST) | Statistics-capable visibility path. Release RT_OCC_ON=0 already uses FIRST. ON records nearest t, position and occluder kind at 1220; preserve nearest-hit traversal. |
| shaders/rt/rt_common.hlsli:1137;1153-1156 | FIRST + SKIP | Native-specialized visibility without occluder statistics. Already optimal termination. |
| shaders/rt/rt_common.hlsli:1139;1153-1156 | FIRST + SKIP | Legacy visibility without occluder statistics. Already optimal termination. |
| shaders/gi/ls_ref/ls_ref_body.hlsli:19;25 | FIRST + SKIP_PROCEDURAL_PRIMITIVES | Local-shadow reference/check path, already first accepted hit. |
| shaders/rt/rt_common.hlsli:965;966 | FORCE_OPAQUE + FIRST | Experimental groom binary shadow fallback, already first accepted hit. |
| shaders/rt/rt_common.hlsli:971;972 | FORCE_OPAQUE | Groom nearest hit used by closest-hit queries; t, instance and primitive required. |
| shaders/rt/rt_common.hlsli:1013;1014 | FORCE_NON_OPAQUE | Experimental groom transmittance: deduplicates crossings, multiplies transmitted colour, makes stochastic visibility decision. Already aborts for zero transmission. Not a binary intersection search. |
| shaders/rt/rt_common.hlsli:1315/1317;1329-1332 | SKIP | Debug closest hit, alpha and nearest-candidate kind. Attributes and distance required. |
| shaders/rt/rt_common.hlsli:1485/1487;1499-1502 | SKIP | rt_closest_hit: GI/reflection hit material, barycentrics, transform and nearest distance; also primary/origin matching and stats. Cannot use FIRST. |
| shaders/rt/rt_common.hlsli:1861/1863;1875-1878 | SKIP | rt_closest: nearest distance used for AO falloff, projection and depth matching, water reflection/refraction sampling. Cannot use FIRST. |
| shaders/rt/em_common.hlsli:200;201 | FORCE_OPAQUE | Emission lookup over [max(t-eps,0), t+eps]. Reads nearest geometry/primitive/transform/barycentrics to evaluate emission textures. Not a visibility ray. |
| shaders/rt/water_reflect.hlsl:127;128 | FORCE_OPAQUE + SKIP_PROCEDURAL_PRIMITIVES | Water primary query [0,1e30]; nearest t classifies water against raster depth, then locates its surface. Not a shadow ray. |
| shaders/rt/hair_receiver.hlsli:14;17/19 | FORCE_NON_OPAQUE + SKIP_PROCEDURAL_PRIMITIVES | Experimental receiver proof: visits competing surfaces within matching interval, never commits, checks candidate identity/t and unreadable geometry. Existing early returns preserve its conservative proof. |
| shaders/gi/hair_fibre.hlsl:66;67 | FORCE_NON_OPAQUE | Experimental fibre lighting [1e-3,dist]: counts distinct strand crossings, not binary visibility. |
| shaders/rt/hair_primary_gbuffer.hlsl:148;149 | FORCE_OPAQUE | Experimental primary hair [0,1e5]: nearest strand index, segment parameter and depth required. |
| shaders/rt/em_debug.hlsl:66;67 | FORCE_NON_OPAQUE | Debug emission overlap inventory: deliberately never commits; needs all candidate geometry IDs, including ties. |

The final four experimental/debug entries are included for inventory completeness; they
are not ordinary release visibility opportunities. The search covers nested gi/ includes
as well as rt/ includes. TraceRayInline declarations outside these files were not added.

## Release visibility callers and intervals

Every site below reaches rt_common.hlsli's FIRST query above; there is no separate
unflagged shadow query hidden in hit lighting. All use TMin=0 after their existing
surface offset. Positive TMin would discard possible blockers near that offset.

| Caller file:line | Existing TMax / purpose |
| --- | --- |
| shaders/rt/native_hit_direct.hlsli:241 | Point/spot GI and reflection hit shadows: distance from offset origin minus max(ls_clear,0), or the existing light policy clearance. |
| shaders/rt/native_hit_direct.hlsli:301,309,326,333 | Box hit shadows: box exit, additionally clamped to shadow near plane / fixture fade end for the existing LS2 policy. |
| shaders/rt/native_hit_sun.hlsli:86,88 | GI/reflection hit sun: pc.rt_tmax, including the existing disc/cascade policy. |
| shaders/gi/rt_local_shadows.hlsl:233 | Point/spot: length(light-origin) - max(light radius,ls_clear); physical-hair branch retains its exact light segment. |
| shaders/gi/rt_local_shadows.hlsl:526 | Box: exit clamped to shadow near plane or fixture end under the existing policy. |
| shaders/gi/rt_shadow.hlsl:109 | Screen sun: pc.rt_tmax after shared origin offset. |
| shaders/rt/water_compose.hlsl:530 | Water sun: pc.rt_tmax after one-footprint surface offset and scene rebase. |
| shaders/rt/water_compose.hlsl:627 | Water local: distance(light-offset origin) - pc.ls_clear. |
| shaders/gi/ls_ref/ls_ref_body.hlsli:726 | Reference local shadow: same finite light segment, its own already-FIRST query. |
| shaders/gi/sharc_common.hlsli:253,254 | Non-release cache connection/thin tests: connection length / voxel extent via rt_occluded, also already FIRST. |
| shaders/gi/rt_occ_stats.hlsl:56 | Diagnostic sun visibility: intentionally retains nearest occluder statistics when enabled. |

No shorter positive bound is proven equivalent: local/box bounds already express the
light policy, while directional light can be blocked anywhere up to pc.rt_tmax.
The cascade-specific minimum at rt_common.hlsli:1203 is applied only to cascade
candidates; raising the whole ray's TMin would also drop valid nearby scene blockers.

## Exactness and rejected changes

The visibility loop retains alpha evaluation at each candidate with its original cone,
candidate distance, barycentrics, transform and ray class. FIRST stops only after an
opaque hit or an explicitly accepted non-opaque/procedural hit. Rejected alpha candidates
continue as before. The candidate cap and kRtUnknown fallback remain part of the result.
Replacing that loop with FORCE_OPAQUE, or culling non-opaque geometry, changes foliage
coverage. Culling opaque geometry loses solid blockers. Face culling would lose existing
two-sided blockers; no winding/closed-volume invariant proves it safe. Procedural culling
is already enabled exactly where hair does not require procedural candidates.

The superficially shadow-like extra query at native_hit_direct.hlsli:249 is stats-only:
it reads the nearest blocker's distance and instance/primitive to identify self hits and
near-fixture hits. AO at native_hit_direct.hlsli:380 reads t for 1-(t/R)^2 falloff.
Neither can use first traversal hit without changing its output. GI/reflection nearest
hits are projected and depth-matched before lighting, and emission lookup evaluates the
selected triangle's textures. First-hit termination there is incorrect.

No key-on implementation exists to qualify. Release equivalence compilation, startup
prewarm and live-job preparation/lint are therefore inapplicable under the requested
no-change stop condition. GPU cost is unmeasured; expected code-path delta is zero.
The requested build/test result is recorded in docs/progress/opt-rayflags.md.
