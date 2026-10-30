# Native CSM receiver audit (read-only, 2026-10-30)

Exact FS: 0687fa327aed7f8e. Retained module SHA256:
3f0b622b50d627977768b038125b7947d644fb88a5b7e6d4c09eb61407ee8ac3.
Primary source: main captures/shaders/0687fa327aed7f8e.spv. Fresh SDK
spirv-cross --vulkan-semantics output is BYTE IDENTICAL to existing main
captures/alpha_cache/0687fa327aed7f8e.glsl (GLSL SHA256
08553425b70e3fedbf3d8e51dfbb3accad187ef087abfefe3b720907e7a2d185).
No game/GPU calls or native shader edits were made for this audit.

## Bindings and receiver space

set2 b8: CSM depth texture2DArray; set2 b9: comparison sampler; set2 b14:
noncomparison sampler. set2 b26: 544-byte Sun UBO. Relevant offsets:
_m4 float@16 (tan parameter in blocker/PCF radius); _m6 float@24 (feather s);
_m7 float@28 (receiver bias); _m8 vec4@32 (D); _m9 vec4@48 (W);
_m10 row-major mat4[4]@64, matrix stride16,array stride64.

set1 b1 is linear-depth R32F; set1 b2 and b6 are material RGBA8; set1 b5
noise texture. Native VS a97ade8947e73bbc writes view-ray input from set0 b1
vec4[3]@112. FS reconstructs P = interpolatedRay * linearDepth (GLSL196-198).
Thus q_i = vec4(P,1) * SunUBO._m10[i], NO perspective divide in receiver code.
Do not substitute an unverified absolute-world/camera origin.

## Exact selection and blend

GLSL240-307 computes cascades0..3 in order. Define sat(x)=clamp(x,0,1),
q_i=(P,1)*M_i, h_i=sat((1-abs(2*sat(q_i.xy)-1))/s),
w_i=h_i.x*h_i.y*sat(sat(q_i.z)*D_i/(W_i*(s/2))).
The first cascade with NONZERO w_i is selected. No camera-distance split test.
For finite positive s,D,W, nonzero support needs 0<q.x<1,0<q.y<1,q.z>0.
There is NO explicit q.z<=1 receiver rejection: saturation makes q.z>1 positive.
If no positive support in the receiver's quad, result is1; quad derivative
sharing does not create an extra selected-layer contribution for an unsupported pixel.

Selected layer i uses blocker search7-i samples and PCF9-i samples. If i<3,
initial contribution is visibility_i*w_i. If w_i<1 and next layer i+1 also has
positive support, final visibility adds visibility_(i+1)*(1-w_i).
Only selected+NEXT are blended. If next support is zero it keeps the weighted
selected contribution (does NOT automatically add white). Layer3 fades to white:
visibility_3*w_3 + (1-w_3). Key lines412-427,434-477.

## Light-front/depth evidence and limits

Blocker search GLSL362 accepts storedDepth > sampleDepth. Its radius expression
at line330 contains tan(angle)*(1-q.z)*D_i, capped at0.21 before division by W_i.
This supports increasing q.z as lightward and a front at normalized depth1.
With STANDARD Vulkan clipping and affine/orthographic light matrices, native
caster support is q.xy in[0,1] and q.z in[0,1]; a lightward RT ray should be
clipped to that selected volume (or selected+next separately with native weights).
Compute its parametric exit from matrix planes, rather than generic rt_tmax300.
For the aligned sun axis, the apparent depth extent is D_i and front distance
is (1-q.z)*D_i. For off-axis disc rays, clip against all projected planes.

IMPORTANT: F10 does NOT contain pipeline depthClampEnable/depthClip state,
comparison-sampler state, actual Sun UBO values, or actual source draw membership
by vertex/index/instance identity. Do not certify the finite front from shaders
alone: depth-clamping could retain casters beyond the nominal front. Runtime
must validate affine matrices, positive finite D/W/s, matrix/space consistency,
and actual caster depth clipping before relying on that certificate.

## Auxiliary shadow domains

GLSL480-538 conditionally uses OTHER shadow arrays as well. set1 b0 UBO:
uint _m0@0, uint _m1@4 (aux enable), uint _m2@8 (layer), float _m3@12
(depth threshold), row-major matrices _m4@16,_m5@80. If enabled and
cameraDepthScale*depth > _m3: q5=(P,1)*_m5; when q5.xy is inside(0,1) and
q5.z>=0, plus CSM3 coverage<1, use min(CSM,compare(set1b4,layer,q5.z+5e-5)).
Otherwise it tries _m4 and set1b3 at depth+0.0007; that branch can REPLACE CSM.
No UBO bytes in F10 => cannot say whether this path is active in these captures.
Do not call the four CSM layers the entire possible native shadow domain.

## Current world capture evidence

20261030-163004 frame94565: submits[1].cmds[16..19] are four 2048x2048
D16 passes for image i72082 (four layers). Native sun-mask draw is
submits[1].cmds[22].events[1], FS0687..., VSa97..., sampling s2b8=i72082.
Four passes have respectively30/31/31/29 draw groups, sum n=116/141/143/131.
All four have13 FS hashes, same FS set. This does NOT establish geometry-set
containment or a concrete missing mesh; frame.json lacks per-draw VB/IB/matrix
identity and layer-view mapping. Largest-volume CSM3 is not a proof of near
caster membership. 163010 has the same counts; 162008 uses CSM image i57980.
Aux arrays at s1b3=i72053 are2048x2048 D16 two layers; s1b4=i72054 are1024x1024
D16 two layers. Bound resources alone do not prove enabled branches.

Actionable contract: preserve all current relevant native0..3 caster domains,
select/blend per exact receiver formula, keep layer/domain tags, validate actual
clipping/matrices and auxiliary-enable values. Do not use far3 volume or FS-set
similarity as proof that it contains near native geometry.

## PCSS radius and capture protocol clarification

The exact native blocker radius (GLSL330) uses tan(_m4), NOT tan(_m4/2):
max(min(0.21,tan(theta)*(1-q.z)*D_i)/W_i,
    max(2/(W_i/W_0),transmissionFloor)/2048).
The PCF radius (GLSL396) uses the same expression with meanBlockerDepth-q.z
instead of1-q.z. _m4 is interpreted by GLSL tan in radians; whether the engine
already supplies a physical half-angle is unknown until its bytes/semantic source
are captured. The cap0.21 is in units consistent with P and D; it is not a fixed
UV radius. Avoid naming it metres without coordinate/unit validation. Both kernels
use golden-angle2.39996314 and sqrt(k+0.5)/sqrt(sampleCount) disc offsets plus
normal/bias terms. Blocker search averages only samples with depth>receiver;
PCF compares using the shadow sampler, then performs the spatial cascade blend.

Capture complete set2b26 Sun block544bytes. Capture complete auxset1b0 block
144bytes, NOT96: matrix_m5 occupies80..143. Camera/frame bindings are also needed:
Frame0b0, native Sunmask View0b1 (400bytes, rays vec4[3]@112), native Camera0b2
(224bytes). Core GI's Camera binding does not itself prove P-space equivalence.
Native fullscreen VS negates clipY, interpolates View0b1 rays and FS multiplies
by set1b1 depth. Validate these versus the core's reconstructed P at same pixels,
actual CB/submit epoch and selected image identities, rather than assigning UBO
names from matching binding numbers alone. This is source evidence, not live data.


## Shared receiver/domain helper

`common/native_sun_math.h` is shared C++/HLSL math, not a packet parser or a
runtime certification source. `ns_select` reproduces the audited nominal weight,
first-positive selection and immediate-next blend; missing next coverage keeps
only weighted selected visibility. Layer 3 fades to white. Relevant captured
layer bits, strict native raster/clipping validation and receiver-space identity
are explicit mandatory inputs. It never assumes layer 3 contains nearer drawsets.
`ns_aux` tests actual per-receiver activation with the native strict greater-than
far-times-depth test. Active unsupported auxiliary coverage declines selection.

`ns_cone_bounds` takes an actual projected finite-disc center/halfwidth and
independently derived transform errors. `ns_cone_front` proves the full supplied
direction interval remains inside the layer XY volume until its slowest positive
lightward direction reaches qz=1. A linear path reaches each axis extremum at an
endpoint; an outward-rounded upper front distance therefore bounds all enclosed
paths. Nonpositive z direction, edge contact, invalid/negative errors or missing
native clip certificate decline. This is a sufficient conservative enclosure,
not a necessary domain test. Receivers beyond the front also decline for now.

`ns_ray_front` returns a lower/upper interval for the actual ray's rounded front
quotient and checks its XY support. It does NOT promise an exact float boundary:
consumers must enforce native candidate-domain clipping or fall back for ambiguous
front hits. In particular, whole source triangles outside native depth clipping
cannot be certified by an approximate tMax alone. No generic 300-unit ray limit
or unconstrained infinite-cone claim is made.

271,567 CPU checks cover the actual shared helper, including independent double
precision selection and 64 direction samples for each of 4,000 random cone cases
where certified. Dense samples are a numerical regression, not a continuous-domain
proof or validation of live matrices/errors/caster membership. A dynamic-input
HLSL compile fixture passes DXC and Vulkan 1.3 SPIR-V validation. No runtime shader,
public ABI, game/GPU work or live quality/performance gate changed here.
