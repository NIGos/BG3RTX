# Native-hit direct-only reference — 2026-10-31

Offline implementation following provider `177ef6e`. No game/GPU call or live
candidate change. The provider's `NativeHitMaterial` and `rt_hit_material`
interfaces remain unchanged; the public ABI stays 20.

`rt_hit_material=1` at startup plus `rf_debug=7` displays a **primary-hit
point/spot direct-only reference** on qualified pixels. This is not a reflected
hit-distance view, a complete hit-radiance solution, or normal RF promotion.
Normal RF retains its existing probe replacement. Unknown reference pixels
retain the existing renderer output, so the view is explicitly a mixed
qualified-reference/native-fallback image rather than whole-image ground truth.
No quality/energy equivalence or GPU-cost claim is made.

Reusable functions:

- `hd_native_incident`: CPU/HLSL authored native point/spot amplitude, range,
  falloff, cone, global scale and channel selection. The formulas are the
  existing audited `bg3_lights.hlsli::bg3_light_eval` / native `f469db78` source,
  factored before cosine and BRDF. The established native renderer helper stays
  untouched for exact ordinary shader identity. No SI photometric calibration
  is asserted for the game's authored amplitude/falloff parameters.
- `hd_scatter`: shared `TfFrame` plus the existing Nt-to-FresnelBlend adapter,
  then `f * incoming amplitude * cosine * visibility`, once each. No material
  lobe weights, ambient constants, energy clamp or duplicate BSDF is introduced.
- `native_hit_direct`: reusable HLSL sum using the actual current native froxel
  point/spot list at a supplied hit position/outgoing direction. This can feed
  a later cache-free reference consumer. It is not restricted to the display's
  primary outgoing direction.

The supported light-list domain is an in-frustum hit within the actual native
froxel slice range, valid buffer ranges and the existing 1024-light bound.
Projector-bearing receiver lists explicitly return unsupported. Sun,
environment/probes, emission, AO multipliers and indirect bounces are omitted
by this declared point/spot contract; their absence must never justify replacing
full probe radiance. Every positive point/spot term gets binary RT visibility, including sources
without a native shadow atlas. Native flag bit0 indicates atlas presence and does
not disable this physical visibility test. UNKNOWN visibility rejects the result.
No atlas fade or artistic shadow-strength multiplier is copied.
Standard shared ray offsets move the geometric surface origin; no arbitrary
ray-distance selector or ambient floor is added.

Non-finite/negative authored amplitudes, coincident sources and the native
sub-1e-10 squared-distance regularization domain are unsupported. Invalid
material/frame/visibility inputs return explicit status. Shading/geometric
hemisphere disagreements are unsupported; shading-normal correction and
transmission remain unimplemented. The native material cone-versus-raster
sampling limits of the frozen provider still apply.

The separate consumer uses the existing LS layout with **main AlphaDSL** only.
After actual OMM scene selection, it calls ABI 20 `rt_hit_material_use` on the
selected tuple before dispatch. It binds no private Sun descriptor set and
uses the existing actual native local-light bindings. Optional dispatch failure
continues the established post passes. No provider resource/layout/lifetime
change or new key is required.

Validation: matching core build succeeds; four targeted CTests pass, including
877 new independent double-precision attenuation/cone/composition checks plus
the existing independent BSDF/frame/native-parameter suites. All 16 new variants
compile and pass Vulkan 1.3 `spirv-val`. Actual recompiles of five representative
existing modules (normal RF, Sun, GI, frozen material acquisition, and deep
OMM/scalp/cascade/rebase RF) are byte-identical. All pre-existing shader/common
inputs are unchanged from `177ef6e`; the prior full 539-entry identity evidence
is retained rather than rerun. Exact hashes are in the adjacent JSON.

GPU milliseconds, runtime participation and image quality are unknown offline.
The coordinator's frozen provider acquisition smoke is separate from this
reference increment. Next: reuse this declared evaluator as a boundary/direct
term in the shared cache-free reference; supply actual environment/emission
and additional supported light domains before any full-radiance RF promotion.
