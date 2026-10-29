# Native direct-light validation control - startup only

Debug4 forces AO0 and removes the sun, but native AO-on-direct coefficient1
suppresses direct terms too. Debug6 AO1 produces positive light but retains
ambient/in-scatter. Neither isolates native direct lighting for box accuracy.

Default-off ls_native_direct_validate1 is a startup-only DIAGNOSTIC, requiring
debug4/5 and ls_check0/ls_impl1. A recognized native compute module's READS of
set1/b0 m0.x/y/w are replaced by constants1/0/1: use the AO texture, leave direct
light unattenuated, and use the sun mask. The recognized native binding7 float2D fetch is
replaced with a typed null vector, so solar0 does not depend on a writable
R8 view. Prep writes AO0; no shared solar image needs changing. Uniform
buffers, descriptors, other fields and native BRDF/cookie/light code are untouched.
In-scatter/atmospheric transmittance is NOT separately removed; inspect actual
residuals rather than claiming pure accuracy from a request alone.

Transformation requires a compute entry point, native output/light bindings,
32-bit float4 flags at offset0 and uint2 resolution at offset16. Only proven
scalar field reads are supported. Whole/vector/dynamic/escaping pointer cases
refuse the complete transform; no partial patch or guessed layout. Other shaders
remain original. The actual successful module variant is propagated through
module -> pipeline -> command state -> Bg3DispatchInfo. Paired host/core ABI9.
Core refuses missing variants, unsupported views/reference modes
and live startup-key mismatches. Inspect the WHOLE log for every REFUSED line;
no mixture of transformed native output and ordinary recompute is acceptable.

Separate owned full/full-box validation shaders force direct factors1 only with
the matched dispatch flag. All six ordinary/default/FULL/check/box shader binaries
remain byte-identical to56bcc06. No native-buffer mutation, camera transport,
extra image or GPU lifetime model is introduced. Driver fallback to the original
module clears its validation flag; this invalidates the comparison, never passes.
Original and transformed shader hashes are logged/dumped and Aftermath sees the
actual submitted variant. Default/release key0; never use as a production AO fix.

Offline: semantic field-preservation/foreign-interface/unsupported/malformed
input tests run with assertions enabled even in release.62 captured native modules
transform exactly3 reads/components11 and each passes spirv-val. Full build/4
CTests and both owned shader compiles pass. Runtime still needs actual applied
modules, positive inputs, no refusals, full layout/push/write VVL audit and native
error numbers. No quality/merge/default approval follows from offline checks.

288 scene comparison INVALID despite queue expectations: native modules are
applied, but the writable R8 solar-mask view is absent in S6. Core refuses
scene frames; early positive windows belong to menu/preload. No native accuracy
follows. Next transform additionally requires EXACTLY ONE native set1/b7
float2D fetch and replaces its result with a typed zero, validated on all62
original modules. SPIR-V Depth0/2 float2D fetches are recognized; arrayed/MS/
depth-only or sampled-image paths refuse rather than guessing. No native image
or buffer is overwritten to force a pass. Corpus hashes are retained.
Fixture now uses a session with -Set in startup args AND the same live set:
modules are transformed before creation, expects/statistics use the post-ready
scene slice. Full startup log/VVL still needs separate audit. This prevents menu
lighting from masquerading as positive scene-reference data.

289 on 8fa0c0c has 62 controlled modules, NO full-log refusals and nine loaded
scene windows (2,932,082,331 projector receivers). Native mean Y0.0292808848,
recompute0.0294563717 (+0.599322%), absolute error0.0001767432, invalid0.
Queue verdict remains INVALID because its one-time box activation expectation
was emitted before the scene slice. The replacement expectation checks recurring
scene stats' actual box-active flag; no old result is relabelled as a pass.
Final output is finite; native D32F dump d130 has2604 nonfinite values retained.
Full layout/push/write categories absent; other VVL categories remain unresolved
(see evidence/p3-native-direct-scene-289.json). No native-accuracy approval.

### Format-derived diagnostic (still default-off)

Native diffuse/specular bindings are queried as B10G11R11_UFLOAT_PACK32 before
the snapshot is accepted. The comparison previously subtracted full-precision
recompute from those reduced-precision stored values. The +0.60% mostly-positive
residual motivates checking storage rounding; it does NOT establish its cause.
[Vulkan Floating-Point Format Conversions](https://registry.khronos.org/vulkan/specs/latest/html/vkspec.html#fundamentals-fp-conversion)
allows either adjacent representable finite value. Thus each loaded native RGB
component defines an OPEN interval between its representable neighbours. Zero
includes0, excludes the smallest positive codeword. Finite maxima have no finite
upper bound and are reported INCONCLUSIVE, never passed. Negative/nonfinite
estimates and nonrepresentable native values are separately INVALID.

common/ufloat_interval.h is shared by the diagnostic HLSL and an exhaustive CPU
test against an independent mathematical decoder (both finite codebooks,
subnormals, powers of two, immediate boundary neighbours, nonfinite and maxima).
Only dedicated native-validation shaders collect counters165..172: all shaded
receivers, invalid/unbounded/failing pixels, bounded/violating channels and Q24
mean excess outside these intervals. Six channels are checked separately, before
owned output conversion; no diffuse/specular cancellation or tuned percentage.
No allowance is made for BRDF, atmosphere, material or cookie error. A sample
inside the interval is compatible with storage rounding, not proof of exact math.
Q24 half-step2^-25 describes statistic quantization, NOT a quality tolerance.
All six ordinary shader variants remain byte-identical to56bcc06.
