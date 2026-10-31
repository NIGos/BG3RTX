# OMM GPU content epochs: bounded source contract

## Coherent write protocol - 2026-10-30

An AFTER-only global/image increment can certify torn state across queues:
validator observes new global with old image, then image changes without another
global change. Reordering those two increments alone also leaves the native write
unmarked before the epoch dispatch. Existing hooks now bracket each eligible
copy/blit/resolve/clear with a GPU active-writer increment BEFORE the native
command and image epoch, global epoch, memory fence, active decrement AFTER it.
The header is 16 bytes: global 0, sticky-disabled 4, active-writers 8; every image
counter starts at an alignment-derived stride >= 16. Begin/end use specialization
constant 0 on the same shader module; native push constants remain untouched.

Untracked/unsupported queue, push-descriptor restore or allocation paths record
sticky disable before the native command. Missing completion leaves active>0;
unpaired completion and active-counter saturation disable permanently without
wrapping. The ordinary fresh rt_omm=0 path still performs no counter GPU work.

Readers must reject active writers and perform ordered before/after epoch checks.
This counter protocol does NOT synchronize native image reads against writes or
make concurrent texture accesses legal. Native producer/consumer semaphore and
pipeline dependencies must cover the injected RT/bake reads, including subsequent
uploads after G-buffer shading. Root owns those reader/submit integration gates;
the writer-only GPU fixture cannot certify them. Two tiny dispatches replace one
per supported native write; GPU cost remains unmeasured.

Actual GPU evidence: docs/evidence/omm-write-protocol-20261030.txt. Existing
windowless fixture exercises beginning, overlapping writers, partial completion,
coherent final completion, replay, saturation and pre-write sticky disable.

`rt_omm=0` in a fresh process allocates nothing and adds no GPU calls. Successful
OMM device initialization latches content instrumentation until device teardown,
including after INI off, because held BLASes may still reference micromaps.
The counter storage is host-coherent and zeroed before its first GPU use. CPU
code never resets live epochs/disable. It is concurrent across queue families.

`ContentSource` supplies the counter buffer, byte extent, per-image byte offset,
process-unique 64-bit image ID and never-recycled slot. Header words are global
epoch at byte 0, sticky disabled at byte 4 and active writers at byte 8. A zero per-image epoch is not
initialized content. Consumers must use atomic GPU reads (e.g. InterlockedOr 0)
when unrelated upload queues can write; a blind transfer copy/plain shader Load
is not a concurrent atomic snapshot. Global and per-image counters saturate and
set disable on reaching UINT32_MAX; they never wrap. Disabled always requires
the reference TLAS, irrespective of an otherwise matching scene epoch.

Eligibility is deliberately narrow: 2D, one layer/sample, optimal tiling, empty
create pNext/flags, R8/RG8/RGBA8/BGRA8 UNORM, and usage limited to sampled/transfer.
Storage, attachments, host transfer, mutable/alias/sparse/external images and
unknown extensions decline. BC UNORM/sRGB formats have only the explicit
uniform-endpoint policy described below; general code8 proof does not apply. Allocation/binding tracking requires known
non-host-visible backing without imported/unknown allocation extensions. Unknown
bind pNext declines. Free/recreate invalidates CPU source lookup; image IDs/slots
are not reused. Devices enabling raw device-address commands or dedicated image
allocation aliasing decline content proof rather than assuming those writes absent.
These restrictions exclude data-consistent aliases. Native valid Vulkan contents
and independent source/view/descriptor lifetime identity remain required.

Ordinary CopyBufferToImage, CopyImage, Blit, Clear and Resolve operations and their
core/KHR 2 variants bracket writes with one-thread GPU begin/end dispatches.
A replay executes both phases again. Queue family comes from the actual command pool recording graph.
The kernel has no push constants. It binds immutable ordinary descriptor sets
with an alignment-derived image-counter slice and restores native compute bindings.
Transfer-only, unknown recordings, active native push descriptors or failed set
allocation use GPU CmdFillBuffer sticky disable BEFORE the native write. NV/KHR indirect image copies also
take this unsupported path. No substitute content certificate is manufactured.

**Required integration:** `content_dispatch_in_submit(spanCBs)` covers direct
updates, sticky fills, secondary generation snapshots and actual OMM ticket readers.
Submit2 signal stage masks for affected entries must become ALL_COMMANDS without
changing semaphore values or other fields; legacy submit already covers this.
Root/Solar own that hook. Bake sampling commands must call mark_content_dispatch
before sampling, including later record-failure/orphan cases. Full before/after
epoch consistency and safe native image read/write ordering, GPU stamp comparison,
source generation identity and reference-TLAS fallback are root provider duties.
Do not enable production source certification before these are wired.

Teardown order: retire/destroy backend resources after proven completion, release
baker/orphan inputs after its device-idle guard, then content storage. The content
component waits idle before destroying recorded counter resources. It exposes no
host ABI change; coordinated ABI 19 remains separate.

Offline validation: host DLL builds with two workers; targeted CTests 3/3 pass:
87 backend checks, 111 submission checks, 83 registry/view/policy checks. Shader DXC/spirv-val
pass. Actual RTX 5090 synthetic GPU test, explicit Khronos validation, zero errors:
first write global/image 1, same-CB replay 2, UINT32_MAX saturation + disable 1,
subsequent update no wrap, unsupported sticky-fill disable 1, later supported update
keeps disable 1. The unsupported branch ran on a compute queue; a dedicated
transfer-only queue was not tested. Requested counter storage is 1,048,592 bytes
(65,536 slots, alignment stride 16), excluding driver descriptor/pipeline overhead.
GPU time is not measured. No game/window/presentation/input/config deployment.
Evidence: docs/evidence/omm-content-epochs-20261030.txt.

## Sampled view identity follow-up

Native asset proof must use `content_source_view`, not image-only `content_source`.
View creation/destruction hooks retain a never-reused 64-bit view ID plus the exact
parent image generation. Accepted views are 2D, flags 0, empty pNext, color aspect,
single layer 0, exact plain UNORM image format and an in-range nonempty mip span.
VK_REMAINING_MIP_LEVELS/ARRAY_LAYERS are resolved against the stored image metadata.
Unknown pNext (including VkImageViewMinLodEXT), flags/types/formats/ranges decline.
A destroyed/reused image or view handle cannot inherit its predecessor's proof.
Base mip, resolved level count, format and component swizzle are returned in
ContentSource. Swizzles are retained because bake and native alpha must bind the
SAME view; these fields and view ID belong in the root bake identity.

Targeted CTests remain 3/3; registry/view CPU checks increased from 17 to 77.
Actual GPU fixture created/destroyed/recreated a sampled view: view IDs 1 then 2,
resolved base 0, one level, format 37, same image ID. Source lookup after destruction
fails. The ray, endpoint and epoch replay/saturation gates still pass, with zero
Khronos validation errors. Evidence: docs/evidence/omm-content-view-20261030.txt.

## BC and sRGB: explicit uniform-endpoint policy

ContentSource.requiredSamplingCertificate is metadata, not a granted certificate:
1 means the general plain-UNORM8 interval contract; 2 means ONLY uniform decoded
0/1 endpoints. BC1 RGB/RGBA, BC2, BC3, BC4, BC5 and BC7 UNORM plus their available
sRGB counterparts require policy 2. Plain R8/RG8/RGBA8/BGRA8 sRGB also requires 2.
SNORM, BC6 floating formats and unknown formats still decline. Root must enforce
this metadata and supply the separately established sampling certificate. Mixed
values cannot inherit a general UNORM8 code/rounding certificate. All mip/view/
content epoch guards remain mandatory. For stochastic native >= threshold [0,1],
uniform zero remains UNKNOWN (hash zero is reachable); uniform one can be Opaque.

The actual RTX 5090 fixture checks BC3 UNORM/SRGB (formats 137/138) and BC7
UNORM/SRGB (145/146), alpha 0 and 1, all three mips, LINEAR min/mag and mip
filtering, 64 UVs and 17 fractional LODs. Each of eight fixtures has 1,088 sample
points: 8,704 total. Both Texture.Load decoded alpha and filtered SampleLevel
return exact endpoint float bits, zero failures. BC7 mode-6 block construction is
verified by the actual decoded loads, not accepted from a guessed bit layout.
All eight sampled-view sources report required policy 2. Explicit Khronos
validation errors remain zero. BC1/2/4/5 were not separately hardware-tested here;
their registry acceptance is conditional on the same explicit endpoint policy,
not a claim that arbitrary decoded values or filtering errors were certified.

[sRGB conversion in Vulkan](https://docs.vulkan.org/spec/latest/chapters/images.html)
converts RGB after UNORM decoding; alpha remains linear. This does not grant a
mixed-value RGB precision bound. The [weighted-average filtering contract](https://docs.vulkan.org/spec/latest/chapters/textures.html#textures-texel-filtering)
preserves constants algebraically, subject to the numerical caveats already
recorded in OMM_EXT_BACKEND.md. This finite fixture is not an all-content or
all-driver certificate and does not itself bake/attach BC-derived micromaps.

Run the existing optional console with a fourth shader path compiled from entry
bc_endpoints. Evidence: docs/evidence/omm-bc-endpoints-20261030.txt. Host DLL and
3/3 targeted CPU tests pass; registry/view/policy checks are now 83. No game,
window, presentation, runtime configuration change or deployment was performed.

## Native sampler metadata

Host-internal sampler tracking reuses existing descriptor writes/templates/copies;
public ABI and ordinary shaders are unchanged. Fresh rt_omm=0 records no sampler
metadata and emits no added GPU calls. Created samplers receive never-reused IDs;
raw handle reuse cannot change an older descriptor's sampler generation. Snapshot
fields include filter/mipmap/address modes, LOD min/max/bias, anisotropy, compare,
normalization, border color and flags. Weighted-average/min/max reduction state
is retained; unknown/custom-border/Ycbcr chains decline with their structure type.

BoundSampler returns sampler ID/state, descriptor-set/layout generations, write
version, binding flags and immutable status. Immutable samplers are seeded from
actual layout definitions and survive layout-handle reuse through copied set
metadata. Unknown/push/cross-binding rollover/unsupported allocation paths decline.
Mutable state is CURRENT CPU metadata, not a frozen GPU descriptor certificate.
The initial factory must require immutable_sampler_current and a standard frozen
image binding, rather than treating a lifetime reference as freezing updates.
No production factory was introduced by this sampler increment. Host DLL build
and two CPU tests pass: 32 create-state and 42 binding/generation checks, no device calls.


## Native plain material factory

`capture_native_plain` now captures the actual native draw pipeline, exact audited
FS/VS recipe, canonical AlphaSnapshot, view/content identities and immutable
sampler metadata. It requires a normal frozen sampled-image binding and exact
binding/view agreement. Unknown recipes, WPO recipe 2, UV/material changes,
unsupported sampler state, mutable samplers and stale generations decline.

`make_native_plain_input` requires the SAME native command recording and an
explicit owner for the caller-verified post-VS triangle-list vertex range
(float4 position followed by float2 UV at byte 16). It rechecks source/view and
sampler identities, then selects certificate 2, every view mip and stochastic
thresholds [0,1]. All-zero regions remain UNKNOWN. It does not invent a continuous
numeric certificate for mixed values or freeze image bytes: production GPU epochs,
scene validation and baseline fallback remain mandatory. Existing lifetime notes
retain sampled handles through the same command submission.

The helper has no renderer call site yet; the static BLAS variant integration
must call capture at every native draw, including cached static draws. Forty CPU
checks exercise the actual helper, including stale recordings/view identities,
changed recipes/samplers, range failures and strict WPO rejection. The isolated
host DLL and sampler tests also build/pass. No game or GPU work was run for this
increment; ordinary shader sources and public ABI are unchanged.
