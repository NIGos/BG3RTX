# Mesh content candidate ID v1

Runtime implementation: `layer/mesh_content_id.h`, used by `layer/bvhsplit.cpp`
only with `rt_mesh_content_id=1`. This is a sampled candidate fingerprint, not
proof of mesh equality and not a material qualification path.

The catalogue reader `mesh_materials.py` parses LSF VisualBank metadata only.
`tools.assets.locate.lslib_tools()` returns an empty string on this machine:
LSLib.dll and its GR2 conversion tools are absent. There is no GR2 vertex/index
decoder in this catalogue tool. Thus no pak content IDs can be generated yet.
Needed: decode the exact GR2 ObjectID / LOD / submesh raw object-space float3
positions and ordered triangle indices, retaining position bits and native
vertex addressing. UV/tangent decoding is also needed for the later exact join.
Do not hash VisualBank strings, bounds or triangle counts as a substitute.

For a future offline counterpart, v1 canonical words are:

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
bytes can alias and are not counted as detectable collisions. Full canonical
byte equality, unsupported-layout rejection and material disambiguation remain
mandatory before a future pak row may qualify. No ABI or shader changes.
