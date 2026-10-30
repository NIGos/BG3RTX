# OMM GPU content epochs: bounded source contract

`rt_omm=0` in a fresh process allocates nothing and adds no GPU calls. Successful
OMM device initialization latches content instrumentation until device teardown,
including after INI off, because held BLASes may still reference micromaps.
The counter storage is host-coherent and zeroed before its first GPU use. CPU
code never resets live epochs/disable. It is concurrent across queue families.

`ContentSource` supplies the counter buffer, byte extent, per-image byte offset,
process-unique 64-bit image ID and never-recycled slot. Header words are global
epoch at byte 0 and sticky disabled at byte 4. A zero per-image epoch is not
initialized content. Consumers must use atomic GPU reads (e.g. InterlockedOr 0)
when unrelated upload queues can write; a blind transfer copy/plain shader Load
is not a concurrent atomic snapshot. Global and per-image counters saturate and
set disable on reaching UINT32_MAX; they never wrap. Disabled always requires
the reference TLAS, irrespective of an otherwise matching scene epoch.

Eligibility is deliberately narrow: 2D, one layer/sample, optimal tiling, empty
create pNext/flags, R8/RG8/RGBA8/BGRA8 UNORM, and usage limited to sampled/transfer.
Storage, attachments, host transfer, compressed/sRGB, mutable/alias/sparse/external
images and unknown extensions decline. Allocation/binding tracking requires known
non-host-visible backing without imported/unknown allocation extensions. Unknown
bind pNext declines. Free/recreate invalidates CPU source lookup; image IDs/slots
are not reused. Devices enabling raw device-address commands or dedicated image
allocation aliasing decline content proof rather than assuming those writes absent.
These restrictions exclude data-consistent aliases. Native valid Vulkan contents
and independent source/view/descriptor lifetime identity remain required.

Ordinary CopyBufferToImage, CopyImage, Blit, Clear and Resolve operations and their
core/KHR 2 variants append a one-thread GPU epoch dispatch. A replay executes the
increment again. Queue family comes from the actual command pool recording graph.
The kernel has no push constants. It binds immutable ordinary descriptor sets
with an alignment-derived image-counter slice and restores native compute bindings.
Transfer-only, unknown recordings, active native push descriptors or failed set
allocation use GPU CmdFillBuffer sticky disable. NV/KHR indirect image copies also
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
87 backend checks, 111 submission checks, 77 registry/view checks. Shader DXC/spirv-val
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
