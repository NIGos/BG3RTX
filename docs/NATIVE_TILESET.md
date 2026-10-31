# Native instanced tileset coverage

`rt_native_tileset=0` is default and appears in the experimental launcher group.
Startup enable requires `rt=1`, `rt_alpha=1`, `rt_bvh_split=1`, sufficient native
sampler descriptor limits and a game restart. No installed trial was changed.
Quality and GPU cost remain unmeasured; this is not the nine-texture M3 material
provider or support for every construction shader.

The only recipe is native STI camera depth VS `689836dece58e778` with FS
`781cd8d1d600117d`, from ENV_Tileset_MultipleTextures_AlphaTest. Native deferred
and retained S1 association evidence is in
[the construction material note](evidence/native-construction-material-20261030.md).
Static cached-position, shadow and other pass variants are not enabled by hash
similarity. All RT purposes share this captured camera-depth coverage; native
shadow-pass raster and LOD equivalence is not established by this increment.

The capture reads the same draw's set 1/binding 0 material packet (16 bytes): Green
tiling at byte 0, SeeThrough at byte 12. A finite SeeThrough value exactly equal 1
is unsupported; native code uses exact equality, not a nonzero test. Missing or
nonfinite parameters remain UNKNOWN. The supported native equation is:

- Interpolated COLOR.y > 0: sample Green albedo alpha and keep alpha >= 0.5.
- Otherwise: literal alpha 1, including Red/Base branches.

The real Green albedo view is set 1/binding 1. Its format/swizzle/mips and the
actual set 0/binding 7 native sampler are retained. Samplers must have complete,
current, ordinarily frozen metadata; their actual bias, anisotropy, addressing
and LOD limits are cloned. Comparison, unnormalized coordinates, nondefault
reduction and unsupported sampler extension states remain UNKNOWN. Fixed RT
g_wrap is not substituted. The query uses
declared isotropic world-to-UV ray-cone gradients with this sampler; it does not
claim native raster quad derivatives or a hardware filtering precision proof.
There is no stochastic threshold, opacity erosion or added bias.

Only the exact audited VS gains smooth COLOR vec4 output at stream bytes 24..39;
position remains 0 and UV 16. Startup stride is max(existing stride,40): existing
64-byte native material and 88-byte Hair layouts remain unchanged. All kind 5
draws use fresh dynamic XFB, preserving actual per-instance vertex/output order
and COLOR, including legitimate instance-rate COLOR. The static mesh cache is
not allowed to reuse the first instance's COLOR.

AlphaRec stays 208 bytes: kind 5, native texture slot, native sampler slot in cache,
UV tiling in scale.xy, SeeThrough in p0.x and explicit completeness status in p0.y.
Missing parameters, views, samplers or interfaces retain an incomplete record;
they never silently become opaque or disappear. Actual dropped capture/record
capacity sets the named incomplete bit at AlphaRec TABLE header byte 12 (distinct
from the Hair sceneStamp byte 12). It resets at capture begin and survives scene,
query and private-Sun table copies. All four query entry points return UNKNOWN
or aborted status on incomplete coverage. Current scalar consumers check aborted.

The main and private-Sun layouts reserve binding 19 for a 16-entry sampler table
(dummy 0 plus 15 native slots). Owned clones survive held snapshots, scene/query
retirement and private-Sun remapping; recycled native sampler identities are not
reused. The capability and descriptor layout latch at startup and survive ini
key-off/core reload. Separate RT_NATIVE_TILESET wrappers and pipeline variant
bit 32 preserve every prior macro-off shader, including Scalp-capable variants.
The existing actual-SPIR-V GI/reference checker accepts binding 19 SAMPLER×16
only with its explicit tileset feature flag.

The existing `[alpha tileset]` stats line reports complete/unknown records, native
sampler count, capture-incomplete state and actual stride. Live qualification must
confirm nonzero complete participation, no capture overflow, native cutout
appearance and pass cost from the existing gpu lines in one controlled run.
No GPU/game run is included in the offline candidate checks.

Offline checks: both DLLs and launcher build; 58/58 CTests and QA pass. The
coverage test has 171 CPU checks, 193 with the retained native module. All 639
prior compute entries remain byte-identical, including Scalp-capable variants;
604 new entries compile/validate. The actual GI/reference contract checker passes
80 prior and 80 new modules and rejects sampler 19 without the tileset flag.
Native VS patches validate at 40/64/88; all three key-off patches match the frozen
main patch tool. Details: [checked evidence](evidence/native-tileset-coverage-20261031.json).
