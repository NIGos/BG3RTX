# Native Fur_IDmsk: bounded source checkpoint

This is an offline source/capture/scalar increment. No runtime alpha dispatcher,
geometry hook, ABI, setting, GUI, game launch or GPU work changed. Fur remains
unsupported at runtime until the remaining input and driver gates pass.

## Actual observed shaders

| Pass | Fragment shader | Vertex shader | Retained draw calls |
|---|---|---|---:|
| StaticDepth | `c22b20253e25e473` | `0d6885be08b2faa4` | 2 |
| StaticDepthShadow | `1e144b6e40e48558` | `ff2a169c3d58bac8` | 14 |

The pairs occur in 161807 (4 Shadow), 161845 (1 depth + 8 Shadow) and 162008
(1 depth + 2 Shadow). The family inventory's 20 calls also includes two deferred
and two velocity calls. These are draw-call counts, including repeated cascades;
they do not measure unique meshes, triangle counts or screen impact. Other
retained 20261030 F10 files contain no draw matching these two exact FS hashes.
No save files were read; Test1 is only the user's scene reference.

Both pairs were freshly recovered from the effective read-only `Materials.pak`:
`Public/Shared/Assets/Materials/Characters/CHAR_Fur_IDmsk_ST_DEP_Vulkan.bshd`
and `_ST_DEPS_Vulkan.bshd`. All four layer FNV hashes match. Fresh Vulkan-semantics
SPIRV-Cross output equals the existing cached GLSL after newline normalization.
Module SHA-256, source paths and per-capture counts are in the companion JSON.

## Exact source inputs and equations

Both VS output UV at location 0, N at 1, T at 2, B at 3 and original post-WPO W at
4. N/T/B are normalized by the VS but must not be assumed orthogonal after
perspective interpolation. WPO samples the VS wind texture at set 1 binding 2,
uses vertex color X and material Soupleness, and writes the deformed W. The
fragment mask is a different texture at set 1 binding 3, with sampler 0/7. Reusing
rest-pose or stale cached geometry is not source equivalence: later integration
needs current post-VS W/N/T/B, using the existing 88-byte strand payload shape.

Neither fragment shader writes FragDepth. The VS calculates row-vector
`float4(W,1) * CameraVP` and negates clip Y. Fur therefore needs no Hair virtual
height/depth replacement. Its depth is the captured deformed triangle plane.
The native fragment view is `normalize(-interpolatedW)`. For a physical RT
material policy, substituting the ray view is a separate explicitly chosen
policy; it is not proof of replaying the native raster camera expression.

The material UBO is set 1 binding 1: 16 bytes for StaticDepth and 32 for Shadow.
The packaged material metadata maps these byte offsets:

| Offset | Native parameter | Catalog default (not a captured live value) |
|---:|---|---:|
| 0 | Soupleness, already applied by VS WPO | 0 |
| 4 | DitherSoftenMaximum | 1 |
| 8 | DitherSoftenMinimum | 0.6 |
| 12 | OpacityFresnelPower | 3 |
| 16 | CastShadow, Shadow only | 1 |

Let `M = [B N T]`, `v = normalize(-W)` and
`f = abs(dot(normalize(M * normalize(v * M)), normalize(N)))`.
The shared source coverage is
`A * smoothstep(m1,m2,mask.B) * saturate((1 - pow(max(abs(1-f),1e-4),m3))*5)`.
The helper retains both TBN transforms, the exponent, mask B and mask A; it never
substitutes the old raw-alpha cutoff or the simpler Hair angular factor.

Shadow survives when
`fma(step(0.33329999446868896484375,coverage),CastShadow,-0.3333300054073333740234375) >= 0`.
Camera survival instead uses the native Frame words 2 and 3, the eight-entry
phase sequence, UV.x, original integer FragCoord X/Y and the two source FMA
operations. Equality at the step edge survives. Original CameraVP and viewport
are required to recover those raster coordinates; a ray-hit screen guess is
insufficient. `common/native_fur.h` captures the actual Frame and CameraVP bytes,
without filling missing values from defaults or a frame_index guess.

## Explicit unsupported cases and next integration step

The native instruction literally uses `smoothstep(maximum,minimum,B)`. The
catalog defaults are descending. Actual F10 material UBO values were not retained,
and reversed/equal SmoothStep behavior has not been compared against the native
driver. The helper returns `SmoothStepOrder` UNKNOWN for those cases. It does
not reorder the edges or declare a polynomial inversion equivalent. This is a
real remaining gate, rather than a completed Fur fix. Ordered finite edges have
a faithful scalar implementation; equal/descending edges, invalid frames,
nonfinite inputs and missing/mismatched interfaces fail closed.

Before runtime integration, prove actual mask view/sampler/read validity and
capture current post-VS attributes plus original raster coordinates. Reuse the
exact two-pair lookup and bounded packet helper; do not enable other Fur, STI,
Tetrahedron, deferred or velocity variants by name. Root owns the alpha and BVH
integration. No new kind/key or ABI was introduced here. Missing coverage must
remain UNKNOWN/native fallback, never opaque or inferred clear.

CPU validation compiles only the new test with MSVC `/O2 /fp:precise` and registers
it in CTest. It checks exact bounded reads, mismatched recipes, stale-output
clearing, Frame range, mask channels, CastShadow, threshold equality, TBN sign
invariance, explicit unsupported edges and an independent long-double oracle on
10,000 non-orthogonal frames. Numeric regression bounds are derived from float32
operation error; they are not runtime eligibility thresholds or GPU proof.
No ordinary shader changed, and no repeated full renderer build was run.

Result: 60,048 CPU checks pass. No GPU, runtime coverage or visual gate is claimed.
