# Windowless native coverage probe

The existing `native_hair_gpu_probe` optional target now accepts `--tileset` as
its fourth argument, after bounds/query/query-rebase SPV paths. Default Hair,
Scalp and Pubes mode retains its original cases and resources. Compile-time
`NATIVE_TILESET_PROBE=1` selects the new entry in the same probe shader; no
production host/shader, setting or ABI changes are needed.

Build CPU code with `BG3RTX_HAIR_GPU_PROBE=ON`. Compile
`shaders/rt/hair_bounds.hlsl`, then `tools/native_hair_gpu_probe.hlsl` with
`NATIVE_TILESET_PROBE=1`, then with both that define and `RT_SCENE_REBASE=1`.
Use DXC cs_6_6/main, SPIR-V Vulkan 1.3, HLSL 2021, O3, include paths `shaders`
and `tools`. Validate the three SPVs before running. The ordinary Hair query
shader was separately compiled and is byte-identical to the frozen old probe.

The executable creates no window, surface, swapchain or game process. It uses
the existing private Vulkan path with implicit layers disabled and explicit
Khronos validation required. Clear inherited VK_ADD_LAYER_PATH,
VK_LOADER_LAYERS_ENABLE and VK_INSTANCE_LAYERS in its child environment, as in
the recorded invocation. Run this standalone probe only when GPU tests are
authorized. Use BelowNormal process priority and a 60-second owned-child timeout;
the tileset queue itself requests priority 0. No game attachment/input/focus API.

```text
native_hair_gpu_probe.exe bounds.spv tileset.spv tileset_rebase.spv --tileset
```

Tileset mode reuses the existing triangle AS setup and all four actual
`rt_common` query functions. It does not dispatch Hair bounds. Thirty-two tiny
cases check COLOR at byte 24/Green at 28 for strides 40/64/88, alpha immediately
below/exactly/above 0.5, Red/Base literal coverage, SeeThrough/missing-recipe,
geometry-range and header 12 UNKNOWN, held pool and the rebase shader. The rebase
case uses zero translation and certifies the query variant, not a coordinate-
conversion proof. Unknown closest queries can retain a committed t alongside
aborted=true; consumers must respect the latter.

The float RGBA image preserves adjacent FP32 threshold values. The additional
native sampler 19 uses opaque-white clamp-border while the legacy sampler uses
repeat. At UV2.5, alpha0 image gives native 1 versus legacy 0; actual kind5 rays
must occlude. This verifies sampler selection, not all native sampling metadata
or native raster derivatives.

One authorized invocation on RTX 5090 passed all 32 cases with zero validation
errors. No presentation or BG3 launch occurred. The process completed in 3137 ms;
that wall time is not a pass GPU-cost measurement. Existing Hair mode was not
rerun on GPU. Evidence hashes and scope are in
`docs/evidence/native-tileset-windowless-gpu-20261031.json`; raw local outputs are
under `captures/tileset-windowless-probe`. The inherited summary's bounds flag
was corrected after this run, with no GPU behavior change or second invocation.

Uniform vertex COLOR exercises the production interpolation expression, but is not a varying-COLOR oracle. Held pool reads are exercised at stride 88. Native graphics XFB execution, game capture, anisotropic 16 filtering precision and raster derivative equivalence are not certified by this synthetic fixture.

## Optional material packet mode

The same executable now accepts:

```text
native_hair_gpu_probe.exe bounds.spv material.spv material_rebase.spv --material gather.spv join.spv
```

Compile the existing probe shader with `NATIVE_MATERIAL_PROBE=1` (and additionally
`RT_SCENE_REBASE=1` for its second query module). The final two modules are the
unchanged production `rt/hit_material_tileset_gather.hlsl` and
`rt/hit_material_join.hlsl`. The bounds pipeline is created but not dispatched.
No production source, descriptor layout, setting or public ABI is changed.

Material mode adds only its private bindings 12/13/14/15. Nine distinct 1-pixel
RGBA32F 2D views provide three normal, three albedo and three physical samples;
these views select separate layers of one tiny image. Actual sampler 15 slots
provide contrasting repeat and opaque-white border behavior. Production scalar
gather and join dispatches assemble the query-owned packet before an actual
`rt_closest_hit` result enters the production `rt_hit_material` decoder. Declared
synthetic 80-byte source/deferred vertex buffers supply clip, UV, COLOR and NBT;
this mode does not perform native graphics capture or invoke the host producer.
The source-coverage texture is separately declared opaque to isolate material
sampling; host Green-view identity validation is not exercised.

One authorized windowless invocation on RTX 5090 passed all 14 cases in 1.217 s
wall time, with zero validation errors through destruction. Green/Red/Base
albedo, roughness, metalness, reflectance, frame-normal and channel/family checks
passed. Green priority, interpolated COLOR and actual border-sampler behavior
passed. Clip/UV/COLOR mismatches returned status 4; zero publication returned
status 1; invalid scalar and wrong snapshot identity returned status 3. Rejected
publication/scalar/identity packets remained unjoined; clip/UV/COLOR packets
were rejected by the per-hit proof. Geometry hits remained at distance 3. This is
GPU packet/decoder evidence, not native asset/view provenance, host recording
ownership, G-buffer equality, in-game image quality, a performance benchmark or
a four-query material pass. Rebase uses zero origins and is not a nonzero
coordinate-conversion certificate. Existing Hair and tileset query modules keep
all four ordinary/rebase shader identities.

Raw evidence is retained at `captures/native-hit-material-gpu-20261031/`.
The raw inherited footer incorrectly printed `production_four_queries=1` for
material mode: only `rt_closest_hit` ran. That log-only footer was corrected and
the executable rebuilt without another GPU invocation. Versioned JSON records
both tested and rebuilt binary hashes, the exact fields, module hashes and this
exclusion. The tested executable remains beside the raw run. See
`docs/evidence/native-hit-material-windowless-gpu-20261031.json`.
