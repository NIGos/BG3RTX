# Native hair runtime coverage and depth

## Status: coverage candidate, full Hair remains open

`rt_native_hair=0` is default. Enabling requires a restart with `rt_alpha=1` and
`rt_bvh_split=1`. The latter requirement is enforced: legacy startup rejects this
mode, logs the reason and never mixes 24/88-byte geometry. Hot-enabling after a
24-byte startup is also explicitly rejected. No live build was deployed.

The current increment implements actual native CHAR_Hair coverage, not an alpha-A
surrogate. It still intersects base-card triangles. Native fragment depth/origin
equivalence remains OPEN; do not call this a complete hair or face-shadow fix.

## Native source and pass contract

Materials.pak native SHD8 SPIR-V was identified using exact layer FNV hashes.
The catalog has all 15 CHAR_Hair variants; all 15 VS and 15 FS bodies were recovered
offline. Source provenance: captures/native-hair-20261030/native-module-provenance.json.
Depth and shadow shader source recipes implemented here:

| Native pass | Static FS / VS | Instanced FS / VS |
|---|---|---|
| Depth | 5a03e5b678b2d040 / bf72b58803efe34d | 0527bb148e1d6c03 / 00e2829fe1dd1a70 |
| DepthShadow | 34951669ed4b3b0f / c39780dc8824981e | 8dddc0bc49e56efc / fb74490e1a527950 |
| DepthShadowTetrahedron | 00a9ecbb9c2b4ff8 / f99b997be5725de7 | a39086306bfc9c03 / 304b4f616f0c2f7b |

Static material/noise/mask descriptors are set 1 bindings 1/3/4. Instanced uses
0/2/3. Sampler is native set 0 binding 7, UV location 0. Depth reads 48 material
bytes; Shadow reads 32, with root/density at offsets 16/20 rather than Depth's
32/36. Captured records canonicalize these into material indices 8/9. Noise scale
and shift are material offsets 8/12. Shader queries use existing linear repeat,
trilinear isotropic RT-cone footprints, max sampler LOD 16. Original native raster
anisotropic sampling and precision identity are NOT claimed.

Deferred, Forward, Emissive, Velocity and Bake passes have separate bindings and
layouts. They were inventoried, not falsely entered as the Depth recipe. All six
supported VS provide the exact required strand outputs. Some other native passes
do not; their missing layouts are not labeled as Hair support.

## Capture and query implementation

Startup opt-in stream stride 88: clip/world position at 0, UV at 16, world N/T/B
at 24/36/48, native post-wind camera-relative position at 60, fade vec4 at 72.
Only smooth float32 location 1/2/3/4 vec3 and optional location 5 vec4 are advertised.
Camera Depth requires fade; native Shadow does not. Wrong/missing interfaces are
logged and counted. Hair remains dynamic: world strand frames cannot enter the
ordinary object-space static cache. Held draw snapshots preserve both 2D views
and their image identities, material, attributes and original camera metadata.

AlphaRec remains 208 bytes; kind 4 uses texture/cache indices for mask/noise 2D
views, canonical material48B at byte48, original VP64B at96, viewport16B at160,
near/far8B at176, ortho flag at184; header scale.xy contains native phase/mode.
The existing sticky capability makes all normal query entry points and original
scene buffers use compatible variants after key-off/core reload. Legacy helpers
honor the recorded stride, although procedural legacy support remains separate.

Coverage combines original and noise-shifted masks using the native centered-red
signed/max expression, not just max(alpha). It then applies root suppression,
the full interpolated nonorthogonal strand-frame projection and density. Native
front-face sign cancels in the absolute expression; 4,912 skewed-frame cases test
this invariant. Camera visibility/origin uses original native W, VP, viewport,
phase and interpolated fade, including native nonzero-mode dither. Single-sample
raster coordinates are reprojected to their pixel centers. Shadow queries use the
light-ray view direction and native shadow hard branch, without camera fade or
dither. No generic RT stochastic cutoff replaces either native branch.

## Why fragment depth is still open

Native Depth writes `D' = (D*w + h*A)/(w+h)`, `A = near/(near-far)`, for perspective;
orthographic writes `D' = D + h/(near-far)`. This is a material-dependent virtual
surface, not an ordinary vertex offset or a proven negligible bias. With standard
native perspective projection `D=A+B/w`, the displaced point is
`Q=C+(P-C)*(1+h/w)`: it moves AWAY from the camera. The coverage view expression's
toward-camera shift is a separate shading calculation, not that geometric depth.

For finite normalized mask channels and defined smoothstep,
`0 <= h <= 0.01*max(material[4], material[5], 0)`. For positive vertex clip w,
component ratios `(P_i-C)/w_i` bound the whole triangle's ratio, permitting a
conservative widened AABB. Actual close-up material bytes were not in the F10
capture; no claim that h is below `depth_tol` is possible from existing evidence.

Next implementation: procedural Hair AABBs and original88-byte triangle sidecar,
reserved InstInfo bit12. Camera-collinear primary rays have a direct one-sample
solution; native shadow rays intersect the base card because native Shadow has
no fragment-depth output. Arbitrary secondary rays need a bounded interval solver
with explicit UNKNOWN on unresolved cases, not a fixed-step false miss. GPU bounds
failure will use reserved scene-stamp byte12 and make all query types unknown;
it cannot silently drop a caster or pass a missing-RT primary-match gate.

## Offline validation

Full isolated build and 21/21 CTests pass. Native Hair CPU test: 19,716 checks,
including six exact parameter-binding recipes, reset-on-read-failure, signed mask
combination, root/density effects, 4,912 strand sign cases and XFB interface checks.
All 15 native VS patch and validate; nine expose strand frames, five also fade,
and all six actual supported pass interfaces meet their individual requirements.
95 fresh key-off ordinary shader entries are byte-identical to main92183e7;
95 capable variants compile and validate. No GPU/game jobs. GPU cost, depth match,
image quality and temporal behavior remain unmeasured.
