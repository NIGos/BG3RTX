# Offline frame replay (gi_hit_direct)

Re-run one production pass on one captured frame without the game (docs/OFFLINE_OPPORTUNITIES.md A).
Pass: `gi_hit_direct` (`shaders/gi/gi_trace_rt_hit_direct.hlsl`, base variant). It was chosen over
`rf_hit_shade` because it reads only two sets (the GI push set and the material set 1): no box
lights, no probes, no sky cubes, no sun set. `rf_hit_shade` needs those in the pack first.

## Harness: `bin\frame_replay.exe` (tools/frame_replay.cpp)

```
frame_replay --pack <dir> [--out <dir>] [--hlsl <file> | --spv <file>] [--define N=V]... [--repeat N] [--no-validation]
frame_replay --synthesize <dir> [--dark]
frame_replay --self-test <dir>          (CTest frame_replay_test)
```
- Windowless, own Vulkan device, implicit layers disabled (never the game layer), below-normal priority.
- On every run it compiles the pass shader from this checkout's `shaders/` with the layer's DXC flags
  (layer/shaders.cpp: vulkan1.3, HLSL 2021, cs_6_6, `BG3RTX=1`, SHaRC headers, `-O3`, the file's
  `// dxc:` line, the pack's `defines`). Edit the shader, rerun: about 3 s, of which DXC is most.
- It rebuilds one BLAS per distinct instance key from each instance's InstInfo (base, stride, tris;
  opaque soup or mesh pool, opaque unless kInfoAlpha), as layer/bvhsplit.cpp does, then the TLAS from
  the pack's instance array in its order (InstanceIndex = InstInfo index). Legacy soup, hair AABB and
  empty instances are kept with mask 0 and counted in a note.
- Every descriptor of the pass contract (common/replay_pack.h `kGiHitDirect`) is bound: pack data,
  else a zero dummy (listed as "not in the pack"). An image the writer `omitted` is bound NaN, so a
  material lookup that needs it fails as "sampling" (reason 6) instead of shading with garbage.
- Output in `--out` (default `<pack>/replay`): `giRaw.raw` (RGBA16F), `giRaw.pfm` (RGB float),
  `giRawDepth.raw`, `stats.txt`, `pass.spv`. The console prints the dispatch GPU time and the live
  log's lines for this pass with "1 frames": `rt stats ... hybrid GI`, `GI hit direct`,
  `GI hit direct lights`, `GI hit direct policy`, `hit material` (wording of core/gi.cpp), plus a guard
  line. With a live `output ... FILE` reference it prints the bit-equal share and mean/max |diff|.

## Pack format (common/replay_pack.h)

A folder with `manifest.txt` (grammar in the header) and raw files: `push`, `buffer`/`range` (sparse
buffer slices at their real offsets, so record offsets stay valid), `image` (VkFormat, mips, layers),
`omitted`, `sampler`, `instances` (VkAccelerationStructureInstanceKHR as read at submit, the BLAS
address kept only as a key), `output` (+ the live result). The geometry is not duplicated: it is the
soup / pool slices the InstInfo entries name.

## Synthetic pack and CTest `frame_replay_test`

`--synthesize`: a 128 x 128 frame (GI 64 x 64, 8 rays) of a back plane, a slab whose front face is in
the depth buffer and whose back face (20 cm behind it) carries an opaque hit-material row (real 8995
recipe, all-zero page table, 1 x 1 tile caches: albedo 0.8/0.6/0.4, roughness 0.5) and one point light
between slab and plane. GI rays from the plane hit the back face on screen but hidden, so the real
`rt_hit_material` and `native_hit_direct` run. The test writes a lit and a dark (light colour 0) pack,
replays each twice from disk and checks: rays and hits; candidates = qualified (356, all reason 0);
supported = nonzero (356); two dispatches bit-identical (images and counters); outputs finite; dark:
bounce exactly 0, same candidates; giRaw.a bit-equal lit vs dark (occlusion does not depend on the
light); no validation errors. Skips (77) without a ray-query device or the validation layer.
Pack 3.5 MB (the hit-record table is 3.1 MB of it). Wall about 3.3 s; dispatch 0.004 ms (RTX 5090).

## Size (target < 1 GB per pack, `bg3::replay::kPackBudgetBytes`)

Estimate for a 2560 x 1440 render frame, to be replaced by the first live pack's own line:
- set 0 images: depth 4 + normal 4 + velocity 4 + prevRadiance 8 + rtOrigin 8 B/px = 103 MB;
  live giRaw / giRawDepth reference (half resolution) 11 MB; light lists, UBOs, hdSlots < 3 MB.
- set 1: hit records 3.1 MB + pool rows 1 MB; InstInfo + instances 80 B per instance (5 MB at 60k);
  soup and pool slices only where an instance points (3 x stride x tris each: about 200-400 MB at
  2-4 M unique triangles; static meshes counted once per BLAS key, not per instance); pool basis ranges.
- textures: only the elements the frame's records and alpha records index (gAlphaTex, gAlphaCache,
  gHitCache) with their mip chains. They are dropped first, largest first, when the total passes the
  budget (`omitted`); geometry and set 0 are never dropped (a pack over budget without textures is
  "over budget" and not written).

## Live writer (NOT built yet; the live job tools/live_batches/replay_capture waits for it)

Key `replay_pack` (serial, 0 off; diagnostics only, `BG3RTX_DIAGNOSTICS`; capture only, no rendering
change). On a new serial, at the next `gi_hit_direct` dispatch:
1. Core (core/gi.cpp `hit_gi_dispatch`, hot-reloadable): after `CmdDispatch`, copy into host buffers
   (`alloc_host_buffer`) every set-0 resource from its `writes` (buffers by VkDescriptorBufferInfo;
   game images from the Snapshot `Bg3Binding` (render targets have TRANSFER_SRC, tracker.cpp
   force_transfer_src); core images giRaw, giRawDepth, prevRadiance, rtOrigin, hdSlots need
   TRANSFER_SRC at `alloc_image`), plus `hp` (the pushed constants) and the groups `(g_hw+7)/8`.
2. Layer (needs one host call, ABI 25: `replay_scene(cmd, tlas, query_alpha, dir)`): resolve the
   query AlphaDSL to its build's buffers (alphatest.cpp: rec, soups, InstInfo, pool, stamp; the hit
   records/basis of hit_material.cpp), copy the instance array of that scene slot after the submit
   fixup (bvhsplit.cpp ring, host-mapped), the InstInfo-referenced soup/pool slices and pool basis
   ranges, and the referenced texture elements (tracker view -> image) within the budget.
3. Write `captures/replay-<stamp>-<serial>/` after that frame's fence (one wait, diagnostics only),
   then log `[replay] pack <serial> written: <dir>, <bytes> bytes (<n> images omitted), frame <f>,
   pass gi_hit_direct, instances <n>` (or `[replay] pack <serial> failed|over budget: <why>`).
Open questions for that step: whether the AlphaDSL's descriptor contents can be read back from the
layer's own write records (alphatest.cpp) rather than re-derived; the VT cache sizes at S8.
