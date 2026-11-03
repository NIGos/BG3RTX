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

Binary B3PAKS1 version 2 carries each sampled candidate, material UUID, flags,
and all ordered position/UV corners. JSON also retains resolved texture UUIDs,
parameters and provenance. Runtime loads `mesh_paks_cov_paks.bin` beside the
asset DB. The sampled ID or 32 corner UVs (groom tolerance 1e-3) select candidates;
full position bits and all corner UVs must agree before HLOD can publish a row.
The HLOD recipe uses current observed descriptors/overrides and the existing
checked XFB basis. Unknown resources, ambiguity, unsupported layouts and
vertex-colour recipes stay unqualified. A UV-only candidate is never a row.

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
