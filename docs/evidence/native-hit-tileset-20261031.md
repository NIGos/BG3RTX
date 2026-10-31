# Native STI tileset hit material - bounded provider

This extends `rt_hit_material` only when the native tileset startup capability and
its active key are both enabled. It does not change native coverage or visibility.
No game or GPU test has run for this provider.

Exact pair: depth VS `689836dece58e778` / FS `781cd8d1d600117d`, deferred VS
`a618ddae98c2d1cf` / FS `cad41eff979970e4`. Recipe evidence is in
`native-construction-material-20261030.md`. ST and cached geometry remain unsupported.

Candidate identity uses the six actual active vertex streams (position, three
instance rows with divisor 1, COLOR and UV) and actual indexed/direct draw and
instance arguments. It is only a candidate. The shader additionally compares all
three actual post-VS clip positions, UVs and COLOR values. Repeated candidate keys
remain unsupported. Source and deferred records retain independent recording and
successful-submission ownership and the existing query-private publication proof.

The 80-byte tileset layout is clip 0, UV 16, COLOR 24, N 40, B 52, T 64. Existing
VT and Hair layouts are unchanged. A deferred execution-time gather reads actual
set 1 binding 0 (channel) and binding 1 (Green/Red/Base tilings, reflectance).
Those bytes are distinct from the depth UBO whose last field is SeeThrough.

The nine actual deferred 2D views at set 1 bindings 2 through 10 are appended to
query-private AlphaDSL binding 2, deduplicated with the existing opaque VT page
views. No new descriptor binding is added. The actual set 0 binding 7 sampler is
cloned and retained with the deferred packet. Sampler and material capacities
remain 16 and 128; the new 2D family does not consume array-cache slots. Unknown
views/samplers, unsupported recording state, ambiguity and exhausted storage keep
the existing output. The depth Green view and image identity must match deferred
Green albedo binding 5.

Per hit, strict positive COLOR green wins, then red, then base. The selected UV
is scaled by the actual tiling. Actual view formats, swizzles, mip ranges and the
owned native sampler perform the sampling; no guessed sRGB decode is introduced.
Sampling uses the already declared isotropic ray-cone derivatives, not an assertion
of equality to native raster-quad derivatives. Sampled fields reuse `nv_fields`
and the existing native class 0 parameter / declared FresnelBlend adapter.

The family field and acquisition counters 287/288 are compiled only for
`RT_NATIVE_TILESET`. The combined core must reserve 289 counters before use.
Counter 287 counts qualified family-2 acquisitions; 288 counts their same-frame
primary matches. CPU row counts alone do not qualify runtime material coverage.

Validation checkpoint: 754 MSVC CPU cases and registered CTest pass. Host provider
object and new scalar gather compile; gather SPIR-V validates. All96 affected TileOFF modules are byte-identical to4b62c89; all96 TileON
prototypes compile and validate before M1 integration. Exact hashes and scope are
in native-hit-tileset-20261031-shaders.json. Combined M2 getter,80-byte capture
routing, combined TileON proof and full host link are pending.
No live participation, field agreement, quality or performance claim is made.
