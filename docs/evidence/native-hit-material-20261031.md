# Native hit-material acquisition checkpoint — 2026-10-31

Offline implementation. No game or GPU test was run. Gameplay participation,
field agreement and GPU cost are **not qualified**. Normal GI and reflections
retain their established output. This is a staged provider checkpoint, not M3
completion or a native-BSDF equivalence claim.

## Supported tuple and output

`rt_hit_material=0` by default in all presets. Startup ON enables the main
AlphaDSL bindings 12–15 and the 64-byte capture stride (88 with native Hair).
Runtime OFF disables publication/consumption; startup OFF requires restart.
The coordinated host/core ABI is **20**, appending the typed
`rt_hit_material_use` query-owner callback. Rebuild both DLLs and launcher.
Private current-Sun descriptors and consumers remain unchanged.

Initial family: native VS `3812bc980438747f`, deferred FS `8995da1e4060f53b`,
fresh dynamic alpha-soup triangles only. Instanced, cached/pool, procedural,
legacy and ambiguous identities are unsupported. Existing original alpha data
and TLAS remain intact. Native deferred bindings are engine UBO 1.0, reflectance
UBO 1.1, immutable sampler 1.2, indirection view 1.3, normal/albedo/physical
views 1.4/1.5/1.6, and tile-set SSBO 0.12. The provider uses these actual native
views, including their component mapping and sampler interpretation. It does
not infer sRGB from backing images or invent VT residency.

A per-slot join spans separate prepass/deferred/query recordings. Exact draw
identity and per-triangle GPU bit equality of post-VS clip positions and UVs
are required. Source/deferred camera binding, scene generation and frame must
agree. An execution-time compute gather reads the actual native uniform/storage
slices immediately after the deferred render pass. CPU alpha snapshots are
only comparison hints. The gather publishes a row only when actual native VT
parameters agree. Every graphics module in that pass must be proven free of
storage writes; secondary execution explicitly rejects this domain.

Normal, albedo, physical layers, native reflectance and native tangent frame
feed shared `NvFields`, then `NtParameters` and the declared FresnelBlend
adapter. Physical Z remains an uninterpreted auxiliary field. Ray-cone gradients
are explicit and differ from native raster quad gradients. Missing resident
pages, unsupported source state, degenerate mappings or invalid frames return
unsupported rather than a white/default material.

`rt_debug=9` with the key ON visualizes supported primary-hit albedo. Existing
`rt_stats` reports provider status counts, source/deferred recording flags and
prepared/copied/claimed counts. On matched opaque class 0 primary hits, it
reports raw albedo, normal, roughness, reflectance and metalness residuals.
Mean residual quantization is 1/256. Cone versus raster footprint differences
prevent treating these raw residuals as an encoding-only equality test.

## Lifetime and limits

Owned sampler clones retain complete captured sampling metadata through slot
retirement. Original view/image lifetimes use existing host lifetime tracking.
Each producer/deferred/query recording is independently pinned around the
actual driver submission; only success records submission. Replayed ONE_TIME
records or foreign queues fail closed. Pending unsubmitted records cannot age
out. Successful command reset/free or pool reset/destroy invalidates recording
identity and drops pending BVH records; failed reset preserves it. GPU resource
reuse waits for actual submitted-owner completion and all reservations.
Submit2 wait/signal stages cover added transfer/compute work independently of
OMM. The ordinary path preserves the original submit pointer.

Hard bounds: 16 accepted material draws per scene, 64 cache descriptors,
16 owned sampler descriptors, 8192 rows, 32 MiB basis/copy stream and about
2 MiB rows per allocated slot (about 272 MiB over eight allocated slots).
Exact shared-resource deduplication is a follow-on. First-scene preparation
must precede matching deferred recording; this ordering and the game's actual
ONE_TIME flags still need the small live acquisition probe. No zero-record
run may be called a provider success.

For equal capture vertex capacity, material-only stride 64 needs camera
1024 MiB / cascade 427 MiB where stride 24 used 384 / 160. With Hair stride 88,
use the already established 1408 / 587 profile. This commit never resizes user
budgets automatically. GPU milliseconds are unknown offline.

## Validation

Matching ABI 20 host, core and launcher build successfully in isolation.
Eight targeted CTests pass: exact join/ownership/readonly proof, ABI callback,
actual Vulkan Submit2 structs, native VT fields, native transport parameters,
shared BSDF, shared frame and settings/schema/presets. Both actual recipe
SPIR-V modules pass the readonly proof. Independent existing numerical suites
include 164188 VT checks, 786570 native parameter checks, 1700 FresnelBlend plus
832 baseline BSDF checks, 4338 frame checks, 58 submit-struct checks and
291 settings checks.

All 544 current shader files compile. All 539 entries from the 527-file
`d4a1612` baseline are byte-identical; 17 new material modules also pass
`spirv-val --target-env vulkan1.3`. Four initially path-confounded baseline
compiles were rerun from a short isolated source path, not omitted. Exact
hash pairs and the path correction are in
`native-hit-material-shaders-20261031.json`. These are CPU compiler checks,
not executed Vulkan/device validation.

Next: freeze this coherent acquisition candidate, confirm actual nonzero
qualified tuples and same-frame field residuals in one small authorized probe,
then add the reusable bounded direct-only shaded-hit reference consumer.
Normal RF promotion requires an actual boundary/environment contribution;
direct-only radiance must not silently replace the full native probe.
