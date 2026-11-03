# Mesh content candidate ID v1

## Compact runtime catalogue (cov-paks2, 2026-11-03)

`python -m tools.assets.mesh_paks_compact --summary <full-summary.json> --out
<assetdb>/mesh_paks_cov_paks2_compact_20261103.bin` reads the immutable v3
shards and batch metadata, verifies all geometry/COLOR SHA-256 values and row
conservation, then publishes a NEW complete manifest last. Existing files are
never overwritten. The runtime prefers that compact manifest, falling back to
the previous manifest/table only when the compact manifest is absent. Invalid
compact input publishes nothing. The 512 MiB/file and 2 GiB/set caps remain.

B3PAKS1 v4 keeps the 840-byte v1 prefix (including all 131 canonical words,
64 sampled UV floats, UUID and flags). It appends little-endian COLOR layout
u32 (0 absent, 1 canonical COLOR_0 RGBA), ordered position3/UV2 SHA-256 (32
bytes), ordered RGBA SHA-256 (32 bytes; zeros when absent), texture count u32,
then each parameter length u32, ASCII parameter bytes and 36-byte texture UUID.
Parameters are unique, 1..128 bytes, <=128 refs per row. Flags: two-sided bit 0,
uses-COLOR bit 1, audited HLOD bit 2. No full geometry, colors or cache paths.
Original Float4 versus Byte4 storage is not recoverable from the v3 canonical
layout; current Vulkan attributes still determine the checked runtime encoding.

All 40,983 rows, 6,091 COLOR layouts and 85,630 texture references occupy
41,956,440 bytes (40.01 MiB), versus 33,386,148,008 source binary bytes.
The C++ reader independently parsed the complete compact file. Source deletion
inventory is in its additive JSON companion; no compact operation depends on
those source files afterwards. Texture UUIDs remain provenance, not a bridge
from game UUIDs to VkImageViews. Existing observed descriptor/UBO, shader pair,
cull, clip, basis and resource checks remain mandatory.

### What the ID actually hashes, and why the old audit joined nothing

`layer/bvhsplit.cpp::content_sample` reads the native bound vertex input under
the tracker lock: binding offset + position attribute offset +
`(vertex_offset + index) * effective_stride`, exactly 12 bytes per sample.
Indices start at ib_offset + first_index * index_width. Pipeline float3 support
and current host mapping are required. Instance transforms are NOT applied;
XFB, world positions and mesh-pool bytes are NOT substituted. The sampler
does not apply skinning; if the engine already deformed a bound input buffer,
those current bytes are hashed.
The pak decoder calls the same v1 algorithm on native GR2 object-space float3
and original indices. These IDs CAN agree when native upload floats/index order
agree. There is no evidence that the earlier 0 full-byte replay matches meant
0 native content-ID joins: the replay packs do not contain the native index
stream or input bindings needed to reconstruct that ID.

XFB uses post-VS positions and expands indexed draws to corner order. A
non-indexed capture hashes index_count=0, vertex_span=corner_count and implicit
corner ordinals; a pak row hashes its actual index count/span/relative indices.
Position words also change under transforms, skinning, WPO and rounding.
Stride/padding themselves are excluded, but UV must be read at byte 16 in both
64- and 88-byte captures, not as tightly packed position3/UV2. The mesh pool
inverts the instance transform on the GPU, recovering approximate object-space
positions, not original float bits. Inverting again cannot undo float rounding
or skinning/WPO, nor recover the original indexed topology.

Read-only replay evidence (`captures/cov-paks2/compact_uv_positions_20261103.json`):
S6 0/1,015 UV candidates (36 missing counts, 979 UV mismatches); camp 58 unique
and 4 ambiguous/471; S8 52 unique/602. Full ordered witnesses and sampled position
bit matches are 0 in all three. Representative matching-UV position errors are
5.60e-6 at camp and 2.83e-6..6.68e-6 at S8, consistent with the documented pool
transform/inverse round trip. This explains rejection of these examples; it
does not prove the source of every missing candidate or live material coverage.

### Join and publication

The candidate key is draw index/corner count + 32 ordered UV0 pairs at
floor(s*(count-1)/31), using the existing groom tolerance 1e-3. It survives
position transforms and vertex renumbering when UV0/corner order are preserved;
it does not survive arbitrary topology reorder or animated UV changes.
`mesh_paks::uv_sample` handles indexed native bindings and expanded 64/88-byte
captures. Runtime narrows by count before comparing UVs. Different material,
geometry, COLOR, flags or texture contracts remain ambiguous, including equal
UUIDs with disputed geometry. The original content ID remains diagnostic.

Mapped native inputs can publish only after reconstructing every ordered
position3/UV2 byte and, for Tileset, every normalized RGBA value and checking the
stored SHA-256 witnesses. This permits changed vertex numbering without
trusting a UV-only identity. Compact UV equality is intentionally bit-exact at
the full-witness stage (stricter than legacy v2/3); small UV differences decline.
Device-local UV readback finds candidates but still cannot publish without
that witness. Pool rounding is NOT handled by an invented position epsilon.
Unsupported animated lava recipes remain unqualified. SHA-256 collision
resistance replaces stored full-byte comparison; the 64-bit content hash never
authorizes publication.

`[hit material paks join]` logs cumulative UV attempts, unique candidates,
missing/ambiguous/invalid keys, witness failures, recipe/cull failures, pending
or unsupported inputs, equal/different native samples and total draw attempts.
`draws = attempts + inputs_pending`; `attempts = unique + missing + ambiguous +
invalid`. These are candidate/publication gates, not hit-weighted coverage.
The existing paks line reports full-witness matches and ready material basis.
The unqueued `tools/live_batches/cov_paks_compact` camp job is 75 s plus load,
two ON observations with an OFF interval and explicit restoration. It may
diagnose zero matches; it cannot qualify a visual gain, GPU cost or Ultra parity.
No game was launched. Shader request delta and ABI delta are both 0.

## Earlier full-geometry catalogue contract

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
