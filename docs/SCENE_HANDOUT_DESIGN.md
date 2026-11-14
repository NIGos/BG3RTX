# Scene handout: remove the `[rt] scene wait` (design, 2026-11-09)

Status (2026-11-14): increments 1-5 are built and merged, off by default, never qualified. Current state, compatibility
and the qualification batch: section 8. Sections 1-7 are the design as written on 2026-11-09 (branch
`claude/scene-handout-design`); the notes under the table of section 5 record what each increment changed. It answers the four blockers of the
2026-11-08 review (docs/TRACK_D_SCENE_BUILD.md, "Scene wait growth", branch claude/opt-scene-handout).

## 1. The problem in numbers

- What waits: the lighting worker's first RT query of frame N (`rt_scene_set`, layer/rtscene.cpp:1485) blocks on
  `g_scene_cv` (:158) until the G-buffer thread's `rt_after_capture` (:1091) commits frame N's slot
  (`commit_scene_slot`, :221, called at :1230). Both command buffers go down in one vkQueueSubmit, G-buffer first.
- Size: rv2-s8-capture-stride.1.145525.log (Water Queens House, S8): 120/120 got, mean 2.1-3.4 ms static,
  5.0-7.8 ms in motion, 0 timeouts in steady windows. That run is capped near 30 fps (`[pace]` median 33.3 ms),
  so it does not show whether the wait costs frame time.
- Does it cost frame time? Only lighting work recorded after the first RT query is delayed, and the submit also
  needs the G-buffer thread. The one data point: on 2026-11-08 the wait grew 2.0 -> 18.2 ms and the frame
  25.9 -> 52.6 ms with GPU ms flat, so the wait sat on the critical path then. The prepared but never run job
  `tools/live_batches/scene_handout/jobs/sh1-camp-scene-wait-fg2.json` (rt_scene_wait 1/0/1, uncapped, FG,
  cpu_timing=1) measures the ceiling: frame time(wait on) - frame time(wait off).
- What the wait contains: (a) the rest of the game's G-buffer recording after the lighting thread reaches the
  RT passes, plus our per-draw capture hooks on that thread; (b) our capture end -> commit path. No log in
  captures/ has a `[cpu] scene build recording` line any more (logs pruned after the 11-07 format). The last
  numbers (docs/PERF_AUDIT_CPU.md E6/E7, sw2/sw3 2026-11-02): capture end -> commit 1.504 / 0.796 ms, of which
  scene lock wait 0.000, scene_prepare 0.076; BLAS keys 0.466 / 0.100, TLAS instances 0.275 / 0.241,
  convert recording 0.040 / 0.050, AS command recording 0.447 / 0.158 ms. So (b) is 0.8-1.5 ms; (a), the
  game's own recording, is the other 2-6 ms and only a handout can remove it.

## 2. Smaller steps first: how much they can remove

Moving work earlier on the same G-buffer thread saves nothing: the commit time is that thread's total. Only work
taken off the thread, or the commit moved before work that the lighting does not need, shortens the wait.

| Step | Where | Saves (of 3.3 static / 5-7 motion) | Notes |
|---|---|---|---|
| S1 notify before AS command recording: compute instances, `ensure_tlas`, write set 1, notify, then record convert/BLAS/TLAS | bvh_build (layer/bvhsplit.cpp:2260) split in two; rt_after_capture :1192-1230 | 0.2-0.5 ms (AS + convert recording, E6/E7) | Touches bvh_build, which another branch edits now (mesh cache). OMM commit order must hold. |
| S2 scratch reuse in bvh_build (PERF_AUDIT_CPU item 4) | bvhsplit.cpp:1272, :1515 | 0.1-0.3 ms | Already planned as `perf_scene_scratch`. |
| S3 lighter per-draw hooks on the G-buffer thread | cmd.cpp, geocapture.cpp | unknown share of 1.0-1.3 ms/frame hook time (E3, summed across threads) | Needs cpu_timing per thread first. |

Together at most ~1 ms, about 15-30 % of the wait. Worth it only if sh1 shows a small ceiling and the handout is
dropped; otherwise the handout makes S1 unnecessary (do not spend effort on both).

## 3. Design: the stamp is the handout

Idea: the lighting thread binds a slot that is reserved but not yet built. Everything the GPU needs to find the
scene is read at execution time, from data finished before the vkQueueSubmit. Two mechanisms do it:

1. Set 1 (layer/alphatest.cpp:580-640 layout, alpha_commit :1198-1400 writes) becomes update-after-bind (UAB).
   With UAB the G-buffer thread may write set R after the lighting command buffer bound it, as long as it is
   before the submit. The writes stay exactly as today: no new buffers, no buffer device addresses.
2. The TLAS is read by address from the slot's scene stamp (set 1 binding 9, 512 bytes per slot), as the groom
   TLAS already is: alpha_commit writes it host-side (alphatest.cpp:1274-1277) and shaders call
   `OpConvertUToAccelerationStructureKHR` (rt_common.hlsli:955 `rt_groom_scene`, hair_primary_gbuffer.hlsl:67).
   That instruction already compiles on this DXC (Vulkan SDK 1.4.357, dxcompiler 1.9.0.5399) and runs on the
   5090 driver (api 1.4.357) in ray-query shaders (rt_hair_groom lives). The gi/ files that carry it also build
   the GI raygen pipeline (gi_rt_pipeline=1); a live with the groom and the raygen path both on is not on record,
   so increment 2's live window must include the raygen path. Push-descriptor binding 18
   (core/gi.cpp:2131 `bind_rt`) and water binding 64 get a layer-owned dummy TLAS (an empty, valid AS).

The stamp already carries per-slot build data the shaders read (generation at 0, groom at 48, OMM proof at 128,
fade capsule at 192, build camera at 256). New words (unused today: 16-31): reference TLAS address (16), the
traversal TLAS address (24: the OMM accelerated TLAS, or the reference when OMM is not ready).

Because binding 9 points at the slot's stamp, a fallback is one descriptor copy: copy set P (previous valid
slot) into set R before the submit, and every binding, including the stamp with P's TLAS address and P's build
camera, then describes P. No TLAS clone, no injected command buffer.

### 3.1 Slot states and lifetime (blockers 2 and 3)

A small state machine in a new pure header `common/scene_handout.h` (unit-testable, like
common/water_compose.h `pick_scene`), owned by rtscene.cpp:

- Reserve: at the first `rt_scene_handout` of frame N, or at `rt_before_capture` (rtscene.cpp:892) if it comes
  first. Picks a retired slot R (`pick_scene_slot` :204), records the current valid slot P, sets
  `last_use = N` on both (both count as live: `slot_retired` :181 then needs `gpu_done >= N + 2`). The lighting
  thread must not take `g_mu` (rt_after_capture holds it for the whole build): reservations sit under a new small
  `g_handout_mu`; the slot table moves under it too, or the G-buffer thread reserves at capture begin and the
  lighting thread only reads the published reservation. If no slot is free or P is stale: no handout (the frame
  takes today's path: `rt_scene_set` with the wait).
- Commit: rt_after_capture uses R when R is reserved and not yet resolved, else picks a slot as today. After
  alpha_commit it writes the TLAS words of R's stamp (host-mapped, like the groom words) and marks R committed
  with its build record (`res.rec`).
- Resolve at submit (layer/layer.cpp:509 / :559, next to `hair_primary_submit_begin`, which already decides at
  submit): for each lighting command buffer in the batch carrying a handout of R:
  committed AND (R's build record already submitted, `bvh_rec_state == 1`, OR its command buffer comes earlier in
  this batch) -> nothing to do. Otherwise -> fallback: `vkUpdateDescriptorSets` copy set P -> set R (all bindings,
  UAB allows it before the submit), `alpha_slot_in_use(P, N)`, R becomes "resolved as P" and a later commit for
  frame N picks another slot (set R must not be written again while that submit is pending).
- Blocker 2 (TLAS growth, ring and pool reallocation) disappears: the TLAS handle is never bound, only its
  address is written at commit; ring and pool bindings are UAB writes at commit. `ensure_tlas` (bvhsplit.cpp:1237)
  and `ensure_ring` (:1211) stay unchanged.
- Blocker 3 (bvh_build / alpha_commit failure, no capture this frame, lighting submitted alone or first): all
  resolve to P, the scene the RT passes would trace today after a timeout. Exactness note: P's dynamic instances
  read the shared XFB soup that frame N's capture overwrote (geocapture.cpp:2765 "next frame's capture into the
  same scratch memory"); that is today's fallback behaviour too, not new.
- Core hot reload with a reservation outstanding: the submit resolution still runs in the layer; no core state.

### 3.2 Device features and set 1 layout (blocker 1)

- Restart key `rt_scene_handout_layout` (Apply::Restart, off): in `rt_device_create_info` (rtscene.cpp:636)
  query and enable `descriptorBindingStorageBufferUpdateAfterBind`, `descriptorBindingSampledImageUpdateAfterBind`
  (covers the SAMPLER bindings 4, 5, 15, 19) on the game's own VkPhysicalDeviceDescriptorIndexingFeatures (it
  chains one: log "sampledImageArrayNonUniform 1, runtimeArray 1, partiallyBound 1"; patch it in place as
  dlss5.cpp:407 does), and `descriptorBindingAccelerationStructureUpdateAfterBind` on our `g_f_as` (binding 10).
  If the game already chains RT features (the "leaving the device alone" branch, :690) or a feature is missing:
  log it, key stays effectively off.
- alpha_device_created (alphatest.cpp:580): binding flags UPDATE_AFTER_BIND on every binding,
  `UPDATE_AFTER_BIND_POOL` on the layout and on the set pool and the hit-material clone pools
  (alpha_clone_hit_set :1497). Core pipelines get the layout through `rt_alpha_set`, so no core layout code
  changes. NVIDIA UAB limits (maxPerStageDescriptorUpdateAfterBind*) are far above kTextures 4096.
- Not needed: a persistent bindless texture table or buffer device addresses for soup/InstInfo/pool/stamps.
  UAB already covers per-frame views and buffers, and it keeps every shader buffer access unchanged.

### 3.3 Consumers (blocker 4)

- OMM: today the per-dispatch choice (core/gi.cpp:2318 `prepare_omm_traversal` -> `rt_omm_scene_reference`)
  is made at record time. With the handout the commit decides (`bvh_omm_scene`, bvhsplit.cpp:4013) and writes
  the traversal address; OMM variants (`RT_OMM_SCENE`) trace word 24, the others word 16; binding 10 (reference,
  for omm_scene_guard.hlsli) is a UAB write. Ticket uses (`bvh_omm_use` :4019 -> `register_ticket_use`) go on
  the G-buffer recording at commit; the submit check above guarantees the same batch. `mark_feedback_use` stays
  at handout time (it needs the lighting recording before it ends). A fallback traces P's reference TLAS (no
  tickets needed).
- Water: the stash keeps the lighting's pair (core/water.cpp:1077-1110, common/water_compose.h:379
  `pick_scene`). With the handout that pair is (dummy, set R), so water_reflect.hlsl:128 (`waterTlas`) reads the
  stamp address under the same spec constant. Water is recorded after the lighting, so it always binds a set that
  the submit resolution covers.
- Hair: the groom TLAS is already a stamp address; hair_primary already re-decides at submit and reads
  `rt_current_build_camera`, unchanged.
- Native hit materials (`rt_hit_material`, off by default) and the legacy single-BLAS path: no handout (today's
  wait path). Supporting them is a later increment only if wanted.
- `scene_current` in core (gi.cpp:3230) is stats only; counted at submit resolution instead.

### 3.4 Shaders

- One spec constant `kSpecIdSceneHandout = 1051` (common/shader_variant.h; 1047-1049 are reserved for the hair
  streams, 1050 is GiSer). A macro in rt_common.hlsli wraps every scene trace:
  `if (kSceneHandout) q.TraceRayInline(rt_handout_scene(gSceneStamp.Load2(RT_HANDOUT_WORD)), ...) else
  q.TraceRayInline(gScene, ...)`. Sites: rt_common.hlsli :1164/1167/1340/1343/1510/1513/1901/1904,
  gi/ls_ref/ls_ref_body.hlsli:25, rt/hair_receiver.hlsli:19, rt/water_reflect.hlsl:128.
- Request delta 0 (spec constant, no define). Recompile once: every request whose preprocessed text includes
  rt_common.hlsli (29 files; all 134 GI requests of shaders/variants.txt plus water, D-LS and hair requests).
- Key off: specialized to 0, frozen and folded (tools/spec_constant_proto.py canonical) = the previous program.
- GPU cost: one uniform 8-byte stamp load per query, expected < 0.01 ms; measure from the `gpu` lines anyway.

### 3.5 Host ABI

ABI 29 -> 30, one appended entry:
`bool rt_scene_handout(VkCommandBuffer cmd, VkDescriptorSet* set, VkAccelerationStructureKHR* dummy_tlas,
uint64_t* token)`. Core `rt_tlas` (gi.cpp:3208) calls it when `rt_scene_handout` is on and falls back to
`rt_scene_set` when it returns false. One ABI bump, merged early (AGENTS.md).

## 4. Alternatives considered

- Fixed per-slot TLAS handle sized ahead (no shader change): growth then forces a fallback, and the fallback
  needs a TLAS clone recorded into an injected command buffer at submit. More layer code and a new submit
  mutation, for avoiding a mechanism (stamp address) that already ships. Rejected.
- Buffer device addresses for soup/InstInfo/pool plus a bindless texture table: rewrites every buffer access in
  ~29 shader files and still needs UAB (or a persistent table) for per-frame views. Rejected: UAB alone suffices.
- Record the trace passes into a layer command buffer at commit and inject it into the submit between the
  G-buffer and lighting buffers: no UAB, no shader change, but every RT pass must be proven not to depend on
  work or image layouts inside the lighting command buffer before it. Unknown feasibility; not pursued.
- `rt_scene_wait=0`: traces the previous scene (primary match 33 % vs 97 %). Not a candidate.

## 5. Increments (each merges off by default, key off identical)

| # | Content | Files | Effort | Offline proof | Live |
|---|---|---|---|---|---|
| 0 | Run sh1 (exists, never run) | none | 0 | none | ~5 min camp; decides go/no-go |
| 1 | `rt_scene_handout_layout` restart key: UAB features, set 1 layout/pool flags, dummy TLAS | layer/rtscene.cpp (device create), layer/alphatest.cpp, settings | 1 d | settings test; GPU fixture under validation (bind UAB set, record dispatch, update, submit, read the new value; then the copy-set fallback) | folded into 3 |
| 2 | Stamp TLAS words written at commit; spec 1051 shader path; core binds with spec on but still waits | alphatest.cpp alpha_commit, shaders (11 sites), common/shader_variant.h, core pipelines spec slot | 1-1.5 d | spec identity (key off same program); frame_replay `--spec 1051=1` vs `=0` bit-equal on gi_hit_direct, D-LS and reflections packs (harness writes the replay TLAS address into the pack stamp) | folded into 3 |
| 3 | Handout: `common/scene_handout.h` state machine, reservation under `g_handout_mu`, submit resolution and fallback, OMM tickets on the G-buffer recording, water stash, ABI 30 | layer/rtscene.cpp, layer.cpp (both submit hooks), bvhsplit.cpp (omm use only), core/gi.cpp rt_tlas, core/water.cpp, common/host_api.h | 2-3 d | CTest scene_handout_test: reserve/commit/resolve, late build after fallback, no capture, lighting submitted first, retirement counts R and P live, OMM ticket owner | sh2 below |

Increment 1 done (2026-11-09, branch claude/scene-handout-1, off by default, not merged, no live): key
`rt_scene_handout_layout`, common/scene_handout_layout.h, CTest scene_handout_gpu_fixture (5090, 0 validation errors).

Increment 2 done (2026-11-09, branch claude/scene-handout-2, off by default, no live): stamp bytes 16/24 written at every
commit, downgrade at record time, constant 1051 (`rt_scene_handout`, live) at the 10 scene trace sites, `frame_replay --handout`.
Corrections to 3.3/3.4: water binding 64 is the water-surface TLAS, not the scene; water traces the scene through
rt_common (binding 18) and gets 1051 like the GI passes. The dummy at binding 18 moves to increment 3: while a
spec-0 pipeline may still run (async rebuild after a toggle) binding 18 must stay the real TLAS.

Increment 3 done (2026-11-09, branch claude/scene-handout-3, off by default, READY_FOR_LIVE, no live): `rt_scene_handout=2`,
common/scene_handout.h + scene_handout_test (every interleaving, mutation control), ABI 30, live job sh2-s8-scene-handout.
Not covered (increment 4): OMM acceleration on reserved slots, native hit materials (Ultra's Far bounce refuses the handout).

Increment 4 (2026-11-09, branch claude/scene-handout-4, off by default, no live):
- Done: OMM on reserved slots. The commit keeps the accelerated TLAS in stamp byte 24 when `bvh_omm_tickets` (bvhsplit.cpp)
  gives its micro-map tickets; they are registered on every recording that bound the handed-out set (the commit's users,
  `Machine::accelerate`, and later joiners in `rt_scene_handout`) while it records, so its submit keeps the data alive
  (omm_submit.cpp). A user that already ended cannot hold them: byte 24 goes back to the reference (monotonic, counted:
  `OMM accelerated commits A, downgraded D` on the handout line).
- Legacy single-BLAS builds: still the waiting path (they share one TLAS, never fill a reservation). Unchanged on purpose.
- Not done: native hit materials. Why they refuse (layer/hit_material.cpp `register_use`, :1169-1560): the query runs
  on the lighting thread at record time and needs, all on the CPU: this frame's prepared and attached slot (slot.frame ==
  frame_index(), scene_prepare at the capture end and native_hit::attach in alpha_commit), the finalized deferred packet
  of the G-buffer hooks (packet_for), the CPU pairing of prepass rows with deferred rows (q.materials, instanced pair
  checks), the lighting dispatch's camera binding equal to slot.camera, a set clone with appended texture tables
  (alpha_clone_hit_set copies set R's descriptors at that moment), and GPU work recorded into the lighting command buffer
  (row uploads, join/remap/gather dispatches, header {slot.frame, generation} at row byte 0). A handout binds R before
  any of this exists. Plan (increment 4b, about 2 days): build the query at the commit instead (G-buffer thread, after
  attach), record its uploads and join/remap into the G-buffer command buffer, write a per-slot query set (update-after-
  bind pool, clone of set R plus tables) whose handle the handout returns with set R; keep the frame checks as they are
  (row header frame + generation on the GPU; slot frame and generation at the commit); move the camera-binding check
  to the submit settle (the lighting's binding recorded at the handout; mismatch -> fallback to P's query set, the
  existing "reuse F-1 rows" rule, borrow_query); the fallback copies P's query set into R's like set 1.

Increment 4b done (2026-11-09, branch claude/scene-handout-4b, off by default, READY_FOR_LIVE, no live): the plan above
as written (commit-time query into a per-slot handout query set, camera check at the submit settle, P's query set as the
fallback). Still waiting after it: legacy single-BLAS builds, frames whose capture has not begun at the first RT query,
passes whose pipeline lacks constant 1051 (two frames after a toggle), water's once-per-frame settle in run_post.

Total 4-5.5 days after a go from increment 0. Gate to start 1: sh1 frame time(on) - frame time(off) >= 1 ms
(below 0.5 ms, inside the on-a/on-b spread: drop the handout, keep only S1/S2 if cheap).

## 6. Live gate (sh2, after increment 3, one launch per scene)

- Camp static and S8 motion, `fps_cap=0`, Foreground, FG, `rt_scene_handout_layout=1` at launch; toggle
  `rt_scene_handout` 0/1/0/1 in 120-frame windows (`expect_every` per window, hold 90).
- New line per window: `[rt] scene handout: N frames, committed C, fallback F (no capture X, failed Y, order Z)`.
- Gates per ON window: `[rt] scene wait` mean < 0.1 ms (only frames without a handout still wait) and
  committed = handed out (the `got = waited` analogue), fallback 0 in steady windows; `rt passes: GPU cost` and
  "TLAS this frame" equal to OFF within OFF-repeat noise; energy (dumps.json pre-post mean, static camera)
  equal within OFF-repeat noise; `rt_primary_check` match equal to OFF in one short dedicated window;
  `[pace]` median frame time ON below OFF by more than the OFF a/b spread (the deliverable).
- smoke_ultra with the other qualified keys on before the default flips.

## 7. Expected result and open risks

- CPU: the lighting thread's 3.3 ms (static) and 5-7 ms (motion) wait goes to < 0.1 ms. Frame time: up to that
  amount when the lighting thread is on the critical path; sh1 gives the real ceiling. GPU: ~0.
- Risks: (1) enabling UAB on the game's device (feature patching; off by default, restart key);
  (2) driver behaviour of UAB sets in the RT pipeline path (fixture plus live); (3) pairing hit rate between
  the reservation and "this frame's" capture (the G-buffer may begin before present N-1): wrong pairings are safe
  (they resolve to P) but cost quality, so the fallback counter must be 0 in steady windows; (4) `register_ticket_use`
  on the G-buffer recording must keep OMM data alive as long as the lighting runs (same batch check);
  (5) the slot-table lock split in rtscene.cpp is the riskiest code change (races between the G-buffer thread,
  lighting thread and the submit thread); keep it a small mutex around reservation state only;
  (6) replay packs record set 1 (`replay_note_alpha_layout`, layer/replay.cpp:1006): the UAB flags must be
  ignored or recorded there.

## 8. State on main and qualification (2026-11-14, branch claude/handout-ready)

### 8.1 What is built

| Increment | Content | State |
|---|---|---|
| 1 | `rt_scene_handout_layout` (restart): UAB device features, set 1 UAB layout and pools, dummy TLAS | complete, GPU fixture |
| 2 | TLAS addresses in the stamp (bytes 16 / 24), constant 1051 at the scene trace sites, `rt_scene_handout=1` | complete |
| 3 | `rt_scene_handout=2`: reservation at the capture begin, handout without the wait, resolution at vkQueueSubmit, ABI 30 | complete |
| 4 | OMM accelerated TLAS on reserved slots (tickets on every user recording) | complete |
| 4b | Native hit materials: query built at the commit into a per-slot handout query set | complete |
| 5 | Traversal flag (ABI 31), water on the handout | complete |

Keys today: `rt_scene_handout_layout` 0 / 1 at startup; `rt_scene_handout` 0 (wait), 1 (trace by stamp address, same
TLAS, still waits), 2 (handout; needs the layout on the device, `rt_scene_wait=1` and the BVH split, else it acts
as 1). Native hit materials, OMM (static and dynamic) and water run on the handout. The old ini comment "not with
rt_hit_material" described increment 3 and was stale since 4b; it is corrected.

### 8.2 The quality trade

In order (the normal frame) there is none: the lighting traces this frame's scene, as with the wait. The slot is
reserved at the capture begin and filled before the same vkQueueSubmit; the shaders read the TLAS address at execution.
It is not a one-frame-old scene. Only a fallback frame traces the previous frame's scene P (every binding of set P
copied into set R at the submit): build not committed, build failed, build submitted after the lighting, recording
dropped, hit-material query failed or bound another camera. That is what a wait timeout gives today; a moving
character's shadow lags one frame in such a frame. Frames that still wait (no handout): no valid previous scene (level
load, the first frames, after a skipped build), legacy single-BLAS builds, a first RT query before the capture began
(the 0.5-0.7 ms residual in motion), two frames after a pipeline without constant 1051 ran (a toggle).
Counters: `[rt] scene wait ... handed out N (0 ms)`; `[rt] scene handout: N frames, reserved, handed out, in order,
fallbacks F (not committed, build failed, order, dropped), pairing mismatches, committed first, unused, users refused;
OMM accelerated commits, downgraded; hit material queries at the commit, failed (fallbacks: query, camera)`;
`[rt] scene handout fallback: frame ... (reason)`; `[core] scene handout: ... settled ..., water on the handout`.

### 8.3 Live evidence so far (logs pruned; numbers from docs/archive/PROGRESS_CODEX_2026-11-11.md)

- sh2, 2026-11-09 18:43, S8 real Ultra, RTX Hair strands on: wait mean ON 0.47-0.69 ms vs OFF 4.4-5.1; 0 fallbacks,
  0 pairing mismatches; rendered median 24.2-24.6 vs 24.6-25.2 ms (GPU-bound); rt passes 6.2-6.6 vs 7.2-7.5 ms.
- sh2 rerun the same evening (increment 5): every gate passed but the in-motion wait (0.6-0.7 ms, static 0.06),
  0 fallbacks, water on the handout 59/60, no false OMM downgrades.
- Never run: sh1 (the ceiling), the inn, High, anything after 2026-11-09. Today's OFF wait (main 6f6cef07): inn Ultra
  7.9-9.6 ms, S8 3.2-5.6 ms.

### 8.4 Compatibility with what landed after it (code read 2026-11-14)

Since b5da66ae nothing changed in core/, layer/alphatest.cpp, layer/hit_material.cpp, common/scene_handout*.h or
shaders/rt/rt_common.hlsli; layer/rtscene.cpp gained only the build-camera UBO accessors.

| Feature | Verdict | Why |
|---|---|---|
| RTX Hair strands (`rt_hair_groom_primary_current`, header camera, `rt_hair_header_cam_gpu`) | works | The strand G-buffer traces its own header TLAS (hair_primary_gbuffer.hlsl:131, :152), published by the build (bvhsplit.cpp:4408) and re-decided at the submit after the handout resolution (layer.cpp:533 then :549). It never binds set 1. The build camera and its GPU copy are recorded into the build's command buffer (rtscene.cpp:1197-1213, hair_primary.cpp:780). sh2 ran with strands on. |
| Groom TLAS in secondary rays | works | A stamp address already (alphatest.cpp:1288-1293, rt_common.hlsli:983); written at the commit of slot R; a fallback copies P's stamp binding with P's groom. |
| Pixel LOD camera TLAS | works | Per scene slot like the groom TLAS (bvhsplit.cpp:1018, :4259); only the header names it; slot lifetime rules unchanged (rtscene.cpp:215-226, R and P pinned). |
| `rt_capture_wide_split` | works | Capture and BLAS level only; on in sh2. |
| VRAM guard latch | fallback exists | A refused or failed build ends as `build failed` -> P (rtscene.cpp:1337-1350 HandoutCommit); geometry left out does not fail the build. |
| OMM static and dynamic | works | `bvh_omm_tickets` hands out both ticket kinds (bvhsplit.cpp:4676-4685); a user that cannot hold them downgrades byte 24 (counted). |
| Water | works | core/water.cpp:1121-1137, core/gi.cpp:4115-4135 (increment 5). |
| Native hit materials | works | Commit-time query (rtscene.cpp:1445, :1945-1961); fallbacks query / camera counted. |

No incompatibility found, so no code fix. Not proven live: pixel LOD, header camera GPU copy, `rt_hair_mv_card`,
row / slot stability and the guard latch together with the handout (they share no state with it).

### 8.5 Qualification (staged, not run)

`tools/live_batches_staged/handout_qual/` (predicted.json first, README.md): five launches in one session, 0 / 2 / 0
inside each run, thresholds from the OFF repeat. Offline on 2026-11-14: scene_handout_test, scene_handout_gpu_fixture,
frame_replay_test (self-test 4c) and the whole suite pass; this branch changes no shader (variants.txt delta 0).

### 8.6 Launcher

Performance page, "Low-latency scene" (`scene_handout`): Off / On. On writes `rt_scene_handout_layout=1` and
`rt_scene_handout=2`, needs a game restart (Off is live), is marked Experimental (needs "Show Experimental") and is Off
in every preset until the batch above has run.
