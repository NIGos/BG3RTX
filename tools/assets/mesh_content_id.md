# Mesh content candidate ID v1

Runtime implementation: `layer/mesh_content_id.h`, used by `layer/bvhsplit.cpp`
only with `rt_mesh_content_id=1`. This is a sampled candidate fingerprint, not
proof of mesh equality and not a material qualification path.

The catalogue reader `mesh_materials.py` parses LSF VisualBank metadata. Its
GR2 counterpart is now `python -m tools.assets.mesh_paks`: use the existing
LSLib v1.20.4 ExportTool package (LSLib.dll and granny2.dll) and PowerShell 7.
Pass `--catalogue`, `--game`, `--lslib`, `--pwsh`, `--cache`, and a NEW `--out`
JSON file; its binary companion is generated beside it. Outputs and cache must
be outside the game install. Paks are read-only; generated data is never shipped.

The native reader preserves vertex/index order. ObjectID's numeric suffix is
ExportOrder, not a triangle-group index. Current support requires one uniquely
named native mesh and one MaterialIndex 0 group covering the whole index array.
Conflicting source/name material assignments across all catalogue rows poison
the join before family filtering or --limit. Native ExportOrder/LOD verification
is explicitly unavailable in this increment. Multiple groups remain unknown.

`--match lava tileset hlod` selects initial coverage gaps; `--limit` limits GR2
sources. A 25 s heartbeat covers native decoder subprocesses. Cache keys include
GR2 bytes, material metadata, tool source and LSLib/granny binaries. Unchanged
sources reuse decoded metadata and full ordered position3/UV2 corner streams.

Binary B3PAKS1 versions 2/3 carry each sampled candidate, material UUID, flags,
and all ordered position/UV corners. JSON also retains resolved texture UUIDs,
parameters and provenance. Runtime loads `mesh_paks_cov_paks.bin` beside the
asset DB. The sampled ID or 32 corner UVs (groom tolerance 1e-3) select candidates;
full position bits and all corner UVs must agree before HLOD can publish a row.
The HLOD recipe uses current observed descriptors/overrides and the existing
checked XFB basis. Unknown resources, ambiguity, unsupported layouts and
unsupported vertex-colour recipes stay unqualified. A UV-only candidate is never a row.

The shared v1 canonical words are:

1. Version `1`, referenced vertex span `max_index - min_index + 1`, index count.
   For non-indexed draws the vertex count is the draw count and index count is 0.
   The span is not the number of distinct referenced vertices or allocation size.
2. For s = 0..31, corner `floor(s * (draw_count - 1) / 31)` in draw order:
   index minus min_index, then the three raw IEEE float32 position words read at
   `vertex_offset + index`. Non-indexed indices are implicit 0..draw_count-1.
3. FNV-1a 64 over all 131 uint32 words serialized little endian, offset basis
   14695981039346656037, prime 1099511628211, wrapping at 64 bits. Map hash 0 to 1.

Buffer handles, byte offsets, strides, padding, instance transforms and index
width are excluded. Indexed draws scan indices for their referenced span, then
sample 32 corners. Raw float3 triangle lists only; missing host mappings,
unsupported formats, out-of-range addressing and sampled NaN/Inf remain unknown.
No GPU copy/readback is added. Re-captures read current bytes, including reused
buffer ranges; ordinary cached draws do not compute IDs.

The log reports unique IDs in the last capture set, observed differing-sample
hash collisions, and equal samples seen in both adjacent capture sets. It prints
after two sets, every 120 sets, and when disabling the key. Different unsampled
bytes can alias and are not counted as detectable collisions. Full ordered position/UV equality, unsupported-layout rejection and material
disambiguation are enforced by the HLOD fallback; sampled statistics alone
still prove no equality. No ABI or shader changes.


cov-paks2 offline batch: `--plan-only` inventories every matching catalogue row
without a decoder. `--limit` defaults to 0 (all sources), and `--start-source N`
selects a deterministic lava-first source shard when bounded work is needed.
Cache records are written after every decoded GR2 and reused on restart; progress
prints every 25 s including during the native decoder call. Unprocessed rows and
metadata rejections are separate from decoded candidates; offline candidates are
never reported as observed runtime joins.

GR2P v2 records explicit native `Format.ColorMaps == 1` and Float4/Byte4
`ColorMapType`, then LSLib `Color0` in RGBA order. Default Color0 on a format
without a color map is never evidence. Missing/multiple/mixed layouts retain no
COLOR_0. Non-finite/out-of-range channels reject. B3PAKS1 v3 appends layout/count
and every ordered RGBA corner after position/UV; absent COLOR uses layout/count 0.

Example after making LSLib v1.20.4 available (replace its path):
`python -m tools.assets.mesh_paks --catalogue <assetdb>/mesh_materials_cov_mesh_material_v2.json --cache captures/cov-paks2/cache --out <assetdb>/mesh_paks_cov_paks2.json --lslib <Tools>`.
The writer streams new `<stem>_0000.bin` tables below 480 MB each and publishes
`<stem>.manifest` last (newline-separated shard basenames, UTF-8 without BOM).
The runtime validates the complete manifest transactionally; its aggregate memory
cap still applies. A catalogue larger than that cap needs a bounded material set
or a future indexed storage path before it can be loaded wholesale.
Do not overwrite any shared generated file. Inventory and candidate output are
local generated data, never shipped. The game is never required for extraction.

Native COLOR publication (cov-paks2): the audited Tileset deferred pair
`a618ddae98c2d1cf/cad41eff979970e4` reads COLOR R/G (`_3.x/_3.y`, FS input
location 0). `common/native_hit_tileset_fields.h` preserves its native branch:
green > 0 selects green, else red > 0 selects red, else base. Camera depth
`781cd8d1d600117d` uses green to select alpha; the deferred colour is never
substituted for its separate coverage recipe. Full position/UV and every native
RGBA corner must match the local decoded row; only float4 and explicit RGBA8
UNORM captured inputs are accepted. Ambiguous/BGRA/unknown layouts fail closed.
The native XFB carries all four channels at byte 24 into the existing material
basis, alongside the nine current texture views and checked native UBOs. Row
padding words 6/7 record the R/G channel mask (3) and explicit RGBA layout (1).
Those words are provenance only; they bypass no resource, clip or alpha gate.
Paks-only operation requires the joined source; the separate native Tileset
feature keeps its existing captured path. Key-off declines paks-only rows.

Lava channel evidence is template-specific. The dumped
`captures/alpha_cache/1a81f41b4ea5e1d5.glsl` (main checkout) for
`FLUID_Lava_Plane_Flowmap` reads COLOR Z/W in its flow displacement and
heat/rock-mask expressions (`_3.z`, `_3.w`, location 0). It also samples two
animated flow/noise layers, four normal samples, distance-dependent blending
and the blackbody LUT. The existing D-EM blackbody record instead consumes one
constant `rt_em_lava_heat`; RGBA recovery alone cannot make that record exact.
`ENV_Lava` forward FS `56c5e5a0ff23fdbc` has a different interface and must not
inherit the FLUID channel mapping. These lava recipes stay unqualified until
their complete native sampling and vertex-varying mappings are implemented.

The requested `ENV_Lava_AlphaTest_Flowmap_Bounce_VT` is a third distinct
case: asset DB SHDR/SREF maps its StaticInstancedDeferred variant to VS
`88d26a9941a51911` and FS `8e6584bfaa46d6f3` (both dumped in the main
checkout's `captures/alpha_cache`). The VS reads COLOR G as normal-direction
animated displacement amplitude and B as animation phase. The FS receives no
COLOR varying: it distorts UV through the animated flowmap before VT sampling.
Its depth/WPO position and deferred UVs need their own per-draw pairing recipe;
static pool geometry or FLUID's Z/W mapping cannot substitute. Local COLOR
recovery retains all channels, but this shader family remains fail closed.

## Bounded full-catalogue generation

Add `--batch-sources 100` to the existing command to process every eligible source
in deterministic lava-first order. Pass the Packed directory containing both
LSLib.dll and granny2.dll (the Tools directory has Divine but no granny DLL).
The reusable PowerShell server compiles mesh_paks_dump.cs once per batch and
calls the same native GR2Reader as the retained one-shot PowerShell reference.
The Python decoder still validates every resulting GR2P stream.

Use a NEW `--out` prefix in the additive asset DB. Each batch writes bounded
binary shards, then JSON and its complete-set manifest. Restart the same command
after interruption to reuse completed batches and per-GR2 caches. An incomplete
publication may require a NEW output prefix; existing shared files are never
replaced. Input catalogue, game package fingerprint and decoder identities must
match before a complete batch is reused. Keep the workspace cache: the JSON
references its immutable geometry and colour streams.

The top-level JSON is an offline summary of batches, never a whole-catalogue
runtime manifest. A complete catalogue exceeds the runtime aggregate cap.
`native_decoded` counts eligible rows whose named native mesh was recovered;
`decoded` counts usable identity candidates after strict geometry/group checks.
`rejected_rows` counts row-level failures, `source_failed_rows` counts eligible
rows blocked by source failures, and `failures` counts error records (not rows).
`joined` remains zero until current resources, cull, COLOR and basis are proved.

The existing mesh_material_replay_audit.py accepts `--pak-summary` to compare
ordered object-space pool positions and UV bits with candidates. SHA-256 only
selects candidates; complete bytes must then match. Soup, missing UVs and sparse
pool captures are excluded. Material ambiguity stays unknown. These geometry
counts are neither hit-weighted coverage nor new qualified material joins.
