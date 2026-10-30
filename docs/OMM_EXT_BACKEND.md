# EXT opacity micromap backend contract

Offline increment, 2026-10-30, based on main 92183e7. No game, Vulkan device,
GPU job or deployment was performed. `rt_omm=0` remains the default. Restart is
required to request the extension. Existing shaders and the host ABI are unchanged.

The layer requests **VK_EXT_opacity_micromap**, its `micromap` feature and core
Vulkan 1.3 synchronization2. Vulkan 1.2 still supports ordinary RT, but this first
backend declines OMM there. Explicitly disabled application features are respected.
Private EXT function pointers avoid changing the shared Vulkan dispatch table.
Core/Vulkan12 bufferDeviceAddress must also be enabled for address-flag allocations;
the diagnostic EXT-only BDA configuration is declined for OMM.
The KHR micromap-object backend is not used; recent headers alias some wire types,
but object creation, build, destruction and triangle attachment all use EXT APIs.

`layer/omm_backend.h` accepts immutable GPU packed state bytes plus a GPU triangle
descriptor buffer. Both addresses require 256-byte alignment and the MICROMAP
BUILD INPUT READ ONLY + SHADER DEVICE ADDRESS usage flags. The CPU descriptor span
must be the exact bytes uploaded to the triangle buffer. Validation checks every
data offset and extent, four-state format, subdivision/property bounds, histogram
counts and address arithmetic. It does not prove that a supplied known state is
true. Known-state semantic proof belongs to the provider, including >= equality,
stochastic thresholds, filtering/LOD and resource generations. UNKNOWN must still
execute native alpha. Do not use force-two-state or force-opaque instance flags.

`record` allocates separate micromap storage and aligned scratch, queries EXT build
sizes, creates the EXT object and records producer/build/BLAS dependency barriers.
It returns a ticket. `attachment` supplies a matching triangle-opacity pNext with
index NONE, base triangle 0 and exact primitive count, with ticket-owned histogram
storage. This first interface supports one-to-one triangle order, not indexed
remapping. The caller must preserve unrelated pNext structures and mark every
submission that uses a BLAS referencing the micromap. Ordinary AS-build-to-query
barriers remain the BLAS owner's responsibility. Inputs must stay valid through
micromap build completion; noncoherent host inputs need flushing before submission.
All GPU recording/submission and completion serials must refer to the same compute
capable graphics queue; cross-queue ownership/semaphore handling is not implemented.

Lifetime protocol: `mark_submitted(ticket, serial)` for the build and all subsequent
BLAS uses; `retire(ticket)` only after no scene can schedule future uses;
`collect(completedSerial)` destroys only retired resources whose last use completed.
Serials are actual queue submission/completion values, never frame indices.
`discard_unsubmitted` is permitted only after the recorded command buffer is reset
or discarded. Device teardown waits idle before freeing any remaining objects.
The storage/scratch allocation budget is bounded to 256 MiB of requested buffer
bytes. This is not a measured device-memory footprint or performance result.

Offline validation: isolated host DLL build succeeded with two compiler workers.
CPU CTest passes 68 plan/range/layout/lifetime checks. Settings defaults agree.
No shader files differ from the baseline. No Vulkan command was executed, and
neither GPU cost nor traversal correctness has been measured. This component has
no production caller in this commit; provider/BLAS integration and a live same-ray
known-state gate are still required. All-unknown plumbing is not completed OMM.

Primary local specification: Vulkan SDK 1.4.357.0 `Include/vulkan/vulkan_core.h`,
`share/vulkan/registry/vk.xml`, and `share/vulkan/registry/validusage.json`.
Relevant rules include `vkCmdBuildMicromapsEXT` VUIDs 07509-07515 (scratch,
input usage and address alignment), `VkMicromapCreateInfoEXT` 07437/07438
(buffer extent/256-byte offset), and triangle-opacity 10719/10722/10723
(index type/stride). Registry extension dependencies require AS plus sync2 or
Vulkan 1.3; registry synchronization tables support the recorded micromap stages.
