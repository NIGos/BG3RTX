# Track D: hardware ray tracing — phase 1, scene geometry capture

Goal of phase 1: prove that the frame's opaque scene geometry can be captured
every frame in camera-relative world space, as the input for BLAS/TLAS builds and
ray queries. **Result: it can, exactly.** All 835 draws (3,163,516 triangles) of
the G-buffer pass are captured; re-projected, they reproduce the frame's depth
buffer on 97.8% of pixels to 0.2%. 0% is missing, and the remaining 2.2% is
alpha-tested geometry that our triangles cover where the game discarded pixels.
Capture costs +0.45 ms GPU per frame. A BLAS refit of the same geometry costs 0.55 ms
and a full rebuild 2.5–3.4 ms on the RTX 5090. Validation-clean.

Captures (Steel Watch Foundry, the auto-run save), each with a `geo/` folder:
`captures/20261026-035645`, `-040924`, `-041511` (under validation) and `-041646`
(final design).

## 1. Survey: how the G-buffer vertex shaders form positions

Source: the decompiled shaders of `captures/20261025-235042` (`shaders/*.glsl`) plus
the draw census of its `frame.json`. The G-buffer pass (5 colour targets + D24S8) has
**835 draws, 36 distinct vertex shaders and 53 VS/FS pairs**, all `vkCmdDrawIndexed`
(no indirect draws). Every one of the 36 ends in the same two lines:

```glsl
precise vec4 W4  = vec4(P_object, 1.0) * M_instance;     // or W = W4.xyz + displacement
precise vec4 C   = vec4(W, 1.0) * cam._m2;                // set 0 / binding 2, ViewProj, row-major
C.y = -C.y;  gl_Position = C;                             // (sky: C.z = 0 as well)
```

Only the source of `M_instance` and the displacement added to `W` differ:

| Class | Position path | VS (draws in the capture) |
|---|---|---|
| A. Instanced static | `mat4(attr4, attr5, attr6, (0,0,0,1))`: the instance matrix rows are vertex attributes (per-instance stream) | 2d2ac3c3 (360), a618ddae (105), 6044c8cf (82), 54df894a (23), 2aff0bdc, c32ce535, 6660248, f474891e, 5575fc19, 646a930e; 6daa7974 = sky dome (forces z = 0) — 11 variants, ~600 draws |
| B. Per-draw UBO matrix, optionally GPU-skinned | `M = set1/b0._m0`; if `_m2 > 0` (or always) the position is read from SSBO `set0/b13` at `(gl_VertexIndex + base - 1) * 20` bytes, the output of the skinning compute pass (`cs 718d8329`) | 3812bc98 (100), f111e292, b1e18895, 1ed985dd, 939d1385 (switchable); 1cf36b97, e9407368, 41a168e2, 61239daf, 17970d7c, 13a9c9f5, 3927925b (always skinned) — 12 variants |
| C. Matrix assembled in the shader | from UBO/attribute data (`_44`, `_75`, `_76`, `_118`) | 5c258c7b, 1e9145e4, 0de1b186 (24), 5b0f1fc1 — 4 variants |
| D. Instanced + vertex animation | class A matrix, then `W += wind / cloth displacement` (noise, `textureLod`, per-material UBO params) | 50fbfceb, 735d5a19, 105e47e4, 80b73bae, 4121c54e, 8e93bb6b, e5dd82b6, 27ef2b49 — 8 variants (foliage, banners, chains) |
| E. Terrain | patch offset from `set?._m0[gl_InstanceIndex].xz`, height from a heightmap `textureLod`, then `* _m0` | 64e25689 (3 draws, 45k triangles) |

So positions come from four different places: vertex attributes, a per-draw UBO, a
skinning SSBO, and textures. Displacement is added after the instance transform. But the
last step is identical everywhere, and the space just before `* _m2` is the one we want.

### The camera UBO (set 0, binding 2, 224 bytes)

Measured in the capture (`report.json` → `camera_ubo.floats`):

| Offset | Member | Content |
|---|---|---|
| 0 | `_m0` mat4 | **View** (absolute world → view; rotation plus translation) |
| 64 | `_m1` mat4 | **Projection**: reversed infinite Z, near 0.1 at `[3][2]`, DLSS jitter in `[2][0..1]` |
| 128 | `_m2` mat4 | **ViewProj of camera-relative world**: its translation row is `(0, 0, 0.1, 0)`, so the camera sits at W = 0 |
| 192 / 196 | `_m3`, `_m4` | near 0.1, far 1500 |
| 208 | `_m6` vec3 | camera position in absolute world (e.g. −320.9, 19.9, −132.5) |

`W` is therefore **camera-relative world space**: world axes (Y up), origin at the eye.
Absolute world = `W + _m6`.

**Correction to FRAME_MAP:** the G-buffer normals (RT0) are *view space*, not
world-aligned. With the view rotation of `_m0` the captured triangle normals agree
with them at |dot| median 0.998 (94% of samples > 0.9). Without it the median is 0.77.
TRACK_C_GI.md (`set1/b1`, "view space") was right.

## 2. Mechanism: transform feedback of `gl_Position`

| Option | Verdict |
|---|---|
| (a) SPIR-V patch: store the pre-projection position to an SSBO | Works, but every variant needs its `W` located (it is a different SSA value in each of the 36 shaders, and classes C and D compute it inline). It also needs `vertexPipelineStoresAndAtomics`, a per-draw base offset (a push constant or a pipeline layout change), and index buffers to rebuild triangles from per-vertex data. |
| **(b) VK_EXT_transform_feedback** | **Chosen.** The patch is three decorations and one execution mode on the `Position` output, the same for every shader. XFB outputs whole primitives (indexed lists and strips are unrolled, instancing expanded), so it delivers a triangle soup with no index buffers. It covers skinning, wind, terrain and anything else by construction. |
| (c) CPU: read vertex and index buffers, rebuild transforms | Would have to reimplement GPU skinning, wind and terrain displacement, and read instance streams per class. Fragile, and needs readbacks. |

The cost of (b): XFB captures clip space, not `W`. Recovering `W` is exact, though.
`clip = [W, 1] · VP`, so the x, y and w rows of `VP` give a 3×3 linear system for `W`
(solved in double). It does not use z, so it is immune to the sky's `z = 0` trick and
to any depth bias. The residual on z then flags such vertices (3,072 sky vertices here).

### Implementation (`layer/geocapture.cpp`, `layer/spirv_xfb.h`)

- **Device.** With `geo_capture=1` (read at `vkCreateDevice`, so restart the game), the
  layer appends `VK_EXT_transform_feedback` and a `VkPhysicalDeviceTransformFeedbackFeaturesEXT`
  to a *copy* of the game's create info. The game's structs are not touched. If the
  driver refuses, the layer creates the device unmodified.
- **Shaders.** Every vertex shader module gets `OpCapability TransformFeedback`,
  `OpExecutionMode Xfb`, and `XfbBuffer 0 / XfbStride 16 / Offset 0` on its Position
  output (a variable or a `gl_PerVertex` member). Modules with GS/tess entry points or existing XFB
  layouts are skipped. Hashes, dumps and pass signatures use the game's original
  SPIR-V. `tools/xfb_patch_test.exe` patches the whole shader dump offline: **2385 of
  2385 vertex shaders patch, and all pass `spirv-val`**. A pipeline is only captured
  if its VS module was patched, the VS is its last pre-raster stage, and its topology is triangles.
- **Capture** (the traced frame's G-buffer pass, found by its signature): every draw
  is self-contained.
  - Before the draw: `vkCmdBindTransformFeedbackBuffersEXT` at a CPU-known offset
    (running sum of `indexCount/3 × instanceCount`; an upper bound for strips, an
    8 MB slab for indirect draws), then `vkCmdBeginTransformFeedbackEXT` with no
    counters, then a `VK_QUERY_TYPE_TRANSFORM_FEEDBACK_STREAM_EXT` query.
  - After the draw: end the query, then `vkCmdEndTransformFeedbackEXT`.
  - The query's primitives-written count gives the actual size. Written vs. needed
    detects overflow (bound size too small).
  - The XFB buffer is 768 MB of host-cached memory.
  - Two designs failed first:
    - Resuming XFB through counter buffers garbled the result: 131k of 3.16M triangles,
      wrong ranges. The counter written by End is not visible to the next Begin without
      a barrier, which a render pass cannot hold.
    - Keeping XFB active across the whole pass worked (identical coverage), but the game
      binds pipelines between draws. That is illegal while XFB is active
      (`VUID-vkCmdBindPipeline-None-02323`, found by the validation run).
- **After the pass** (outside it, in the game's command buffer):
  - Copy the depth (D24S8, depth aspect) and RT0 to host buffers.
  - Copy the camera UBO. Its buffer has only `UNIFORM_BUFFER` usage, so `vkCmdCopyBuffer`
    cannot be used. A 14-thread compute shader (`shaders/geo_ubo_copy.hlsl`, push
    descriptors) copies it. The game's compute bindings are then restored with `restore_bindings`.
- **Analysis** (present thread, once, ~0.9 s):
  - Solve `W`. Vertices that are non-finite in clip space are counted and kept out: the
    game parks culled vertices at NaN (73k vertices).
  - Rasterise the clip-space soup on the CPU (16 threads, reversed Z, w-clipping) and
    compare it per pixel with the game's depth.
  - Compare triangle normals with the G-buffer normals.
- **Outputs** in `captures/<ts>/geo/`:
  - `scene.bin`: header, camera UBO, viewport, draw table (range, VS/FS hash, pipeline,
    index count, instances), clip and world positions. Format in `scripts/geo_view.py`.
  - `report.json`: counts, coverage, per-VS triangles and matched pixels, the camera UBO.
  - `coverage.png`: green = match, red = ours in front, blue = ours behind, magenta = missing.
  - `depth_game.png`, `depth_ours.png`, and `normals_sample.bin`.
  - `scripts\geo_view.py <capture> [--obj]` writes `topdown.png` and, on request, `scene.obj`.
    `geo_obj=1` makes the layer write the OBJ directly (~600 MB).

Settings: `geo_capture`, `geo_xfb_mb` (768), `geo_max_draws` (16384), `geo_depth_tol`
(0.002), `geo_obj`, `geo_timing` (G-buffer pass GPU time, logged every
`geo_timing_frames`), `geo_xfb_always` (XFB into device memory every frame, for the cost
measurement), `geo_patch_dump=N`.

## 3. Results

### Coverage (`captures/20261026-040924/geo/report.json`; `-035645` agrees to 0.01%)

| | |
|---|---|
| Draws captured | **835 / 835**, 36 / 36 vertex shaders |
| Triangles | **3,163,516**, exactly Σ indexCount/3 · instances over all draws; 0 dropped, no overflow, 145 MB |
| Depth pixels with geometry | 2,731,520 (100%: the sky dome writes depth too) |
| **Match** (within 0.2% of the game's depth) | **97.82%** |
| Ours in front | 2.18%: alpha-tested grates, railings, chains, cut-out planks (red in `coverage.png`) |
| Ours behind | 0.003% (73 px, on silhouettes) |
| Missing | **0%** |
| Normals vs G-buffer (view rotated) | \|dot\| median 0.998, p10 0.98, 94% > 0.9 |
| Eye position solved from VP | (0, 0, 0): camera-relative confirmed |

The 2.2% "in front" is the any-hit problem for phase 2. Those triangles need their
alpha mask, which means UVs and the material texture. XFB can capture the UV output too
(location 0 in the class A shaders) with one more `Offset` decoration.

### Cost per frame (RTX 5090, 2560x1067 internal, G-buffer pass GPU time, median of 300 frames, `geo_timing=1`)

| | G-buffer pass |
|---|---|
| Unpatched shaders (`geo_capture=0`) | 0.240 ms |
| Patched shaders, XFB idle | 0.242 ms (the patch is free when inactive) |
| XFB active every frame (`geo_xfb_always=1`, 145 MB into device memory), final per-draw design | 0.685 ms (**+0.45 ms**) |
| (same, earlier pass-wide XFB, not spec-valid) | 0.836 ms |

Not yet measured, estimated by bandwidth: clip → world conversion of 9.5M vertices
(16 B in, 12 B out) is about 0.15 ms of compute. The one-off capture frame (host-memory
XFB, readback, CPU raster) costs about 1 s and is diagnostic only.

### BLAS/TLAS over the captured frame (`bin\geo_blas_bench.exe <scene.bin> 7`, own Vulkan 1.3 device, median of 7)

| Variant | Build | Refit (update) | Size |
|---|---|---|---|
| One BLAS, `PREFER_FAST_BUILD` | 2.52 ms | – | 162 MB (+24 MB scratch) |
| One BLAS, `PREFER_FAST_TRACE \| ALLOW_UPDATE` | 3.37 ms | 0.56 ms | 182 MB |
| 835 BLAS (one per draw) in one build call, `FAST_TRACE \| ALLOW_UPDATE` | 2.74 ms | 0.55 ms | 185 MB |
| TLAS over the 835 BLAS | 0.15 ms | | 0.14 MB |

So a naive "capture everything, rebuild everything" frame costs about 0.45 + 0.15 + 2.7
+ 0.15 ≈ **3.5 ms**. Refitting instead of rebuilding brings it to about 1.3 ms. Phase 2
can get well below that, because most of the soup is static (next section). The bench runs on its own
device so that it measures the same hardware without touching the game. Enabling
`VK_KHR_acceleration_structure` on the game's device is the first step of phase 2 (see below).

### What works, what does not (yet)

- Works:
  - Every G-buffer draw and shader variant, skinned characters, wind, terrain and sky,
    with exact triangle counts.
  - The world-space solve, coverage and normal agreement.
  - `geo_view.py` (the top-down render of the soup shows the level layout out to the
    far city geometry).
  - Validation-clean (section 4).
- Limits:
  - The analysis takes ~1 s on the present thread in the capture frame.
  - Only the G-buffer pass is captured. Forward/transparent passes, decals and the
    depth-only prepass are not.
  - No UVs or material IDs yet.
  - The capture runs in traced frames only. `geo_xfb_always` is a cost probe and writes
    to a scratch buffer nobody reads.

## 4. Validation

`auto-run.ps1 -Validation -Set geo_capture=1,geo_xfb_always=1` (Khronos validation
below our layer), capture `20261026-041511`.

- The set of message IDs is identical to the pre-existing baseline
  (`captures/validation_gi1_fixed.log` from the Track C pass). Those messages are all
  the game's own: its shader modules, `StorageImage-FormatMismatch`, layout 09600 and so on.
- No message mentions transform feedback. The capture result under validation is identical.
- One error came from this work and is fixed: `VUID-vkCmdBindPipeline-None-02323`
  (pipeline bound while XFB is active), which led to the per-draw design.
- Rules kept:
  - No game resource is written. XFB goes to our buffers, and the depth/RT0 copies
    transition back to the original layout.
  - The UBO copy dispatch restores the game's compute bindings.
  - Non-finite clip positions (the game's NaN-parked vertices) are filtered before
    anything uses them.

## 5. Phase 2 plan: BLAS/TLAS and ray-query shadows/GI

1. **Device.** Bump the instance API version to 1.2 (the game asks for 1.1) or use
   `VK_KHR_buffer_device_address`, then add `VK_KHR_acceleration_structure`,
   `VK_KHR_deferred_host_operations` and `VK_KHR_ray_query` with their features at
   `vkCreateDevice`, using the same copied-create-info scheme. Caveat: the game
   enables `VK_EXT_buffer_device_address`. The KHR and EXT extensions must not be enabled
   together, so the core 1.2 `bufferDeviceAddress` feature is the way. This needs a
   validation run of its own.
2. **Per-frame capture into device memory.**
   - XFB into a device-local ring (double-buffered).
   - Ranges are already known at record time: each draw's offset is the CPU prefix sum
     of `indexCount/3 × instanceCount`. All BG3 G-buffer draws are direct, and the
     queries confirmed the counts exactly. So the BLAS build ranges can be recorded in
     the same command buffer with no readback. The stream queries become a debug option.
   - Then a compute pass: clip → camera-relative world (one 3×3 solve per vertex, the
     matrix comes from the UBO copy), NaN vertices → degenerate.
3. **BLAS policy, keyed by draw identity** (pipeline + index buffer/offset + index
   count + instance stream; all available in the draw hook):
   - Class A/C (static): build once in *absolute* world (`W + _m6`), so camera motion
     does not invalidate it. Reuse while the identity persists; the TLAS instance is
     identity minus the camera offset.
   - Class B (skinned) and D (wind): refit each frame (≈0.55 ms for the whole frame is the
     upper bound; these are a fraction of the 3.16M triangles).
   - Terrain: build on change only.
   - Compaction for static BLAS (182 → ~100 MB expected).
4. **TLAS** every frame (0.15 ms for 835 instances), in camera-relative space for precision.
5. **Alpha-tested geometry** (the 2.2%): capture UV as a second XFB output, keep the
   material's alpha texture index per draw (from the FS descriptor set), and mark those
   geometries non-opaque. Ray queries then run `CandidateTriangle` alpha tests in the
   shader. Foliage-heavy scenes need this most.
6. **First consumers** (core, hot-reloadable; inject where Track C does):
   - Ray-query sun shadows: one ray per pixel from G-buffer position and normal, folded into the
     screen-space sun shadow mask like the current contact shadows.
   - Ray-query AO / one-bounce GI replacing Track C's screen-space tracing. Hits are shaded
     from the screen when on-screen, otherwise from a world-space radiance cache.
   - Denoise with RR (Track A).
7. **Coverage of other passes**: shadow-cascade casters outside the view frustum. The
   G-buffer only holds what the camera sees, plus what BG3 failed to cull (the far city
   here). Off-screen occluders for sun shadows would need XFB on the cascade passes too
   (same mechanism, depth-only VS are patched already).
