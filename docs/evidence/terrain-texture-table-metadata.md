# Terrain global texture-table metadata

The existing `trace_terrain_packet.request` now opts the existing frame tracer
into `terrain_texture_table` on audited VS64e2568916ede3d6 / FS6650c75f3d3e4968
graphics draws only. No new key, capture transport, texture-byte copy or GPU work.
The ordinary `res` output and its 32-entry per-set cap are unchanged. With no
request or a different family, the additional field and segment-key suffix are
absent.

The independent object records set 2 binding 32, its actual tracked layout's
declared count, set/layout identities, write version and binding flags. It
enumerates each declared element, including unwritten/null and unavailable
views. Each present view uses existing sampled-view metadata: view/image IDs,
current image identity, format, descriptor layout/type, swizzle, mip/layer range
and image mip/layer counts. Image references also join the existing frame image
metadata table. Output is sorted by numeric element, not tracker hash order.

The explicit cap is 4096 metadata entries. Declared, emitted and truncated counts
are separate. Unknown layout/count reports `unknown_declared_range` with null
count; unsupported/mutable allocation/layout reports its own status. A layout
declaration is not an actual variable allocation count. This is passive CPU draw
recording metadata, not a GPU execution/content/residency certificate or a DDS
identity mapping. Existing R5 lacks the requested entries; this change cannot
retroactively recover them.

R5 retained slot 387, requiring a range of at least 388; its actual declaration
was not captured. A synthetic 388-entry declaration fits, including all 12 missing brush slots:
63/64/65, 98/99/100, 208/209/210 and 383/384/385. No texture association is inferred
from numeric order. A subsequent authorized capture remains needed to obtain
their real views and ultimately correlate content/resource identity.

Validation: host DLL linked; 85 CPU selection/count/cap checks, including every
opt-in predicate combination, the R5-sized range, UINT32_MAX count and all 12
required indices. Existing sampled-view test also passes. No live/GPU test run,
shader change, installed profile change or new GPU timing measurement.
