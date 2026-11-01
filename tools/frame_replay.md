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
  One VkImage per pack file (the writer names a shared image from every element that binds it);
  a note counts the images the writer stored decoded (`image ... SOURCE`).
- Output in `--out` (default `<pack>/replay`): `giRaw.raw` (RGBA16F), `giRaw.pfm` (RGB float),
  `giRawDepth.raw`, `stats.txt`, `pass.spv`. The console prints the dispatch GPU time and the live
  log's lines for this pass with "1 frames": `rt stats ... hybrid GI`, `GI hit direct`,
  `GI hit direct lights`, `GI hit direct policy`, `hit material` (wording of core/gi.cpp), plus a guard
  line. With a live `output ... FILE` reference it prints the bit-equal share and mean/max |diff|.

## Pack format (common/replay_pack.h)

A folder with `manifest.txt` (grammar in the header) and raw files: `push`, `buffer`/`range` (sparse
buffer slices at their real offsets, so record offsets stay valid), `image` (VkFormat, mips, layers;
an optional last field names the game format of an image stored decoded),
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
Checks 9-11 run the writer's image read (`shaders/replay_image.hlsl`, the same piece plan) on 13 x 9
x 3-layer, 3-mip images of all 37 mapped formats: uncompressed texels keep their bits (BGRA8 as
RGBA8), every stored texel is within half a step of the game format's decoded value, and the piece
split does not change the bytes. RTX 5090, 2026-11-01: BC1/2/3/7 (UNORM and sRGB) and BC6H come back
bit-exact (0.00 steps); BC4/BC5 0.50 / 0.49 steps (the decoder keeps more than 8 bits); A2B10G10R10
0.50 steps of 16 bits.
Pack 3.5 MB (the hit-record table is 3.1 MB of it). Wall about 3.3 s; dispatch 0.004 ms (RTX 5090).

## Size (at most 2 GB per pack, `bg3::replay::kPackHardCapBytes`)

The geometry is taken whole while the never-dropped part stays under 1 GB (`kPackBudgetBytes`); the
textures fill the rest up to the 2 GB hard cap (`rp::fill_budget`): highest `rp::texture_priority`
first (2: gHitCache b14 and the table gAlphaTex b2, whose VT page tables and 2D views the hit
material samples too; 1: the alpha tile caches b3), within a priority smallest first, each image
that still fits. An image larger than what is left is dropped and the smaller ones still go in.

S8 packs (2026-11-01), what they measured:
- All real textures have TRANSFER_SRC and are copied raw (BCn kept): the first pack copied all 32
  table elements (20 images, 67 MB); the 4064 elements without it were the layer's 4 x 4 dummies.
  So `read by shader` counts the two dummies (2D and array), not game textures.
- The VT caches: the first pack omitted 3 cache uses as larger than a 256 MB staging chunk. The
  second (055812, 906 MB) kept no texture: `images omitted 4099 (... layout 1, budget 4098); images
  copied 7, read by shader 1`. The old order (alpha-only first, largest first) dropped every table
  element and the alpha cache, then still had to drop the hit cache b14[1]: so b14[1] alone is
  larger than the 1142 MB left (2048 - 906). b14[0] was omitted as "two views of one image": the
  same VkImage as the alpha cache b3[1] (at least 256 MB) through another view.
- So the two caches together exceed the cap even with nothing else in the pack. With the fill the
  pack keeps the tables, the dummies and every cache that fits: expected 906 + 67 MB + b3[1]
  (if under 1075 MB); b14[1] stays dropped. Carrying both needs a cap of more than 2.4 GB (906 + 67
  + 256 + 1142 MB, all lower bounds; the textures line gives the real sizes) or a tile capture of
  only the cache pages the hits sample (the pass's own feedback: core and hit-material shader,
  outside the replay files). Tiles named by the hit tables' page tables would likely not shrink it:
  the 16 hit tables are the scene's tile sets, whose page tables point at nearly every resident tile.
- Two views of one image now share one capture when their formats store the same bytes (transfer
  copy, `rp::raw_compatible`, e.g. BC7 UNORM / SRGB: each `image` line keeps its view's format) or
  are shader reads of the same range (another format: its own read). Other differences stay
  omitted; the note `two views of one image (omitted): ...` names the mips, layers and formats.
- Measured per pack: the note and log line `[replay] pack <n> textures: <MB> MB free under the cap
  for <n> droppable images of <MB> MB; kept ..., dropped ...; by binding (kept/images, MB kept/all):
  ...; largest: <bindings>[<element>] <VkFormat> <w>x<h> x<layers> m<mips> copy|read <MB> MB
  p<priority> kept|dropped; ...` (the 10 largest).

Estimate for a 2560 x 1440 render frame, to be replaced by the first live pack's own line:
- set 0 images: depth 4 + normal 4 + velocity 4 + prevRadiance 8 + rtOrigin 8 B/px = 103 MB;
  live giRaw / giRawDepth reference (half resolution) 11 MB; light lists, UBOs, hdSlots < 3 MB.
- set 1: hit records 3.1 MB + pool rows 1 MB; InstInfo + instances 80 B per instance (5 MB at 60k);
  soup and pool slices only where an instance points (3 x stride x tris each: about 200-400 MB at
  2-4 M unique triangles; static meshes counted once per BLAS key, not per instance); pool basis ranges.
- textures: the elements the material set binds (gAlphaTex, gAlphaCache, gHitCache; unused slots
  hold the layer's 4 x 4 dummies) with their mip chains. Only they are dropped (`omitted`), as above;
  geometry and set 0 never (a pack whose never-dropped part passes the 2 GB hard cap
  `kPackHardCapBytes` is "over budget" and not written; its log line lists the MB per slot).
- Only each descriptor's bound bytes are captured (`rp::bound_bytes`). A set-1 buffer bound inside
  the soup or pool VkBuffer reuses the geometry's slices (`rp::alias_ranges`, `range` lines into the
  soup's file): with the split BVH gAlphaSoup b1 binds the whole soup like b6. First live pack (S8,
  2026-11-01): 1874 MB without textures, of which b1 whole was 1408 MB; set 0 + other buffers about
  157 MB, pool spans 73 MB, soup spans 237 MB. Expected now: about 160 MB + pool whole 512 MB (it
  fits the budget) + soup spans 237 MB = about 0.9 GB. The record line and a manifest note give the
  MB per slot (`by slot: ...`).

## Live writer (layer/replay.cpp, core/gi.cpp `replay_after_hit_gi`; ABI 25)

Key `replay_pack` (bg3rtx.ini, 0 off; not in the GUI presets): a positive serial takes one pack at the
next base-variant `gi_hit_direct` dispatch, once per serial (kept across core reloads); -1 one pack with
every trace (hotkey, `trace.request`, auto). Diagnostics only: the layer built with BG3RTX_DIAGNOSTICS=OFF
leaves `Bg3HostApi::replay_pack` null and the core does nothing. Capture only, no rendering change.
1. Core (hot-reloadable): right after the pass's `vkCmdDispatch` it calls the one new host call
   `int replay_pack(cmd, const Bg3ReplayDispatch*)` with the pushed constants, the push descriptor set 0
   as pushed, the output bindings (b8 giRaw, b13 giRawDepth), the material set 1 and the TLAS. A
   gi_hit_sun or SHaRC variant is refused (`[replay] pack N failed: ... variant`).
2. Layer (stable): it wraps a few `dd->vk` entries (image, view, sampler, buffer creation; the material
   sets' allocation and the layer's own writes and copies into them: tracker.cpp marks the game's
   vkUpdateDescriptorSets so those are skipped). From those records it plans and records, in the same
   command buffer, copies into host-visible staging chunks (256 MB each):
   - set 0: UBOs from the game's host mapping at record (or a transfer copy), SSBOs and images by GPU
     copies (depth as R32F), outputs after the dispatch as the live reference (`output ... s0b8.live`);
     rtStats (b20) is left out (the replay counts into its own buffer);
   - set 1: whole buffers (records, alpha records/soup, stamp, InstInfo, hit records, basis); the soup
     b6 and pool b8 whole when they fit the budget (pool first: its basis rows lie outside the
     InstInfo spans), else the InstInfo spans merged (4 KB gaps) as `range` slices of one file
     (`range S B OFFSET FILE FOFF BYTES`); every image element the set binds (gAlphaTex, gAlphaCache,
     gHitCache: what the alpha test and the hit material can index) with all mips of its view (both
     sample with a computed LOD, so any mip can be read), each VkImage once; the samplers (b4/b5
     immutable, b15 as written);
   - the TLAS instance array of the scene ring region whose InstInfo set 1 binds (rt_replay_region).
   Buffers without TRANSFER_SRC are copied by `shaders/replay_copy.hlsl`. Images without TRANSFER_SRC
   (most game textures, the VT caches, the layer's dummies), or whose view swizzles, are read by
   `shaders/replay_image.hlsl` through a new SAMPLED-only 2D-array view of the game view's format and
   mapping, in their descriptor layout (no transition), and stored decoded (`rp::shader_read_format`):
   BC1/2/3/7 as RGBA8 (sRGB kept: the replay's sampler linearizes and filters as the game's), BC4/BC5
   as R8/RG8 (UNORM or SNORM), BC6H as RGBA16F, BGRA8 as RGBA8, B10G11R11 / E5B9G9R9 as RGBA16F; the
   manifest's `image` line names the game format. Why 8 bits: on the RTX 5090 BC1/2/3/7 decode exactly
   to 8-bit values and BC6H to halves (self-test 10), so those replays sample the same texels; BC4/BC5
   are within 0.5/255 (R16 would be exact-er at twice the size, which the 2 GB cap cannot afford).
   Images and copies larger than a staging chunk are split (`rp::shader_read_pieces`,
   `rp::transfer_pieces`: mips, layer groups, block rows). Still `omitted`: integer formats and 24-bit
   depth without TRANSFER_SRC, non-2D images, views outside a shader-readable layout, two views of one
   image, and the budget; the counts per reason and the omitted VkFormats are the manifest's first notes.
3. When the GPU has run that frame (life_gpu_done, no wait) a worker thread checks the staging marker,
   writes `captures/replay-<stamp>-<serial>/` and logs
   `[replay] pack <serial> written: <MB> MB (<n> images omitted, <r> read by shader), frame <f>, pass gi_hit_direct, instances <n> -> <dir>`
   (or `failed: <why>` / `over budget: <MB> MB without textures`). At record it logs
   `[replay] pack <serial> recorded at frame <f>: <n> image copies, <r> image reads (<b> BCn decoded, <MB> MB in <p> pieces), ...`
   with the buffer copy counts and the geometry mode.
Cost: one frame records the copies and reads (a few ms of CPU, GPU copies of up to 2 GB) and allocates
the staging; the file writing is off the render thread. Not measured live yet (`READY_FOR_LIVE`).
CTests: `replay_pack_test` (manifest writer round trip with the SOURCE field, InstInfo spans, the
budget fill with priorities and the S8 case, raw-compatible formats, shader-read format mapping and
pieces, transfer pieces); `frame_replay_test` writes its synthetic pack with the same writer,
replays it, and runs the image read on every mapped format.
Open: the exact VT cache sizes, formats, layers and mips at S8 (the next pack's `textures` line),
then the choice between a larger cap for this one job and a hit-sampled tile capture.
