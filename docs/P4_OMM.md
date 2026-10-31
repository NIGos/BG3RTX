# P4 opacity micromaps (OMM)

This is the one OMM document. It merges the former OMM_CONTENT_EPOCHS, OMM_EXT_BACKEND
and OMM_GPU_PRODUCER notes (originals in [archive/](archive/)).

Contents: 1 status, 2 native alpha predicate, 3 CPU groundwork, 4 EXT backend,
5 submission and lifetime, 6 GPU producer, 7 content epochs and source identity,
8 hardware fixtures, 9 remaining work, 10 user smoke test.

## 1. Status: isolated runtime candidate

Default rt_omm=0, Experimental GUI off in all presets. ABI remains 19; both DLLs
must travel together. Main/user build is unchanged. No current game/GPU validation
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

OPEN: actual renderer descriptor/GPU traversal validation; nonzero useful known
coverage; same-ray reference violation counts; energy/movement/FG2 quality;
capture/build/traversal GPU ms and real residency. All-UNKNOWN or missing RT never
passes. Current code is an unqualified opt-in candidate, not a shipped speedup.
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
