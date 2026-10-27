# HANDOFF: split-BVH device lost (rt_bvh_split=1)

Branch: `worktree-agent-a9ea45fdd00455d7d`, on top of `7178631` (GPU-fenced retirement).
Built with `scripts/build.ps1` (layer, core, tests: 120 checks, 0 failed). All the RT
shaders that include `rt_common.hlsli` compile offline with the SDK's DXC
(`cs_6_6`, the runtime flags). **Nothing was run in game.**

## 1. Root cause analysis (from the code)

Every way an RT pass could read set-1 data that does not belong to the TLAS it traces,
plus the GPU-memory hazards found on the way. Items A to C are the likely faults.
D to F made them reachable or are smaller holes.

**A. Compaction sized from another mesh's query. This corrupts BLAS memory.** (`bvhsplit.cpp`)
- `JobBatch::compact` stored only the mesh *key*. A mesh can be evicted and created again
  under the same key while its batch is still pending:
  - `bvh_forget_buffer`: the game destroys a buffer, and the new buffer gets the same
    handle value;
  - `bvh_buffer_written`: a transfer writes into the mesh's range.
- `bvh_on_present` then copied the *old* BLAS's compacted size into the *new* mesh
  (`it->second.compact_size = size`, with only `!compacted` checked).
- The next build ran `as_create(m.compact_size)` and then
  `CmdCopyAccelerationStructureKHR(COMPACT)` into a range of that size. The compacted size
  depends on the BVH built over the new vertices, not only on the triangle count. If the
  new size is larger, the copy writes past its range into the neighbouring BLAS of the
  same 64 MB chunk. Traversal of that BLAS then fetches garbage nodes and child pointers.
  The result is an MMU fault or a hang, i.e. device lost / event 153.
- This matches the evidence:
  - split only;
  - a few seconds into gameplay, while many new meshes enter the cache (streaming
    recreates buffers);
  - independent of RR;
  - not fixed by GPU-fenced range retirement.
- Stale extraction/check/audit results were applied to the new mesh the same way (state
  `kReady` without its own check).

**B. TLAS and set 1 came from different builds.** (`rtscene.cpp`, `alphatest.cpp`, `core/gi.cpp`)
- The core took `rt_scene()` (one split TLAS object, rebuilt or refit *in place* every
  frame) and `rt_alpha()` separately. `rt_alpha()` returned the set of the *latest commit
  at record time*.
- The game records the next frame's G-buffer (which holds our build) on another thread,
  interleaved with the lighting. So a pass could bind the set of build N+1 and trace the
  TLAS contents of build N, or the reverse.
- InstInfo, records, soup and pool offsets then do not match the instances. This is the
  0.6-2% `prim >= ii.tris` the earlier agent saw.
- Most reads were bounds-checked, but not all:
  - `alpha_kind_hit` read `gAlphaRec[ii.rec + 8]` unchecked;
  - `find_rec` trusted the record count in the header;
  - the range checks `base + (prim*3+3)*stride > bytes` can wrap in 32 bits;
  - an instance past the InstInfo fell back to the *legacy* lookup.
- The device does not necessarily have robust buffer access (now logged, see 3). So an
  out-of-range ByteAddressBuffer read is undefined and can fault.
- In-place rebuilds also meant that a build could rewrite the very TLAS an earlier frame's
  pass was tracing, if the submission order ever differed from the assumed order.

**C. BLAS ranges and AS handles were released while an older TLAS could still be traced.**
- Pending frees waited for `gpu_done >= frame + 2` of the free.
- But a TLAS built in frame F is handed out up to frame F+1 (`built + 1 >= now`), and its
  pass may be submitted a frame after recording.
- A mesh evicted in frame F, after that frame's build had written its instance, could
  therefore get its chunk range (and `vkDestroyAccelerationStructureKHR`) released while a
  TLAS that references it was still in flight.

**D. Host ring regions and alpha sets were retired by frame count, not by GPU completion.**
- `bvhsplit` used `g_ring_next++ % 8` for the region of instances, InstInfo and matrices,
  written by the CPU at record and at submit.
- `alphatest` used `g_slot++ % 8` for the set and record region. `vkUpdateDescriptorSets`
  on a set that a pending command buffer still binds is undefined; on NVIDIA the GPU then
  reads the new descriptors.
- Safe only while fewer than 8 frames are in flight. Headless or loading can run more
  presents ahead of the GPU.

**E. Duplicate per-draw BLAS key in one frame.**
- The 64-bit `dyn_key` hash of two draws could collide. Two builds would then target one
  destination in one `vkCmdBuildAccelerationStructuresKHR`: a GPU race on the BLAS.
- Rare, but cheap to exclude.

**F. Not changed (noted):**
- The soup (the XFB buffer) is one buffer, rewritten by every frame's capture. With the
  pairing above and the queue order (G-buffer N, lighting N, G-buffer N+1) a pass reads
  the soup of its own build. A pass that executed after the next capture would read moved
  vertices, which is bounded and therefore wrong values but no fault.
- The mesh pool only grows (by copy into a new buffer, old one deferred), and its live
  ranges are never rewritten.

**G. Builds recorded into a command buffer the game dropped** (lead from the emissive
agent; not covered by the first commit).
- Around level loads the game records a command buffer and then frees it or begins it
  again without submitting it. Our build work in it never runs, but the host state
  advanced as if it had.
- **Compaction:** `m.as = c` at record. If the copy is dropped, every later TLAS
  references an uninitialised BLAS, which faults. This is a direct path to the
  level-load device lost.
- **Per-draw BLAS:** a fresh or rebuilt slot whose build was dropped is *refit*
  (UPDATE from an unbuilt source) the next frame.
- **TLAS:** the refit source is the previous slot's TLAS, which may never have been
  built; the slot is also handed out and traced.
- **Legacy BLAS:** refit after a dropped build.
- **New meshes:** the extraction batch of a dropped buffer timed out after 240 frames as
  "lost" and the mesh went to `kNeedCheck`. Its BLAS was never built, and a check job
  that happened to match would have made it `kReady`.
- **Fixups:** fixups of a dropped buffer stayed keyed by its handle. If the handle was
  reused they were applied into a ring slot of a later build.

## 2. What changed

**Build records** (second commit; `bvhsplit.cpp`, `cmd.cpp`, `rtscene.cpp`, `alphatest.cpp`, shaders):
- Every scene build registers a record for the command buffer it is recorded into
  (`bvh_rec_register`). `bvh_on_submit` marks it submitted.
- `vkBeginCommandBuffer`, `vkFreeCommandBuffers` and `vkAllocateCommandBuffers` (a
  handle reused) call `bvh_cmd_reset`. That marks a still-pending record *dropped*
  (logged: `[bvh] ... dropped without a submit`) and discards the buffer's fixups.
- Refit only from a source whose build was submitted: per-draw BLAS, split TLAS (the
  previous slot), legacy BLAS. Otherwise a full build, which does not read the old
  contents.
- **Compaction is deferred.** The mesh switches to the compacted BLAS only when its
  batch's done word shows the copy ran (every batch now has a done word). A dropped or
  timed-out batch discards the copy, and the mesh keeps its original.
- A batch of a dropped buffer is handled as lost at once (no 240-frame wait). A lost
  *extraction* (batch never ran) evicts the mesh, to be extracted again, instead of
  `kNeedCheck`. Meshes still become `kReady` only from a done batch, i.e. GPU-executed.
- A scene slot whose build record is dropped is not handed out.
- **GPU guard** (set 1 binding 9, `gSceneStamp`):
  - per slot, the host writes the generation it expects at commit;
  - the build's command buffer writes the same value with `vkCmdUpdateBuffer` when it
    runs;
  - `rt_occluded`, `rt_closest`, `rt_closest_hit` and `rt_closest_debug` trace only if
    the two match, otherwise every ray misses.

  So a TLAS whose build was dropped, or submitted after the pass, is never traced. Not
  guarded: the generated debug reference `shaders/gi/ls_ref/ls_ref_body.hlsli`
  (`ls_debug` only).
- Not covered: a pool growth whose copy was dropped loses the pool's earlier vertex data
  (UVs and normals of cached meshes read as zeros: wrong alpha and normals, no fault,
  since the reads are bounded).

**Active fault detection** (third commit; `devfault.cpp`, `tracker.cpp`, `bvhsplit.cpp`):
- After the loss the game stops calling Vulkan, so the hooks never saw
  `VK_ERROR_DEVICE_LOST`. The `00ab836` test ran 5 of 5 stalled, with no fault block.
- A layer thread now polls our latest present fence every `fault_poll_ms` (default 1000;
  0 turns it off).
- On `VK_ERROR_DEVICE_LOST` it logs the full report:
  - the scene slots, with their build records (pending, submitted or DROPPED);
  - `bvh_log_state`: meshes, pending batches and compaction copies not yet swapped in,
    ranges awaiting release, pending and dropped build records, and the last 20 `[bvh]`
    events (each build's slot, record, command buffer, new meshes, compaction copies,
    full builds, TLAS build or refit, pool growth; batches done, dropped or timed out;
    dropped buffers);
  - `vkGetDeviceFaultInfoEXT` with the addresses mapped to our buffers.
- If presents stop for 5 s without a loss, it logs `[fault] no present for N s` with the
  same state once per stall, and queries the fault info (one line if nothing is
  recorded).

**Fault `d33fca6` run 1** (`captures/fault_d33fca6_run1.log`) and the fourth commit:
- **What the log shows:**
  - The loss is about 15 frames after the first RT frame. The GPU completed frame 765;
    the poll found the loss at 768.
  - All slots were submitted; there were no dropped records and no pending compactions.
  - The read fault at 0xff93000000 (4 KB page) and the IP fault are in none of our 19
    registered ranges. These were all our BVH buffers: 5 BLAS chunks, the pool, the ring,
    status, scratch, 8 TLAS slots, the alpha records and the soup.
- **What the code says:**
  - Every BLAS address we write into an instance comes from
    `vkGetAccelerationStructureDeviceAddressKHR` on an AS inside a chunk.
  - Chunks are never freed while the split runs, so a stale BLAS reference would still
    point inside a chunk.
  - So the faulting page is not a BLAS reference of ours. The IP fault says a shader
    did the read.
  - Not registered: the alpha dummy/legacy buffers (256 B), our images (core GI,
    rrpost, NGX: image VAs are not queryable), and all of the game's memory.
- **New: `VK_EXT_device_address_binding_report`** (instance `VK_EXT_debug_utils` plus a
  messenger; `dev_fault_bindings=0` turns it off).
  - Every bind and unbind of the driver, the game's included, is recorded: object type,
    handle, range, frame, and the last 16k unbinds.
  - The fault block now names the object for each fault address ("driver bindings: image
    0x... [base, +size) ... LIVE / UNBOUND in frame N").
- Every range of ours is logged at registration and listed in the fault block.
- **Record-time guard:** each TLAS instance's BLAS reference must lie in a chunk; a
  mismatch is logged and kept as an event.
- **Fault-time audit:** for every slot's ring region, each instance's BLAS reference is
  counted as held, freed-but-unreleased, released, or outside our chunks, with the first
  offenders.

**First commit:**

**Scene snapshot slots** (`layer/host.h` `kSceneSlots = 8`, `layer/rtscene.cpp`)
- Each build writes one slot. The slot holds:
  - its own split TLAS;
  - the bvhsplit ring region with its instances, InstInfo and matrices;
  - its alpha set and record region (`alpha_commit(cmd, ab, slot)` returns the set).
- `pick_scene_slot()` takes only slots whose every hand-out the GPU has finished:
  `life_gpu_done() >= last_use + 2`, with a 240-frame fallback as in `defer_tick`.
- The current slot is never reused. A stale slot (`built + 1 < now`) is never handed out
  again. If no slot is free, the build is skipped and the previous scene stays current.
  This is logged as `[rt] ... every scene slot is still in flight ... build skipped`.
- `rt_scene_set()` (**ABI 8**, `common/host_api.h`) returns the TLAS and set of one slot
  atomically and stamps `last_use`.
- `core/gi.cpp rt_tlas` uses it. `rt_scene` and `rt_alpha` return the same slot's data.

**bvhsplit.cpp**
- **Per-slot TLAS.** The refit is now an UPDATE with src = the previous slot's TLAS and
  dst = this slot's (only when both have the same storage size). All slots are grown to
  the newest size, so the refit keeps working after a growth.
- **Ring region = slot index.** No more `g_ring_next`.
- **Mesh generations.**
  - Each created `Mesh` gets a unique `gen`. `Job` and `CompactRead` carry it.
  - A result applies only to the same generation. This fixes A.
- **Releases wait for the oldest live slot.**
  - `PendingFree` also carries the BLAS handle; its destroy is deferred to the range's
    release.
  - A freed range is released only once every scene slot that may still be read was built
    *after* the free (`rt_scene_oldest_live()`), in addition to the existing fence rule.
    This fixes C.
- **Duplicate keys.** A `dyn_key` seen twice in a frame is skipped. This fixes E.

**Shaders** (`shaders/rt/rt_common.hlsli`)
- `rec_in_range` and `tri_in_range` are overflow-safe and based on `GetDimensions`.
- They are used in `alpha_pass_hit`, `alpha_kind_hit` and `rt_hit_normal`.
- `find_rec` clamps the record count to the buffer.
- An instance past the InstInfo yields an empty InstInfo, which means an "unknown" alpha
  and no normal, instead of the legacy lookup.

**Device-fault diagnostics** (new `layer/devfault.cpp`, `layer/layer.cpp`, `layer/tracker.cpp`)
- With `rt=1`, `VK_EXT_device_fault` (`deviceFault`) is added to device creation
  additively, after the RT patch. If creation fails with it, it is retried without.
  `dev_fault=0` turns this off.
- Logged at device creation: `[fault] the game's device: robustBufferAccess ON/off`
  (answers the question; we do not change the game's choice).
- Buffers register their device-address ranges, and frees are remembered (last 1024):
  - soup;
  - mesh pool;
  - BLAS chunks;
  - split TLAS slots;
  - instance/InstInfo ring;
  - alpha records (now created with a device address);
  - scratch, status, and the legacy BLAS/TLAS.
- On the first `VK_ERROR_DEVICE_LOST` from `vkQueueSubmit`, `vkQueueSubmit2`,
  `vkQueuePresentKHR` or our present-fence poll, the layer logs:
  - the scene slots;
  - `vkGetDeviceFaultInfoEXT`: description, each address with its type and precision,
    the registered ranges it falls in (live, or FREED in frame X), and the bvh detail
    (`bvh_describe_address`). The bvh detail is "BLAS of mesh K (gen, compacted)", a
    per-draw BLAS, a freed-but-unreleased range, "no BLAS allocated there", pool mesh,
    ring slot and part, or TLAS slot;
  - the vendor records.

## 3. Tests for you to run (in game)

Build first: `scripts\build.ps1` (ABI 8: the layer and the core must both be new;
restart the game). The second commit changes the set-1 layout (binding 9), so
`shaders/rt/rt_common.hlsli` from that commit needs the new layer. A new shader with an
old layer fails pipeline creation, and the old pipeline is kept.

In the logs, look for `[bvh] ... dropped without a submit` around level loads (how often
it happened) and `[bvh] batch ... compaction copies discarded`.

**A. Fault rate, the default split** (6 runs, Campsite). Run this 6 times:

```
scripts\auto-run.ps1 -NoBuild -Background -NoAnalyze -Save "Campsite - 27h" -Traces 2 -TimeoutSec 300 -Set "fg=0,auto_interval=900"
```

- **Pass:** 0 stalls (no `stalled` result, nothing new in `captures\stalls.log`), and
  0 nvlddmkm System event 153 during the runs. Ignore the pair the notes say comes with
  each launch; compare timestamps against the run windows.
- Check in each run's `bg3rtx.log`:
  - `[fault] + VK_EXT_device_fault` and `[fault] device fault reporting ready`;
  - `[fault] the game's device: robustBufferAccess ...` (report the value);
  - no `[rt] ... build skipped` (if it appears, the GPU fell 8 slots behind: report the
    count);
  - `[bvh] TLAS storage of slot N` lines: 8 slots, then nothing more after the first
    growth;
  - `[bvh] last N builds ... refit` still shows TLAS refits (static views).
- If a device loss still happens, send the `[fault]` block. It names the buffer, and for
  the BLAS chunks the mesh/BLAS and whether the range was freed.

**B. Validation** (1 run):

```
scripts\auto-run.ps1 -NoBuild -Background -NoAnalyze -Validation -Save "Campsite - 27h" -Traces 1 -TimeoutSec 480 -Set "fg=0,auto_interval=900"
```

- **Pass:** compared to the baseline IDs in `docs/TRACK_D_RT.md` 11.4, no new IDs.
- Watch especially:
  - `vkCmdBuildAccelerationStructuresKHR` (the TLAS UPDATE with src != dst),
  - `vkUpdateDescriptorSets` / `-None-03047` (set updated while in use),
  - `vkCmdCopyAccelerationStructureKHR`,
  - anything on `VK_EXT_device_fault` at device creation.

**C. Optional checks:**
- One run with `-Set "fg=0,auto_interval=900,rt_stats=1"`. Alpha counter 3
  (`prim >= tris`) should now be ~0 after scene changes (it was 0.6-2%).
- One with `rt_bvh_split=0` to confirm the legacy path is unchanged (it uses the slots
  for its set only).

## 4. Hypothesis check: stale alpha textures of cached meshes (fifth commit)

- **Does not apply as stated.** Cached draws do *not* keep their first record.
  - `geocapture.cpp` calls `alpha_add_draw` for every cached draw every frame
    (`v != BVH_CAPTURE` branch). So the record, texture slot, tile cache and page-table
    constants come from this frame's bindings, through `query_binding`.
  - `query_binding` notes each view and its image on the recording command buffer.
  - In `fault_d33fca6_run1.log` the life system logged no waited and no "no fence yet"
    destroy. So no noted game view or image was destroyed while our work was in flight
    (as far as the fences tell).
- **Gap closed anyway.**
  - The views were noted only on the command buffer that records the build.
  - The RT passes bind the set from other command buffers, possibly a frame later
    (`built + 1 == now`), and those never noted them.
  - Each set now keeps its game views and images. Each hand-out (`rt_scene_set`/
    `rt_scene`) extends their life to that frame (`life_extend`), so a game destroy
    waits for the present fence after the last pass that can bind the set.
  - A set held in a retired slot is never bound again, so it needs no life.
- **Remaining limit (pre-existing):** a destroy in the same frame as the submission,
  before any present fence covers it, cannot be waited on. `tracker.cpp` logs it as "no
  fence covers it yet".

## 5. Fault `3f5fb8b` run 1 (`captures/fault_3f5fb8b_run1.log`) and the sixth commit

- **Stale slots 4-7** (built 1399-1402, before the level load) referencing RELEASED
  ranges are harmless. A slot is handed out only while `built + 1 >= now`
  (`rtscene.cpp hand_out`). It is never the refit source either: that is always the
  previous build's slot, and an instance-count change forces a full TLAS build.
- **"Freed but not released" in slots 0-2 is the gate working.**
  - Those are per-draw BLAS reallocated by the builds of 11155-11157 (88, 58 and 51
    full builds, mostly growth after the load).
  - Their old ranges stay allocated until every live slot was built after the free.
- **Evictions after the load:** 0 meshes left. All 105 were evicted by the game's buffer
  destroys at the level load, plus per-draw LRU over the 10,000 frames with no build.
- **The IP cannot be mapped to a pipeline.**
  - `VK_EXT_device_fault` gave no vendor records.
  - The shader heap is not in the binding report.
  - Pipeline executable properties carry no addresses.
- **The read address** `0x1dcfb3cb40000` is a valid-looking VA (`0x1dcfb3cb400`, in the
  driver's heap range) shifted left by 8 bits. That is consistent with a pointer stored
  >> 8 in a driver-internal AS node, interpreted from a stale or other layout.
- **The one hazard the code still had: per-draw BLAS were fully rebuilt *in place*.**
  - This happened when the primitive count changed within capacity, on the staggered
    forced rebuild, or on the refit refused after a dropped buffer.
  - A rebuild changes the BLAS's internal layout at the same address, so the TLAS
    signature did not change. The next TLAS was then *refit* (an UPDATE over a rebuilt
    BLAS), and older slots still in flight referenced the rebuilt BLAS.
  - Both runs fit this:
    - d33fca6: 9 full per-draw builds at 765, then TLAS refits at 766 and 767, then the
      loss;
    - 3f5fb8b: a burst of 88/58/51 full per-draw builds just before the loss.
- **Fix:** a full per-draw build now always goes into a *new* range.
  - The BLAS address changes, so the signature changes and the TLAS is rebuilt.
  - The old range is released through the slot gate.
  - Refits (UPDATE: same layout) stay in place.
  - `rt_dyn_inplace=1` restores the old behaviour for an A/B.

## 6. Bisection on e4368ab (compaction off: 0 faults) and the seventh commit

Your five checks against the code, all clean:
1. **Scratch:** one `vkCmdBuildAccelerationStructuresKHR` for all BLAS. Each build has
   its own range: build or update size per mode, mesh sizes from `buildScratchSize`,
   offsets aligned to `minAccelerationStructureScratchOffsetAlignment` (from
   `rt_scratch_alignment`). The TLAS reuses scratch from 0 only after an AS_WRITE to
   AS_READ|WRITE barrier, and every build starts with a barrier on the previous
   frame's AS, compute and XFB work.
2. **Barriers:**
   - BLAS build, then the compacted-size query (an AS barrier);
   - copies, then an AS_WRITE to AS_READ barrier, then builds and TLAS;
   - the copy source was built in an earlier, finished batch (its size comes from that
     batch's done word).
3. **Copy destination:** sized from this generation's query, rounded to 256; the swap
   waits for the copy's batch.
4. **Soup:** `rt_before_capture` makes the next capture wait for AS and compute reads.
5. **Flags:** a refit needs the same alpha (opaque vs no-duplicate) and the same build
   flags.

**What was wrong: the mesh BLAS was built from pool memory the extraction may never have
written.**
- `mesh_extract.hlsl` *returned early* on an invalid or singular instance matrix, and
  wrote non-finite or huge positions as they came.
- The mesh's BLAS is built from that pool range in the same command buffer, before the
  host sees the result. Its input was then uninitialised device memory or Inf/NaN.
  Vulkan leaves such a build undefined (NaN only in x marks an inactive triangle).
- Rejected meshes were evicted later, but:
  - a batch that ran without the job's marker went to `kNeedCheck` with that BLAS;
  - **compaction took any mesh whose size had come back**, verified or not. A compaction
    copy walks the BLAS's nodes, so a corrupt BLAS sends it to wild addresses (an IP
    fault in a driver shader, a computed read address).
- **Fixes:**
  - The extraction now writes every vertex of the range in extract mode, finite and
    bounded (|p| < 1e7, else 0 and counted invalid), including when the matrix is
    invalid.
  - Only `kReady` meshes are compacted.
  - A batch that ran without the job's marker evicts the mesh instead of keeping its
    BLAS.

**For the emissive capture:** it doesn't call these helpers. The shared lesson is to
build only from sanitised vertices: `rt_convert` / `shaders/rt/xfb_to_world.hlsl` already
guarantee finite, bounded positions (collapsed triangles at a point). A builder that reads
raw XFB output, or any buffer region not fully written that frame, gets the same
undefined builds.

## 7. f9fac2a: per-draw builds alone fault. The eighth commit (likely root cause)

**Root cause: a missing barrier between the soup conversion and the per-draw BLAS builds**
(`bvhsplit.cpp`, after `rt_convert`).
- `xfb_to_world.hlsl` rewrites the XFB soup *in place*, from clip space to
  camera-relative world. The split's barrier after it had dst stage COMPUTE only.
- The only compute-to-AS-build barrier came after the extraction jobs, inside
  `if (!ext.empty())`.
- So in a frame with no new mesh, check or audit (the f9fac2a faulting frames: 0 new
  meshes, 503 per-draw builds), the per-draw BLAS builds and refits raced the
  conversion. They could read clip-space vertices, including those the game parks at
  NaN. That makes the build undefined: a corrupt BLAS, then traversal at computed
  addresses (IP fault plus "read invalid").
- The legacy build has always had `barrier(kCS | kXfer -> kAS)` there
  (`rtscene.cpp:842`), which is why it never faulted from the same soup.
- This also explains the earlier bisection:
  - frames with new meshes or audits had the barrier by accident;
  - more full builds and more refits meant more racing builds;
  - compaction off probably only shifted timing (or removed the separate unverified-copy
    problem fixed in f9fac2a).
- **Fix:** dst = COMPUTE | AS_BUILD.

**Your other checks against the code (no change needed):**
- `maxVertex = prims * 3 - 1` with `firstVertex` and `primitiveOffset` 0, non-indexed
  (`VK_INDEX_TYPE_NONE_KHR`).
- The vertex address is soup base plus a multiple of 48 or 72 bytes (4-byte aligned),
  stride 16 or 24.
- `primitiveCount` is the reserved count, exact for lists and strips. Anything XFB did
  not write is the previous contents, which the conversion makes finite and bounded
  (once it is ordered before the build).
- The soup is not a ring: a BLAS reads its soup range only while it is built.
- A refit uses the same `primitiveCount`, `maxVertex`, stride, flags and opaque flag as
  its source build.

**For the emissive capture:** any compute pass that writes a build's vertex input
(conversion, extraction) needs `VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR`
(access `SHADER_READ` / `MEMORY_READ`) in the dst of its barrier before
`vkCmdBuildAccelerationStructuresKHR`. A COMPUTE-only dst does not order the AS build.

## 8. d19c76d: 1 fault in 8 runs. Barrier audit and the ninth commit

The split's dependencies, against the legacy build (`rtscene.cpp`):

| dependency | split before | now |
|---|---|---|
| XFB capture -> conversion / builds | `rt_before_capture` (AS+CS -> XFB, WAR) before the pass; after it `XFB\|AS\|CS -> CS\|AS\|XFER`, MEMORY_WRITE -> R/W | + HOST stage and HOST_WRITE (explicit; `vkQueueSubmit` already covers host writes) |
| conversion -> per-draw builds | fixed in d19c76d (`CS -> CS\|AS`) | unchanged |
| extraction -> mesh builds / copies | `CS -> AS\|XFER` (only with jobs; mesh builds only exist then) | unchanged |
| copies / BLAS builds -> queries, TLAS, scratch reuse | `AS -> AS`, AS_WRITE -> AS_R/W | `AS\|CS\|XFER -> AS\|XFER`, MEMORY_WRITE -> R/W (as legacy) |
| TLAS build -> RT passes (other command buffers) | `AS -> CS\|FS`, AS_WRITE -> AS_READ | `AS\|CS\|XFER -> CS\|FS`, MEMORY_WRITE -> MEMORY_READ. Also makes the compute-written soup and pool visible to the ray queries' set-1 reads, which the old one did not. |
| host ring (instances, InstInfo at record; matrices at submit) -> TLAS build | implicit at `vkQueueSubmit` (coherent memory, written before the submit) | + the explicit HOST barrier above |
| alpha records / stamp (vkCmdUpdateBuffer) -> RT passes | `TRANSFER -> COMPUTE` in `alpha_commit` | unchanged |

- All our work runs on one queue, so every one of these also orders the previous frame's
  passes before this frame's writes. The log shows one queue.
- None of the old masks was clearly too narrow for an AS read; the residual is not
  explained by a missing dependency I can see.
- **Two A/B switches** to localise it:
  - `rt_dyn_refit=0`: never update a per-draw BLAS. Each one gets a full build into a
    new range every frame, so no BLAS is ever rewritten while anything may read it.
  - `rt_build_serial=1`: one build command per BLAS, with a barrier between them,
    instead of one batched call of about 500 builds. This tests the driver's batched
    build.
- Suggested runs: 8 default, then 8 each with `rt_dyn_refit=0` and with
  `rt_build_serial=1`, all with the same `-Set` as before.

## 9. c9297e3 bisection and Nsight Aftermath (tenth and eleventh commits)

**Evidence (all 9 fault logs of c9297e3):**
- The faulting page lies 16-70 KB *below* one of our `VkAccelerationStructureKHR` handle
  values. These are host-heap pointers of the driver's AS objects, not GPU
  allocations: no driver binding covers them.
- Every in-flight slot references only held or freed-not-released BLAS. Released ranges
  appear only in retired slots.
- Every configuration that faults destroys AS objects at run time: compaction,
  new-range builds, `rt_dyn_refit=0`, and the emissive capture. The legacy build, which
  never faults, almost never does.

**`f7867a9`:**
- `rt_as_keep=1` (default): a released BLAS's handle is kept until device destruction.
- `VK_NV_device_diagnostic_checkpoints` markers around every core callback, at every
  game command-buffer begin and at the BVH build stages, read back after a loss.
- A per-draw vertex-range check.
- The coordinator's `auto-run.ps1 -VSet` edit.

**Eleventh commit: Nsight Aftermath (`aftermath=1`, default 0).**
- Built when the SDK is at `BG3RTX_AFTERMATH_SDK` (default
  `D:/BG3_Tools/nsight-aftermath-sdk-2026.3`). The DLL is copied into `bin/`, loaded at
  run time and never packaged.
- **Before the driver's `vkCreateDevice`:** `GFSDK_Aftermath_EnableGpuCrashDumps` (Vulkan)
  with callbacks that write `captures/aftermath/<time>-<n>.nv-gpudmp`,
  `<id0>-<id1>.nvdbg` and a description (frame, presents done).
- **Device:** `VK_NV_device_diagnostics_config` with SHADER_DEBUG_INFO |
  RESOURCE_TRACKING | AUTOMATIC_CHECKPOINTS | SHADER_ERROR_REPORTING. A creation failure
  retries without it.
- **Shader map:** each SPIR-V module the device compiles is mapped by its Aftermath hash
  in `captures/aftermath/shader_map.txt`.
  - Ours are named `file:entry`.
  - The game's are named `game <hash>`. Their SPIR-V is in `captures/shaders/`
    (`dump_shaders=1`), or in `captures/aftermath/shaders/` when the capture patched
    them.
- **On a loss:** every thread that sees `VK_ERROR_DEVICE_LOST` (submit, present, the
  fault poll) waits up to 10 s for `GFSDK_Aftermath_GetCrashDumpStatus` to finish.
- **Decoder:** `bin\aftermath_decode.exe <dump> [out.json]` prints:
  - the page fault (VA, type, access, engine, client);
  - every resource Aftermath tracked at that VA, including destroyed ones, with their
    Vulkan handles and ticks;
  - the active shaders with our names.

  It writes the full JSON (ALL_INFO, with shader mapping through the map and the .nvdbg
  files).
- **Source lines:** for a second run, add `shader_debug=1`. Our shaders are then built
  `-Od` with embedded source, which changes timing.
