# Native direct-light validation control - startup only

Debug4 forces AO0 and removes the sun, but native AO-on-direct coefficient1
suppresses direct terms too. Debug6 AO1 produces positive light but retains
ambient/in-scatter. Neither isolates native direct lighting for box accuracy.

Default-off ls_native_direct_validate1 is a startup-only DIAGNOSTIC, requiring
debug4/5 and ls_check0/ls_impl1. A recognized native compute module's READS of
set1/b0 m0.x/y/w are replaced by constants1/0/1: use the AO texture, leave direct
light unattenuated, and use the sun mask. Prep still writes AO0/sun0. Uniform
buffers, descriptors, other fields and native BRDF/cookie/light code are untouched.
In-scatter/atmospheric transmittance is NOT separately removed; inspect actual
residuals rather than claiming pure accuracy from a request alone.

Transformation requires a compute entry point, native output/light bindings,
32-bit float4 flags at offset0 and uint2 resolution at offset16. Only proven
scalar field reads are supported. Whole/vector/dynamic/escaping pointer cases
refuse the complete transform; no partial patch or guessed layout. Other shaders
remain original. The actual successful module variant is propagated through
module -> pipeline -> command state -> Bg3DispatchInfo. Paired host/core ABI9.
Core refuses missing variants, unsupported views/reference modes, no sun mask,
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
