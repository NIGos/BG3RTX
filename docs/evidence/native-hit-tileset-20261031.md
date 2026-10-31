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
object and new scalar gather compile; gather SPIR-V validates. All 96 affected TileOFF modules are byte-identical to 4b62c89; all 96 TileON
prototypes compile and validate before M1 integration. Exact hashes and scope are
in native-hit-tileset-20261031-shaders.json. After merging M1 224b864, all 96
actual native_tileset material wrappers compile and validate; 48 GI and 32
reference interfaces pass 2,456,696 production interface checks. Combined hashes
are in native-hit-tileset-20261031-combined.json. The getter, 80-byte capture routing and core stats capacity 289 are integrated
in the checkpoint below. Runtime participation remains unqualified.
No live participation, field agreement, quality or performance claim is made.


## Combined capture glue checkpoint

The layer now exposes the validated six-stream pipeline getter for the exact
6898/781c depth and a618/cad4 deferred pairs. Both startup controls
`rt_native_tileset=1` and `rt_hit_material=1` reserve at least 80 bytes per vertex;
Hair 88 remains 88. The deferred native outputs are captured directly: COLOR 24,
N 40, B 52, T 64, with clip 0/UV 16. All outputs must be present and smooth before
flags 3 authorizes material capture. Depth keeps separate flags 1 and its original
COLOR 24 coverage contract. Missing or incomplete interfaces do not qualify.
Fresh coverage correctly has no dependency on the static-cache instancing
analyzer; the material join separately validates its six actual input streams.

The existing deferred_begin route and typed query tuple are reused. There is no
new descriptor bank, public ABI or installed setting. Shared counter symbols
reserve 287/288 and core capacity 289; the existing stats report adds qualified
family 2 acquisitions and their native primary matches. These are participation
and geometry/class checks, not a material-field equality or quality gate.

Both DLLs link. Five targeted CTests pass, including unchanged Hair coverage and
M1 coverage, capture budget and actual descriptor contracts. The extended fixture
passes 785 CPU cases, 816 with the retained 2848-byte a618 native module. Its native
80/88-byte patches validate and two ordinary patch outputs match the frozen main
tool. All 16 affected acquisition shaders validate and remain byte-identical to
the retained combined producer proof after symbolic counter substitution; no
whole unchanged shader proof was repeated. Checks ran offline BelowNormal with
at most two compiler jobs. No game or GPU execution, runtime field agreement or
GPU cost measurement was performed. Private evidence is under
`captures/native-hit-tileset-glue-20261031/`.
