# P4 opacity micromaps (OMM)

This is the one OMM document. It merges the former OMM_CONTENT_EPOCHS, OMM_EXT_BACKEND
and OMM_GPU_PRODUCER notes (originals in [archive/](archive/)).

Contents: 1 status, 2 native alpha predicate, 3 CPU groundwork, 4 EXT backend,
5 submission and lifetime, 6 GPU producer, 7 content epochs and source identity,
8 hardware fixtures, 9 remaining work, 10 user smoke test.

## 1. Status: isolated runtime candidate

Default rt_omm=0, Experimental GUI off in all presets. Both DLLs must match
(ABI 25 on 2026-11-02, `BG3RTX_ABI_VERSION` in `common/host_api.h`; OMM needs no ABI change since 19). Main/user build is unchanged. No current game/GPU validation
or measured acceleration/cost is claimed. Restart is required to request the extension.

The isolated renderer now wires alpha bindings 10/11 (original micromap-free
reference and coherent content epochs), generation-matched scene selection and
all four query entry points. Every actual Core dispatch path registers its own
traversal command, including local lighting, reflections and SHaRC. Ticket-use or
feedback-tracking failure declines acceleration; an unavailable tuple is rejected,
not counted as a quality pass. Reference-only scenes still have stable build IDs.

Snapshot/Bound retain the exact build generation, so a recycled AS handle cannot
substitute another frame's scene. Reference selection is scoped per dispatch and
restores the original bound state. Failed queries do not mark stale GI, local,
reflection, primary or Sun outputs fresh; GI uses the current screen-space trace.
Specular hit distance is cleared when reflection dispatch fails.

The layout capability persists through key-off/core reload. A core-consumer bit
is cleared on unload: an older core receives baseline scenes. An ordinary pipeline
caught during capability publication also receives baseline, even if another
pipeline already enabled acceleration. A later unsupported device keeps the
superset layout with baseline/dummy bindings and zero proof. Fresh key-off paths
still select ordinary shader files.

Alpha proof validation is outside the alpha mutex, preserving BVH -> alpha lock
order; native compute bindings are restored after it. Scene retirement and ticket
release use actual completion, not frame age. Sun's separate scene has no OMM;
its self-reference and cleared proof cannot redirect rays into the camera scene.

Offline checkpoint: both DLLs build; current composition passes 40/40 CTests.
Per-command binding tests cover 58 cases: reference fallback, reused generation,
key-off and unwind restoration. Against d444e70, 229 existing shader entries are
byte-identical; 198 OMM wrappers compile/validate. Wrappers retain native DXC 16-bit
flags. Short owned baseline extraction fixes Windows MAX_PATH without changing
the archived baseline source. No GPU execution follows from this shader gate.

2026-11-02 re-check on main 566db3d (ABI 25): both DLLs build, 96/96 CTests, the
OMM variants compile, the key-off shaders are byte-identical (section 1a). Nothing
was broken. Observables added for the live qualification (section 1a).

OPEN: actual renderer descriptor/GPU traversal validation; nonzero useful known
coverage; same-ray reference violation counts; energy/movement/FG2 quality;
capture/build/traversal GPU ms and real residency. All-UNKNOWN or missing RT never
passes. Current code is an unqualified opt-in candidate, not a shipped speedup.
READY_FOR_LIVE: tools/live_batches/omm_qual (S3 camp, 2 launches, about 7 min), not
authorized, not run.

**Expected outcome (offline, before the run).** Known coverage is about 0 at camp,
so no speedup is expected. (1) Only one audited recipe can bake
(`omm_native_plain_recipes.h` fs 16ac1391689391d0: a plain texture, no VT, 90-6340
triangles per frame in the 2026-10-30 logs); VT foliage is not eligible. (2) A
known state needs alpha exactly 1 under the triangle at EVERY mip down to 1x1,
because the cone can reach any mip and the stochastic hull is [0,1]: a cutout
texture's coarsest mip is fractional, so its triangles stay UNKNOWN; known
transparent never occurs. (3) When any range is attached, `rt_omm_scene_current()`
makes five per-lane atomics into the host-visible content-epoch buffer on every ray
query (the host-atomics pattern, 7-15 ms in 2026-11; fixed 2026-11-02, 1a). If the run confirms ~0
coverage, reject rt_omm for Ultra; a useful version needs a VT provider, a
footprint-bounded mip domain and a device-local guard first.

### 1a. Live observables (2026-11-02)
- `[omm] last N builds` (layer, every rt_timing_frames builds while the backend is
  available): alpha draws / native plain captures; alpha-tested triangles per build
  (static, per-draw), with a capture, with an attached micromap BLAS (and the % of
  alpha-tested); variants baked / reused / declined; accelerated scenes recorded /
  declined; micromap + BLAS bytes (requested), bake input bytes; cumulative baked
  microtriangles: known opaque, known transparent, unknown with a complete proof,
  unknown for a missing proof (omm_bake_plain.hlsl `bakeStats`, device-local,
  wave-reduced, copied to a transfer-only readback after each bake).
- `omm check (N frames)` (core, rt_stats=1, rtStats 435..442): the OMM variants'
  `rt_visibility` and `rt_closest_hit` trace again in the reference scene when the
  accelerated one was current; compared queries, queries without a current
  accelerated scene, visibility / closest-hit violations (must be 0; closest hit
  compares t exactly), unknown pairs excluded, candidates in each trace. The
  reference trace counts nothing in the ordinary counters. `rt_closest` (t only) and
  `rt_closest_debug` are not checked.
- `rt_omm_reference` (live, ls_flags bit 28, logged as `gi: rt_omm_reference`):
  the guard still runs but every OMM query traces the reference scene: the
  in-launch A/B of the traversal saving.
- Cost: the existing `rt passes: GPU cost`, `gi: GPU cost`, `[rt] scene build GPU`
  (bake, micromap BLAS and clone TLAS sit in its TLAS part) and `[mem]` lines. The
  content-epoch dispatches at image writes run in the game's command buffers and
  are in no timed pass.
- Key-off identity: with rt_omm=0 the ordinary shaders compile to the same SPIR-V
  (the traced bodies keep their names through `RT_OMM_TRACED`).
- 2026-11-02 after the merge of main: the check moved to rtStats 443..447 (main took
  435..442; 448 is the frame_replay limit), 5 counters: compared, not compared
  (not current or an unknown answer), violations, candidates per trace.
- Guard cost fix (2026-11-02): `rt_omm_scene_current()` runs once per wave and
  invocation (first active lane, `WaveReadLaneFirst`), not five per-lane atomics
  per query; the content-epoch buffer prefers BAR VRAM (DEVICE_LOCAL|HOST_VISIBLE|
  HOST_COHERENT; the host only memsets it once), logged `device_local=1`.
Only an explicitly authorized queue batch may test it, with automated A/B/settings,
input/captures/restoration. No manual INI/toggles are requested.

## 2. Native alpha predicate and the classifier

`shaders/rt/rt_common.hlsli` alpha_eval samples the texture/VT at the ray's
footprint. At levels <= 1 it compares alpha >= cut. With stochastic alpha enabled,
k = saturate(level - 1); threshold = lerp(cut, alpha_hash, k). The 24-bit hash
grid includes zero. A constant-alpha 0.75 texture therefore is not universally
opaque, and constant 0.25 is not universally transparent, even if every mip is
identical. Statistical agreement cannot justify a wrong hardware known state.

Do not shift the hash by 0.5 as part of OMM. That changes the feature-off runtime
and the highest half-integer can round to 2^24 in float32. Any physical endpoint
fix must be a separately justified change with its own checks.

The classifier (`common/omm_classify.h`, first written as `layer/omm_classify.h`)
classifies an outward-rounded alpha interval against a proven threshold interval.
Opaque requires lower(alpha) >= upper(threshold); transparent requires
upper(alpha) < lower(threshold), because the runtime uses >=. Incomplete,
nonfinite or inverted bounds return UNKNOWN.

The legacy all-footprint stochastic threshold hull is [0,1], including rounding.
Hard-cut-only operation has [cut,cut]. Even alpha zero cannot be declared
transparent for the full stochastic domain, because the native predicate accepts
hash zero. Restricting to hard cut is legal only when that restriction is enforced
for every ray using the BLAS; the runtime flag/policy must be part of the bake identity.
Uniform one can be Opaque; uniform zero stays UNKNOWN under [0,1].

The CPU oracle uses an independent predicate over alpha intervals, cutoffs,
hard/transitional/coarse footprints, hash endpoints and fused/nonfused lerp, and
the old fractional-alpha counterexamples: 197,621 checks over 142 known interval
cases (93 opaque, 49 transparent). It does not prove texture bounds, GPU filtering
or traversal correctness.

History: branch codex/p4-omm started from main f98bfb2. The old branch
worktree-agent-a8b7cb486c1434ebd at 8b3b87a held a design only; its mip-0/1
known-state rule is unsafe for the current alpha evaluator and is superseded.

## 3. CPU groundwork (merged to main e277548)

CPU domain preparation was merged to main e277548 through merge-green (branch
codex/p4-omm-domain from main 47ac6a9). Its statements about absent production
callers are superseded only in the isolated candidate of section 1, not in main.

- `common/omm_alpha_bounds.h`: validated immutable selected-channel LINEAR UNORM8
  mip planes; a conservative WHOLE-MIP code hull across sampler/image-clamped LOD
  support, including both trilinear endpoints. Repeat/negative UV and scale cannot
  escape it. It validates all declared levels, dimensions, spans, residency and
  arithmetic, and checks max_texels before data reads. Missing/unsupported
  VT/sRGB-RGB/raw compressed data or an exhausted budget returns incomplete.
  CompleteCodeCoverage is an INTEGER support statement, NOT a GPU sampler/LOD/
  conversion proof. VT is deliberately unsupported by this provider.
- `common/omm_bake_identity.h`: exact geometry/UV/material/content/view/descriptor/
  sampler/precision/evaluator/policy identity, primitive/microtriangle/subdivision
  identity, and a matching submitted-work completion timeline. Frame age or raw
  handles cannot certify readiness. Content/residency changes invalidate even live views.
- The spatial integer-support provider later replaced whole-mip scans with a
  wrapped UV rectangle and a conservative filtering halo (246,013 independent
  ideal-sampler/validation checks). It is still not a floating-point GPU sampling
  certificate.

**OMM SDK contract (CPU only).** `external/omm-sdk-1.10.0` is present. Optional CMake
BG3RTX_OMM_CPU_PROBE (OFF by default) builds a CPU-only SDK probe.
- SDK defaults assign FULLY_TRANSPARENT at equality (strict >), while native accepts
  it: 18 constant UNORM8 cases gave six known-state disagreements (at 0, 128/255
  and 1, nearest and linear). Do not bake with defaults, and do not repair coverage
  by forcing unknown states to two-state opaque/transparent.
- Conservative setting: alphaCutoffLessEqual = UnknownOpaque, alphaCutoffGreater =
  Opaque, cutoff unchanged, FOUR-state mode. Every UNORM8 constant code at equality
  and adjacent float32 cutoffs in [0,1], nearest and linear: 1,532 cases, ZERO
  contradictions, 12,192 known opaque and 12,320 unknown states, zero known
  transparent. It gives up all known-transparent acceleration, and applies only to
  a fixed hard cut with the same sample (not stochastic thresholds, general
  textures, other filters/LOD, the GPU baker or Vulkan traversal).
- Mixed-texture fixtures: four 4x4 UNORM8 zero/one stripe fixtures, nearest and
  linear mip 0, three primitives each at subdivision 0, DisableSpecialIndices to
  force real array decoding. Each bake maps to one Opaque and two UnknownOpaque
  states (the crossing primitive MUST stay UnknownOpaque, the solid one MUST stay
  Opaque). A 65-edge barycentric lattice gives 25,740 checks, zero contradictions,
  66 exact alpha=cut points, no epsilon. Eight decoder tests cover byte ends,
  truncation, huge offsets, invalid subdivision/format and null storage.
  Evidence: `docs/evidence/p4-omm-sdk-cpu-mixed-20261030.txt`.

Offline full build / CTest at that checkpoint: 19/19 (probe enabled); 398,526 alpha
code-support checks; bake identity checks in `docs/evidence/p4-omm-domain-20261030.json`.

## 4. EXT backend (`layer/omm_backend.h`)

The layer requests **VK_EXT_opacity_micromap**, its `micromap` feature and core
Vulkan 1.3 synchronization2. Vulkan 1.2 keeps ordinary RT but declines OMM.
Explicitly disabled application features are respected. Private EXT function
pointers avoid changing the shared dispatch table. Core/Vulkan12 bufferDeviceAddress
must be enabled; the EXT-only BDA configuration is declined. The KHR micromap object
API is not used: object creation, build, destruction and triangle attachment all use
EXT. If the driver rejects the optional request after reporting support, an OMM-only
retry rebuilds ordinary RT first; later retries keep OMM suppressed.

- Inputs: immutable GPU packed state bytes plus a GPU triangle descriptor buffer.
  Both need 256-byte alignment and MICROMAP BUILD INPUT READ ONLY + SHADER DEVICE
  ADDRESS usage. The CPU descriptor span must equal the uploaded bytes. Validation
  checks every offset and extent, four-state format, subdivision/property bounds,
  histogram counts and address arithmetic. It does not prove a known state true:
  that is the provider's job. UNKNOWN must still run native alpha. Never use
  force-two-state or force-opaque instance flags.
- `record` allocates micromap storage and aligned scratch, queries build sizes,
  creates the object, records producer/build/BLAS barriers and returns a ticket.
  `attachment` supplies the triangle-opacity pNext (index NONE, base triangle 0,
  exact primitive count; one-to-one triangle order only). The caller preserves
  unrelated pNext structures and marks every submission that uses the BLAS.
- Optional `Input.input_owner` holds producer buffers, pipeline and source snapshot
  until ticket retirement; it is reset only after the micromap, storage and scratch
  are destroyed. Bare-address inputs stay caller-owned until build completion.
- The backend also builds/owns the attached material BLAS under the ticket. BLAS is
  destroyed before micromap/input storage, after completion and command-reference
  retirement. Storage, scratch and BLAS allocations count against a 256 MiB budget
  (requested bytes, not a measured footprint).
- All recording, submission and completion must be on the same compute-capable
  graphics queue; cross-queue ownership is not implemented.

Specification source: Vulkan SDK 1.4.357.0 `vulkan_core.h`, `vk.xml`,
`validusage.json`. Relevant VUIDs: `vkCmdBuildMicromapsEXT` 07509-07515,
`VkMicromapCreateInfoEXT` 07437/07438, triangle-opacity 10719/10722/10723.

## 5. Submission and lifetime (`layer/omm_submit.*`)

- The opt-in device creates a private timeline semaphore. Command allocation/pool/
  begin/end/reset/free hooks track recording generations; ExecuteCommands keeps
  secondary generation identity. `record` rejects untracked, wrong-device or
  non-graphics-family command buffers.
- Scene/BLAS acquisition calls `register_ticket_use(cmd, ticket)` for every
  referencing traversal/build and rejects a false return.
- Before the application's QueueSubmit/QueueSubmit2, referenced tickets are pinned.
  After SUCCESS, a separate empty private submit signals an ordered timeline value;
  only its successful return stamps the tickets. The application's pNext and
  timeline values are never changed. Failed application submits release pins;
  failed private signals, stale generations or foreign-queue submissions keep
  resources until device idle.
- `mark_submitted(ticket, serial)`, then `retire(ticket)` once no scene can use it,
  then `collect(completedSerial)` destroys only retired, completed resources. Serials
  are timeline values, never frame indices. `discard_unsubmitted` only after the
  command buffer is reset or discarded. Teardown waits idle.
- Fresh rt_omm=0 adds no GPU calls. A session that enabled OMM keeps tracking held
  tickets through INI off.

CPU tests at that step: 87 plan/range/layout/reservation/lifetime checks and 101
command-graph/queue/counter/input-owner checks. The coordinated Sun ABI 19 change
(6913124) was included before that build.

## 6. GPU producer

`record_plain` bakes from one registered plain texture view and a triangle vertex
range. Level-0 micromaps use the exact EXT descriptor order, one primitive per
micromap, four states, four bytes per primitive (two bits used). The kernel bounds
wrapped neighbouring texels over every reachable mip (derived from the view; callers
cannot omit coarse mips) and compares an outward alpha interval with the native >=
threshold domain. Incomplete domains, nonfinite data or exceeded texel budgets stay
UNKNOWN.

- The first dispatch atomically snapshots image/global epochs and the sticky-disable
  state; every primitive rechecks them before and after sampling. Output has a
  256-byte prefix with that stamp and an atomic invalid flag.
- The default sampling certificate is ZERO. It needs matching interpolation/UV/LOD/
  numeric/content proofs, never a plausible-looking hull, fixture, handle or frame age.
  BC/sRGB sources only accept the uniform endpoint proof (section 7).
- Ownership: source snapshot, pipeline/layout, micromap input and descriptor buffers
  live until backend retirement. Pipeline reload keeps prior generations. If EXT
  recording fails after compute was recorded, one orphan owner is kept until device
  idle and production stops. Inputs capped at 128 MiB; per-dispatch work capped at
  2^24 texel visits (a cap, not a measured cost). The caller restores native compute
  bindings.
- Owned post-VS snapshots: `allocate_plain_vertices(cmd, bytes, &out)` allocates from
  the same 128 MiB budget (storage, AS build-input, transfer dst/src usage) without
  recording commands. `PlainInput.copy_source`, `copy_source_bytes`, `copy_offset`
  request a same-command copy into the private snapshot; aliasing, unaligned or
  mismatched ranges reject before recording. The caller keeps source validity and
  TRANSFER_SRC usage through the copy; only the private copy must outlive traversal.
- Scene validator and all four reference-query branches are implemented; 14 GPU
  validator cases cover stale/current/unrelated/multiple image epochs, invalid
  records and in-progress writers.

## 7. Content epochs and source identity

**Write protocol.** An AFTER-only increment can certify torn state across queues.
So each eligible copy/blit/resolve/clear is bracketed: GPU active-writer increment
BEFORE the native command, then image epoch, global epoch, memory fence and active
decrement AFTER it. Header (16 bytes): global epoch at byte 0, sticky disabled at 4,
active writers at 8; image counters start at an alignment-derived stride >= 16.
Begin/end use specialization constant 0 on one shader module, no push constants,
immutable descriptor sets, and native compute bindings are restored. A replay runs
both phases again. Queue family comes from the recording's command pool.

- Untracked/unsupported queues, active push descriptors, failed set allocation and
  NV/KHR indirect image copies record a sticky disable (CmdFillBuffer) BEFORE the
  native write. Missing completion leaves active > 0; unpaired completion and
  saturation disable permanently. Counters saturate at UINT32_MAX, never wrap.
  Disabled always requires the reference TLAS.
- Readers reject active writers, check epochs before and after, and use atomic GPU
  reads (for example InterlockedOr 0). The counters do NOT order native image reads
  against writes: native semaphores/pipeline dependencies must cover injected reads.
  `content_dispatch_in_submit(spanCBs)` covers direct updates, sticky fills,
  secondary snapshots and ticket readers; Submit2 signal stage masks of affected
  entries become ALL_COMMANDS (values unchanged). Bake commands call
  `mark_content_dispatch` before sampling.
- Fresh rt_omm=0 allocates nothing. Successful OMM init latches instrumentation until
  device teardown (held BLASes may reference micromaps). Storage is host-coherent,
  zeroed before first use, 1,048,592 bytes (65,536 slots). Two tiny dispatches per
  supported write; GPU cost not measured.
- Teardown order: backend resources after completion, then baker/orphan inputs after
  the device-idle guard, then content storage.

**ContentSource.** Supplies counter buffer, extent, per-image byte offset, a
process-unique 64-bit image ID and a never-recycled slot. A zero per-image epoch
means uninitialized content. Eligible images only: 2D, one layer/sample, optimal
tiling, empty pNext/flags, R8/RG8/RGBA8/BGRA8 UNORM, usage sampled/transfer only,
known non-host-visible backing. Storage, attachments, host transfer, mutable/alias/
sparse/external images, unknown extensions or bind pNext, raw device-address
commands and dedicated aliasing decline. Free/recreate invalidates lookup.

**Views.** Native proof uses `content_source_view`. Views get never-reused 64-bit IDs
plus the parent image generation. Accepted: 2D, flags 0, empty pNext (so
VkImageViewMinLodEXT declines), colour aspect, layer 0, exact plain UNORM format,
nonempty in-range mip span (REMAINING resolved from image metadata). Base mip, level
count, format and swizzle are returned and belong in the bake identity: bake and
native alpha must bind the SAME view.

**BC and sRGB.** `requiredSamplingCertificate` is metadata: 1 = general plain-UNORM8
interval contract, 2 = only uniform decoded 0/1 endpoints. BC1/2/3/4/5/7 UNORM and
their sRGB forms, and plain 8-bit sRGB, require 2. SNORM, BC6 and unknown formats
decline. Vulkan converts sRGB RGB after decode; alpha stays linear. Weighted-average
filtering preserves constants algebraically (an inference from the spec, not a
portable rounding guarantee).

**Samplers.** Tracked from descriptor writes/templates/copies, no ABI change. IDs are
never reused. Snapshot: filter/mipmap/address modes, LOD min/max/bias, anisotropy,
compare, normalization, border, flags, reduction mode; custom-border/Ycbcr chains
decline. `BoundSampler` returns sampler ID/state, set/layout generations, write
version, binding flags and immutable status. Mutable state is current CPU metadata,
not a frozen certificate, so the factory requires `immutable_sampler_current`.
CPU tests: 32 create-state and 42 binding/generation checks.

**Native plain material factory.** `capture_native_plain` captures the native draw
pipeline, exact audited FS/VS recipe, AlphaSnapshot, view/content identities and
immutable sampler metadata; it needs a frozen sampled-image binding. Unknown recipes,
WPO recipe 2, UV/material changes, mutable samplers and stale generations decline.
`make_native_plain_input` needs the SAME command recording and an owner for the
post-VS triangle-list range (float4 position, float2 UV at byte 16), rechecks
identities and selects certificate 2, every view mip and thresholds [0,1]. 40 CPU
checks. Capture runs at eligible static draw bindings but does not attach to BG3 yet.

## 8. Hardware fixtures (RTX 5090, explicit Khronos validation, zero errors)

None of these is a BG3, FG or performance gate. No window, presentation or input.

| Fixture | Result | Evidence |
|---|---|---|
| EXT six-ray gate (BG3RTX_OMM_GPU_SMOKE, `tools/omm_ext_smoke.hlsl`, cs_6_6) | 3 micromaps on a real BLAS/TLAS; known-opaque rays hit with 0 candidates, known-transparent miss, UnknownOpaque give candidates; 0 contradictions; 640 storage / 1,023 scratch bytes; timeline retirement collects | `omm-ext-smoke-20261030.txt` |
| UNORM8 endpoints (entry `endpoints`) | RGBA8 alpha 0/1, 3 mips, trilinear, REPEAT, 64 UVs x 17 LODs: 2,176 exact samples; device reports 8 sub-texel and 8 mipmap precision bits | `omm-sampler-endpoints-20261030.txt` |
| BC endpoints (entry `bc_endpoints`) | BC3/BC7 UNORM/sRGB, alpha 0/1: 8,704 exact Load and SampleLevel samples; BC1/2/4/5 not hardware-tested | `omm-bc-endpoints-20261030.txt` |
| Write protocol | first write, replay, saturation, sticky disable, overlapping writers, partial completion; dedicated transfer-only queue not tested | `omm-write-protocol-20261030.txt`, `omm-content-epochs-20261030.txt` |
| View identity | view IDs 1 then 2 on recreate, lookup fails after destroy | `omm-content-view-20261030.txt` |
| Producer | Opaque, Transparent, UNKNOWN-without-certificate outputs, exact stamps, 3 real EXT builds; owned snapshots byte-exact with offset 16 copies | `omm-owned-vertices-20261030.txt` |

The six-ray rays traverse the backend fixture, not producer micromaps: do not treat
the two as a native-textured scene gate. Evidence files are in `docs/evidence/`.

## 9. Remaining work

1. Alpha bounds over each microtriangle's UV and actual filter support, every
   reachable mip and VT fallback, channel, scale and transform, with outward
   precision (no tuned epsilon). Higher subdivision; VT coverage.
2. Missing/streaming VT data is UNKNOWN. A known state is usable only while material,
   texture/image generations, sampler, cut policy, UV topology and residency match.
   Animated/deformed geometry also needs proven UV stability.
3. Bake UNKNOWN whenever proof is missing; never force it, never drop alpha
   evaluation for unknown candidates. Measure known coverage before choosing a design.
4. Material-specific BLAS variants with baseline/reference TLAS, scene validation
   dispatch and descriptor wiring, exact ticket use/retirement at every build/traversal.
5. In one run, compare hardware-known states with native alpha on the same ray,
   footprint, mip/residency and policy: violations must be zero.
6. Serial S3/S4 foliage and S5/S6 controls, same-session off/on: correctness first,
   then memory/build/query cost with FG active. Particles stay on the existing path.

Unsupported CHAR_Hair/CHAR_Scalp cutouts are captured opaque in the current renderer
(see NATIVE_HAIR.md); that coverage must be correct before OMM accelerates it.

## 10. User smoke test (2026-10-31)

User tried OMM and Current Cascade Sun together and reports encouraging visuals,
no obvious new defects. This is not an isolated OMM A/B or performance gate.
Slight dialogue shadow flicker predates this test; source remains unknown.
No default promotion. See HANDOFF_SESSION.md for the current state.

## 11. Path to a useful OMM (plan, 2026-11-02)

Live upper bound (camp hig1 09:54, rt_alpha_test=0 forced opaque, static, exposure
held): rt passes 8.0 -> 3.8 ms, local-light shadows 1.03 -> 0.41, reflections
1.16 -> 0.39: about 5.7 ms if every alpha candidate were resolved. 78% of the
evaluated candidates are clear texels, so known-transparent is where OMM wins.

**Coverage today (camp, lr1 043856 alpha table).** About 700k alpha-tested
triangles per frame: VT shader 738494714d50b4c7 636k (91%), other VT about 33k,
plain-texture shaders about 26k (c65dc7ff 16.2k, c22b2025 8.4k, 9aaa6d61 1.3k,
16ac1391 0.45k). The only audited recipe (16ac1391) is 0.06%: the current code
cannot reach more than that, whatever the classification rule. VT decides it.

**11.1 Classification at the any-hit's LOD (not done: not small).** The RT test
(rt_common.hlsli alpha_eval / tex_alpha) picks the LOD per ray:
lod = log2(g x max|scale| x texsize), g = footprint x sqrt(area_uv / area_world),
footprint = cone.f0 + cone.spread x t, positions in world space (o2w applied), and
k = saturate(lod - 1) blends the cut towards a per-frame hash. A micromap state is
per triangle and ray-independent: it agrees with the test only for rays whose
footprint keeps lod <= L at the triangle. Two exact options:
- A, footprint gate (no image change). Bake mips 0..1 with the hard cut [cut, cut]
  (k = 0 there), so known-transparent is allowed (all decoded texels 0 under the
  UV rectangle plus halo, certificate 2; mixed values only with certificate 1).
  The bake also writes, per variant, F_obj = min over KNOWN triangles of
  2 / (max|scale| x texsize x sqrt(area_uv / area_obj)) (InterlockedMin on the float
  bits into the data header). The clone pass takes F_scene = min over replaced
  instances of sigma_min(o2w) x F_obj (Gershgorin bound on M^T M; <= 0 closes the
  gate) and writes it next to the scene proof. A query uses the accelerated scene
  only if max(f0, f0 + spread x tmax) <= F_scene; else the reference scene.
  Splitting a visibility ray at t_gate = (F_scene - f0) / spread (accelerated
  [0, t_gate], reference after) keeps it exact for long rays at two traversals.
  Files: omm_bake_plain.hlsl (domain, F_obj), omm_bake.cpp / omm_native_plain.cpp
  (max_lod 1, threshold [cut, cut]), omm_scene_clone/validate.hlsl + bvhsplit.cpp
  (F_scene in the proof), omm_scene_guard.hlsli (gate per query). About 150 lines
  plus fixtures. Open risk: with foliage at about 512 texels per metre the bound is
  a few mm; any shadow ray with spread > 0 and tmax ~100 m fails the gate, so the
  split variant is the only one with a real share. That share must be measured
  (candidates per trace in `omm check`) before it is worth the work.
- B, fixed fine-LOD policy for baked materials (image change). The RT test of OMM
  materials uses lod <= 1 with the hard cut for every ray, which is the geometric
  mask; the micromap then agrees by construction. Gate: energy of the pre-post
  colour key-on vs key-off within noise (a hard cut at the fine mip keeps the
  coverage for 0/1 masks; stochastic coarse coverage differs only for fractional
  texels). Smaller code (about 40 lines: a flag bit in the alpha record, the LOD
  clamp in tex_alpha/vt_alpha, the bake domain). Needs a coordinator decision: it
  changes the RT image at distance.

**11.2 VT foliage (the 91%).** The RT test reads VT alpha from the game's streaming
page cache (vt_alpha: indirection entry, tile cache layer, coarser resident page
when the requested one is not resident, counter 15). States baked from that cache
would depend on residency and change with streaming; the content-epoch protocol
declines it (cache images are written by the game). So the bake source must be
offline, and the RT test of those materials must read the same data:
1. Offline (tools/assets, like the assetdb): for each foliage VT material, extract
   the alpha layer of its texture from the GTS/GTP tile set with LSLib
   (`VirtualTileSet.ExtractTexture`, already used by tools/assets/probe_foundry_vt.ps1),
   mips 0..1 (option A/B) or all mips, store as BC4 (0.5 B/texel) in data/assetdb
   keyed by material + tile-set texture name. Cost: a 2048 x 2048 mask with mips is
   about 2.8 MB; camp's foliage is a few dozen materials, about 100-150 MB on disk
   and in VRAM when resident; extraction minutes once per game version.
2. Runtime: the layer binds these masks as ordinary textures (gAlphaTex slots) and
   sets the alpha record of a matched VT draw to the tex kind with the offline mask
   (a new record flag), so the RT alpha test no longer depends on residency (also a
   quality fix: no coarser-page reads). Matching uses the existing material
   identity (VirtualTexture_TilesetDataIndex / uv transform in the asset db,
   tools/assets/extract/materials.py).
3. OMM bake per pooled mesh and material once (existing variant cache), from the
   offline mask through the existing plain producer (content epochs not needed:
   the mask is immutable after upload; its view identity is enough).
Estimated work: offline extractor + db format about 1 day; runtime binding and
record about 1 day; OMM provider from immutable masks about 0.5 day; then the
omm_qual batch. Prerequisite either way: 11.1 A or B for the LOD domain.

**11.2a Done offline (2026-11-02): camp grass masks, key `rt_vt_alpha_masks` (live, off).**
- Source: the camp pack 013545-2 has 7 distinct (tile set, texture) VT records of shader 738494714d50b4c7
  (1.30M triangles, one texture 1.155M). Tile set = record m0 row 3 column 3, texture rectangle = p0 x the
  tile set's level-0 size (65536). `tools/assets/vt_alpha_masks.py` extracts the 7 GTS and the needed GTP from
  VirtualTextures*.pak, LSLib (`tools/assets/vt_alpha.ps1`, pwsh 7) decodes levels 0..P1.x (LSLib gives no
  level below one tile; the RT test never reads past the record's mip clamp P1.x = 2, 3 for one texture), and
  the BC3 alpha halves are the BC4 masks, bit-exact. Size: 7 masks, 1.65 MB (512 x 512 x 3 levels = 168 KB
  each). Files ADDED to data/assetdb/vt_alpha/ (index, manifest, 7 .vtalpha); nothing there was changed.
- Agreement (`tools/assets/vt_alpha_agree.py`, the pack's indirection + BC3 tile cache, vt_alpha() with the
  mip forced, every mask texel of every requestable level): where the page is resident at the requested level
  the read equals the mask bit for bit (100.00%); triangle-weighted the alpha test agrees on 98.51%, differs on
  1.49%: 1.48% on coarser pages, 0.01% elsewhere (bilinear at page borders).
- Runtime (layer/alphatest.cpp, common/vt_alpha_masks.h + unit test): at the first alpha_commit with the key
  on, the masks are loaded and uploaded (BC4 images, once); from the next capture a matched VT record (same
  tile set, same p0 bits) becomes a plain-texture record (kind 2: tex_alpha with the mask, channel 0, scale 1).
  No shader change, so key-off SPIR-V is untouched. Differences of the kind-2 path from the VT rule: trilinear
  LOD log2(N g) instead of the VT's rounded mip of log2(N g) - 0.5, and a continuous stochastic weight. Held
  records keep mask views (no tracker id). GUI: Experimental "Offline cutout masks".
- Live job (not run): tools/live_batches/vt_alpha (camp, OFF/ON/OFF, 4.1 min, 0.19 GB).
- Option A share (`tools/assets/vt_alpha_gate_share.py`, same pack): foliage texel world size median 1.08 mm
  (p10 0.69, p90 2.03); receiver distance median 22.2 m (p10 14.6, p90 50.6), pixel angle 1.04 mrad. The gate
  (footprint < 4 texels, VT mip <= 1) holds for 1.15% of shadow-ray (pixel, foliage triangle) pairs at t = 0
  (an upper bound; 0.39% at t = 5 m) and 0.005% for GI rays. At camp distances the RT test reads the clamped
  mip 2 with a stochastic threshold: option A is not worth building; only B (a fine-LOD policy) can give OMM a
  share.

**11.2b Option B chosen (2026-11-02, coordinator for the user): `rt_alpha_fine_lod` (live, off).**
- Implementation without a shader change: a masked record (rt_vt_alpha_masks) binds a mip-0-only view of its
  mask; tex_alpha clamps its LOD to the view's last level (0), so every ray reads mip 0 bilinearly and
  k = saturate(lvl - 1) = 0 gives the hard cut. Logged as `[vt alpha] ... X at mip 0 with the hard cut`.
- Energy, derived offline from the masks: the triangle-weighted grass alpha coverage is 0.2622 with B off
  (camp distances reach the clamped mip 2 with a stochastic threshold, P(solid) = E[a2]) and 0.2951 with B
  (fraction of mip-0 texels >= cut 0.2): x1.125 occluder area for grass. B is NOT energy-neutral by
  construction; the scene-level change is measured by omm_qual om3 (B OFF/ON/OFF, masks on, camp, 123 s).
- Micromap coverage at mip 0 (`tools/assets/omm_mask_coverage.py`, camp pack, 35,047 of the 1.30M grass
  triangles, uniform subdivision, bilinear support = uv box + 1 texel, margin 1/255 for BC4 decode):
  level 0 2.5% known; 1 9.3%; 2 20.4%; 3 34.3% (19.0% opaque, 15.3% transparent); 4 48.7% (22.9 / 25.8);
  5 61.9% (27.8 / 34.1). Per-triangle micromaps would be 19.8 / 79.3 / 317 MB at levels 3 / 4 / 5 for 1.30M
  triangles; the 179 grass instances are instances of few meshes, so micromaps shared per mesh (index
  buffer per triangle) are far smaller. Level 4 is the first candidate.

**11.2d All camp VT alpha shaders masked (2026-11-02, set `_camp2`).** The camp pack has 17 VT alpha shaders
(2.09M triangles); all use the same recipe (albedo layer, column 0; cuts 0.2 to 0.5), so the same pipeline
covers them. The 16 besides the grass: 67 (tile set, texture) records, 65 masks, 79.2 MB (a few large
textures dominate: one 4096 x 4096 mask is 10.7 MB for 1,038 triangles). Two textures smaller than one tile
cannot be extracted by LSLib (3c090656, 12,938 triangles of 84d5df19; f6c74ea5, the 154 of e3b1d7c2): they keep
the page cache. Added as data/assetdb/vt_alpha/vt_alpha_index_camp2.txt + manifest + 64 new masks (one texture
is shared with the grass set; the layer reads every vt_alpha_index*.txt, first entry wins). Agreement on the camp
pack, triangle-weighted: alpha test 99.05% (bit-exact 93.60%), differs 0.95% (0.94% on coarser pages); per
shader from 92.99% (e62cdef4, 581 triangles) to 100.00% (4bed5db2); b2d72ea7 99.05%, 0d91554d 97.18%, f9ab595d
99.26%. Masked share of the camp VT triangles: 2.08M of 2.09M (99.4%). Candidate share, estimated from the om3
counters (the vt bucket split by triangles, the tex bucket mostly the grass masks: plain textures are about 26k
triangles at camp): about 97% of shadow-ray and 98% of visibility-ray alpha candidates would read a mask, up from
18% / 37%. Total masks 80.9 MB (VRAM when the key is on).

**11.2e va1 live (camp, 2026-11-02 140543, VALID): masks alone are image- and cost-neutral.** Energy shot-on vs
the OFF mean +0.24%, band +-1.95% (OFF-pair sigma 0.72%): within noise, as predicted (null). GPU ms unchanged:
rt passes 8.46-8.59 OFF-a, 8.42-8.70 ON, 8.43-8.56 OFF-b; GI 11.27-11.40, 11.21-11.70, 11.11-11.38. 181 masked
records per frame. With the masks the grass moves from the vt to the tex candidates (shadow tex 2.1% -> 19.0%,
visibility 3.6% -> 35.4%). Disproved: the "coarser page" share did NOT drop (shadow 0.38% -> 0.43%, visibility
0.37% -> 0.38%): at camp distances the grass reads its clamped mip 2, which is resident; the coarser reads the
offline check found are at the mips 0..1 that close rays request, a small share. The masks' value is the
residency-independent, OMM-bakeable source, not a visible fix.

**11.2f Game-wide catalogue (`tools/assets/vt_alpha_catalogue.py`, all paks, 2026-11-02).** 3,295 Content files,
32,698 materials; 1,677 alpha-tested VT materials of 125 templates (template flags VT + alpha test); 1,174 unique
textures, all found in the 16 GTS. BC4 masks: 1,713 MB for levels 0..2, 1,305 MB for level 0 only (enough for
option B). Sizes: 1024^2 393, 2048^2 370, 512^2 181, 256^2 61, 4096^2 30. Top templates by texture count:
Base_AlphaTest_VT 364 (466 MB), CHAR_BASE_AlphaTest_VT 113 (228 MB), Base_AlphaTest_2s_VT 81 (128 MB),
CHAR_BASE_AlphaTest_VertCut_VT 78 (152 MB), CHAR_BASE_AlphaTest_2S_VT 64 (118 MB), Base_GM_Pulse_AlphaTest_VT 41,
IP_Wind_GrassFlowers_AlphaTest_2S_PBR_VT 38 (7 MB), ATM_Wind_Banners_AlphaTest_2S_ClothModel_VT 37,
ATM_Wind_Vertex_RGB_AlphaTest_2S_Translucency_VT 33. Masked so far: 65 textures (76 MB). Catalogue:
data/assetdb/vt_alpha/vt_alpha_catalogue.json (added). The whole set does not fit resident next to the game:
the runtime must load masks on demand (the textures of this frame's records, LRU under a budget), not all.

**11.2g Miss list (`rt_vt_alpha_miss_log`, live, off).** At every alpha commit, each VT record still read from
the tile cache and without an index entry adds its triangles to captures/vt_alpha_misses.tsv (per fragment
shader, tile set, p0: triangle-frames, frames seen, peak triangles in a frame, sessions, cut), ranked and merged
with earlier sessions at every [alpha] log window. `tools/assets/vt_alpha_masks.py --misses <tsv> [--top N]`
extracts exactly those. Candidate weights per record would need a per-record GPU counter (a shader change, so a
variant); triangle-frames are the CPU proxy.

**11.2h Masks generated on the user's machine (design, not implemented).** No Larian data is redistributed: the
release ships the tools, the user's own install is the source, the output lives in their data/assetdb.
- Inputs: the 16 GTS (1.3 GB, read from VirtualTextures.pak) and the GTP page files the wanted textures use. Camp
  measurement: 67 textures needed 80 GTP (3.7 GB read from the paks) and took about 11 min (about 10 s per
  texture, LSLib decode included). All 1,174 textures: a few hours in one process; by tile set in parallel
  (16 workers, I/O bound) an estimated 30-60 min. Recommended: at first install only the textures of the ranked
  miss list plus a shipped list of texture NAMES (names are not game data) for the common foliage; the rest in a
  background pass at low priority, resumable (per-texture output files, skip existing).
- Disk: output 1.3 GB (level 0) to 1.7 GB (levels 0..2) for everything; working set bounded by extracting the GTP
  of one tile set at a time and deleting it after its textures (peak a few GB; never inside the game folder).
- LSLib: v1.20.4 (MIT, Norbyte) needs the .NET 8 runtime; today driven through pwsh 7 (tools/assets/vt_alpha.ps1).
  For players: a small self-contained .NET console tool (LSLib + the extract loop, no PowerShell) shipped with its
  licence, or a native GTS/GTP reader (the page format and BC3 tiles; LSLib's VirtualTextures code as reference)
  to drop the .NET dependency. Integrity: masks keyed by the GTS GUID and the texture name, regenerated when a game
  patch changes the GTS (GUID check at startup, as the asset DB fingerprints its paks).

**11.2c Bake for VT foliage (not implemented yet).** Camp grass is 179 per-draw BLAS (wind VS, refit every
frame), 0 pool meshes: the static-variant path (bvhsplit native_omm_variant, clone TLAS, guard) does not
reach it. Needed: (1) a subdivision-level producer entry in omm_bake_plain.hlsl reading the immutable mask
mip 0 per micro-triangle (EXT barycentric order, uv box + 1-texel bilinear support, the cut from the record,
certificate: exact BC4 decode bounds), no content epochs (the mask never changes after upload); (2) the
per-draw BLAS build in bvhsplit attaching the triangle-opacity pNext for masked records, a micromap per
(mesh identity, UV hash, mask, cut) reused across frames and instances, rebuilt when the UVs change (wind
moves positions only, UVs must be checked, not assumed); refits keep the same micromap; (3) with B on the
known states agree with the test for every ray, so the micromaps can sit in the ordinary BLAS; the violation
check traces a reference TLAS whose instances carry VK_GEOMETRY_INSTANCE_DISABLE_OPACITY_MICROMAPS_EXT;
(4) omm_qual windows B-on/OMM-off vs B-on/OMM-on in one launch via a live key that switches the instance
flag (no rebuild). Estimate about 2 days.

**11.3 Order.** (1) Decide A (exact, small share) vs B (image change, full
share). (2) Offline VT masks with the RT test switched to them, gated alone
(energy, `coarser` counter to 0), useful without OMM. (3) OMM on top, measured
with omm_qual (coverage, violations 0, ON vs REF).
