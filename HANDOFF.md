# HANDOFF: opacity micromaps for alpha-tested foliage (`rt_omm`), PAUSED

Branch `worktree-agent-a8b7cb486c1434ebd`, based on main `4990df7`. Paused by the user during
the design and read-through phase. **No code has been written yet. No queue jobs were
enqueued.** (The previous HANDOFF.md came from main, about the R0 sun replace. It is still in
git history.)

## Checked
- The foliage-alpha fix from the paused branch `agent-ac91da91fddcd3510` is already in main:
  merge `51a2db9` (7a6a90a is an ancestor of HEAD). The grass families and cut-offs in
  `alpha_fs.txt`, the resident-mip read and the stochastic alpha can be relied on.
- The OMM SDK in `external/omm-sdk-1.10.0` has `omm.h`/`omm.hpp`, `lib/omm-lib.lib` and
  `bin/omm-lib.dll`. The CPU baker and the GPU baker API are there, but no source.
- Vulkan SDK 1.4.357 headers have `VK_EXT_opacity_micromap`, including the special indices -1..-4,
  `VK_GEOMETRY_INSTANCE_DISABLE_OPACITY_MICROMAPS_BIT_EXT` (0x20) and
  `ALLOW_OPACITY_MICROMAP_DATA_UPDATE`.
- The game's device: the instance is bumped to 1.3, and `VK_KHR_synchronization2` is enabled with
  its feature. So the extension's dependencies are met.
- The code lives in `layer/`, not `core/`:
  - BLAS: `layer/bvhsplit.cpp` `bvh_build`, with the mesh builds around line 1194 and the
    per-draw `DynSlot` builds around line 1208.
  - TLAS instance flags: around lines 1030 and 1055.
  - Records: `layer/alphatest.cpp` `alpha_add_draw` and `alpha_commit`.
  - Order in `rtscene.cpp` `rt_after_capture`: `bvh_build`, then `alpha_commit`, then `commit_scene_slot`.
  - Shader alpha test: `shaders/rt/rt_common.hlsli` (`alpha_eval`, `vt_alpha`, `tex_alpha`).
  - Stats counters: `core/gi.cpp`. 134 and 135 are free; `kStatsCounters` is 136.

## Design decided (not implemented)
Bake on the GPU with our own compute shader, not the SDK GPU baker. The reason is control over
conservativeness. The SDK CPU baker is used only in a unit test, to check the micro-triangle
indexing (bird curve) and its encoding.

1. **Device** (`rtscene.cpp` `rt_device_create_info`): when `rt_omm=1` (restart) and supported,
   add `VK_EXT_opacity_micromap` and `VkPhysicalDeviceOpacityMicromapFeaturesEXT::micromap`. Load
   `vkCreateMicromapEXT`, `vkDestroyMicromapEXT`, `vkCmdBuildMicromapsEXT` and
   `vkGetMicromapBuildSizesEXT` through `dd->gdpa` in a new `layer/omm.cpp`. Leave the shared
   `VkDeviceFuncs` alone.
2. **Bake** (`shaders/rt/omm_bake.hlsl`, set 1 = the alpha set):
   - Runs right after `alpha_commit` in the same command buffer. The bake is fixed at uniform
     level L (`rt_omm_level`, 4). There is one micromap triangle per triangle of one instance.
   - The index buffer covers all prims. Entry `t` is the special index of triangle `t % tpi`
     when that triangle is uniform, else `t % tpi`.
   - `vkCmdBuildMicromapsEXT` is recorded in the same frame. Sizes are known on the CPU, so no
     readback is needed.
   - The OMM is used by a BLAS only once its bake record is submitted (`bvh_rec_register` and
     `rec_submitted`). A barrier ALL_COMMANDS → AS build stage follows the build.
3. **Classification, exact for the shader's deterministic reads.** A micro-triangle is opaque if
   every texel in the bilinear support of its UV bounding box, at **mips 0 and 1**, is ≥
   cut + ε. It is transparent if every such texel is < cut − ε. Anything else is unknown, and so
   is any VT page not resident at mip 0 or 1 at bake time. ε covers float error in the filter;
   take 2^-12, or 1/512 for the VT center-sample path.
   - VT: the bake samples the texel centers through the same read code at a forced mip.
     Refactor `vt_alpha` into mip selection plus `vt_read(a, u, mip)`. Report `coarser` → unknown.
   - tex: `Load` at mips 0 and 1, with wrap.
   - Change `alpha_hash` to return (h + 0.5)/2^24, so the threshold lies in (0,1). Alpha 0 is
     then never solid and alpha 1 always solid at stochastic levels. The effect on `rt_omm=0` is
     negligible (2^-25 shift).
4. **Tolerance, derived.**
   - Known states are bit-exact for every ray that reads at blur level ≤ 1 (hard cut-off).
   - At level > 1 the shader is itself a Bernoulli coverage estimate (hashed threshold). The OMM
     answer equals the fine-mask coverage inside the micro-triangle. So the gate there is
     statistical: the mean visibility must match within the frame-to-frame noise of `rt_omm_use=0`.
5. **Static meshes:**
   - Bake only `kReady` meshes, with UVs from the pool.
   - When the OMM is ready, rebuild the mesh BLAS once: a new AS range with the OMM chain, and
     the same chain in `blas_sizes`. Bump `m.gen` so pending compaction swaps of the old BLAS are
     dropped, and re-query compaction.
   - The OMM key is (mesh key, record identity, L). Record identity = a hash of kind, channel,
     effective cut, scale, p0/p1, t[32], and the tracker `ImageRec.id` of the tex and cache
     views; add `alpha_record_identity(rec)` in `alphatest.cpp`.
   - An instance whose draw's identity differs from the mesh's OMM identity gets
     `DISABLE_OPACITY_MICROMAPS`.
6. **Per-draw (wind foliage, `DynSlot`):**
   - Key the OMM on (dyn key without the occurrence counter, record identity, tpi, prims, L).
     tpi = `args.count/3` for direct draws.
   - Attach on a full build. Refits keep the same micromap.
   - Each frame, a compute pass compares the soup UVs of every prim with the baked instance-0
     UVs (stored at bake). On a mismatch it ORs `DISABLE_OPACITY_MICROMAPS` into that instance's
     flags byte in the host ring (byte 55) before the TLAS build.
7. **Live A/B:** `rt_omm_use=0` sets `DISABLE_OPACITY_MICROMAPS` on every instance. No BLAS
   rebuild, and the TLAS rebuilds because the flags are part of its signature.
8. **Checks:**
   - `rt_omm_check=1` is passed in alpha record header uint 3, which is currently 0.
   - Bake verifier: random points per known micro-triangle. It reads the state through the
     forward `bary2index` (independent of the bake's `index2bary`) and the runtime read at
     mips 0 and 1. The violation count must be 0.
   - In-game: closest/visibility queries check hits committed by traversal on alpha instances
     (not by our candidate code) against the fine alpha. This uses new counters 134+ in `gi.cpp`.
   - The existing "alpha candidates of … rays" stats give the gain.
9. **Lifetime:** reference-count each OMM from Mesh and DynSlot. Free it through a pending list
   with the BLAS rule (`now >= frame+6`, `frame+2 <= life_gpu_done`, `frame < rt_scene_oldest_live`).
10. **Settings:** `rt_omm` (0, restart), `rt_omm_level` (4), `rt_omm_use` (1, live),
    `rt_omm_check` (0), `rt_omm_bakes_per_frame` (16), `rt_omm_budget_mb` (64). Register them in
    `common/settings.h` and `bg3rtx.ini`. The 2-state ray flag is **out of scope** for now
    (correctness first); it is optional later.
11. **Unit test** `tests/omm_test.cpp`, linked to `omm-lib.lib`, with the dll copied to `bin/`:
    - `index2bary`/`bary2index` round trip for levels 0-6;
    - agreement with the SDK CPU baker's states on a synthetic edge texture.

## Left to do
Everything in the design above. Then:
- the build, `scripts/build.ps1 -Test`, and the DXC compile of all shaders (as in `merge-green.ps1`);
- commit;
- queue session jobs with `patterns`, `"hold": 90` or more, on Campsite S1/S3 and Grymforge:
  - `rt_omm=1` with `rt_omm_use` 0 → 1 for build ms, memory and trace ms;
  - `rt_omm_check=1`;
  - `rt_stats=1`.

## Open risks
- Whether ray-query compute pipelines need any flag for OMM traversal. The spec flag is only for
  ray tracing pipelines; verify with `-Validation`.
- The VK spec rule on BLAS update and the OMM chain. Keep the same micromap across refits.
- Usage counts in the BLAS attachment are an over-estimate (prims). Confirm this is valid with
  validation.
