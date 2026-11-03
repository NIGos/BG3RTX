# Water surface shading analysis

2026-11-03, branch water-shader-analysis. Offline analysis, no game launch, no implementation.
Decision: yes, improve the native surface with small audited FS patches plus compose changes.
Keep water_reflect, reflection intersections, hit shading, tier selection and radiance sources unchanged.
Ranked increments and qualification rules: [M5.6](PLAN_M5_WATER.md#m56-water-surface-shading).

## Evidence and limits

Read PLAN_M5_WATER (M5.4, the game's medium, M5.5), P6_WATER, water_compose.hlsl,
water_reflect.hlsl, water_hit_shade.hlsli and the record/patch headers. Decompiled these existing
main-checkout captures/shaders modules with SPIRV-Cross --vulkan-semantics at BelowNormal:

| FS | Family / scene | Bytes | SHA-256 |
|---|---|---:|---|
| 7b44a567771ecbec | huge_PixelLighting, S1 docks / S8 sea | 105720 | af42cc0bfa7a571291fb5bf4fdee56e26b237a888ae97543f45da31027f4e7e2 |
| 99c1fcef82b253c1 | ExtraWaves, S8 before save load | 110340 | cf8f730bd80aafb23bed74976841f3f024abafb64ae44ee0c258ad152565d3b3 |
| c4e92ccdb08dd777 | NoCaustics_Waves, S8 pool | 89736 | 85d21fb90c1e7710d593feab7a6e4747106503efb728cf6d6a40c92187bcf7c9 |

The worktree has no captures or assetdb junction. Read those in the main checkout; wrote neither.
The asset DB maps the pool FS to StaticInstancedForward, material CB set 1 b1, 224 bytes.
Read the three corresponding Fluids/*.lsf templates through tools.assets dump from installed
Materials.pak. Values below are template defaults, not proof of the level instances' runtime CB.
The extraction and decompilation scratch files are in TEMP/bg3-water-analysis, outside the repo.

The old replay-20261101-113740-1 is absent from the inspected main and worktree capture roots.
Its flat-plane S8 estimates below are retained evidence from M5.5, not newly reproduced measurements.
Inspected test-main/captures/replay-20261101-142819-1/manifest.txt: pass gi_hit_direct,
frame 4175, 2560 x 1067 inputs, 875 instances, no omitted images. This is an opaque GI pack;
it does not capture the forward water CB, flow textures, per-pixel records or post-water target.
It cannot establish measured water glint energy, runtime IOR or transmission RGB versus distance.
No GPU replay or DXC was needed for this documentation-only change.

## Term-by-term findings

| Term | What the game does | Physical model and visible consequence | Compose / small patch feasibility |
|---|---|---|---|
| Waves and normal detail | Flow map s1b4, noise b5; two phase-shifted samples per wave normal layer, triangular phase blending; foam normal/detail layer too. Decode wzy to signed normal, flip z, normalize samples and their combinations, transform by interpolated tangent frame. Huge/ExtraWaves also have normal C; ExtraWaves adds its wave mask. | Resolved slopes should follow the displaced surface, with unresolved slopes retained statistically. At S8 pool and S1 distant sea, tiny lobes can sparkle during Q/E; phase blending is animated art, not a wave spectrum. This is a risk from the model, not a measured per-texture alias diagnosis. | Compose has final N, not UVs/frame/samples. Filter in FS. For M5.6 retain recorded N and reflection roughness: changing these would alter reflection directions or eligibility despite unchanged trace code. A full new wave model is outside scope. |
| Mip / LOD and aliasing | Normal textures use implicit texture() derivatives, not forced LOD 0. All three already compute r=max(0.09,sqrt(clamp(r_authored^2+min(dot(ddx(N),ddx(N))+dot(ddy(N),ddy(N)),0.18),0,1))). Individual normal samples are normalized before mixing. | Mips average vectors but renormalization loses information about subpixel slopes; derivatives of the filtered final N cannot recover it. Footprint-filtered normal moments or verified pre-normalization variance broaden a direct lobe while preserving its integral. S8 lit pool glints should become less intermittent, not simply dimmer. Actual sampler anisotropy, mip contents and selected LODs were not captured here. | Small FS patch before direct BRDF evaluation, with separate direct-only roughness; compose alone cannot reconstruct missing moments. Do not reuse the removed null rt_water_glint_aa key or assume its old result applies. |
| Fresnel and probes | Reflectance_Base=.35 gives F0=.16*.35^2=.0196; foam default .5 gives .04. Direct lights use Schlick at L.H. Probes use DFG(N.V,r): E=F0*DFG.x+DFG.y, roughness-filtered global and local cubes. Entire probe term is then multiplied by opacity a. | Interface reflection exists even above clear shallow water. At S8 near rim, a fades the reflected columns away with depth; at grazing angles reflection should dominate and transmission fall. Exact dielectric Fresnel differs from Schlick, but removing the erroneous a coupling has higher priority. | Existing A/B can rescale reflection without retracing, but division by a is invalid at a=0. FS must emit independent interface/probe and column terms plus new A/B, including fallback probes. Do not multiply the already-Fresnel-weighted RT term by F a second time or apply F(N.V) over direct F(L.H). |
| Sun and local specular | GGX D with alpha=r^2 and alpha^2=r^4; Smith-like visibility uses ((r+1)/2)^2. Sun evaluates a representative direction clamped to an angular disc (sin radius .00459006, about .263 degrees); N.L still uses the sun centre. Burley diffuse coexists with specular. | A water interface needs a footprint-integrated microfacet response and normalized finite-disc illumination. Closest-disc evaluation is an approximation, not an integrated solar lobe. S8 bright lit half can look like glitter over a coloured solid; shadowing correctly removes this native lobe. Do not infer an absolute energy excess from its peak alone. | FS can replace only direct specular, leaving N, Ks and reflection trace untouched. Start with footprint variance; finite-disc quadrature is a later alternative only if its reference shows a remaining bias. Rebuild sun rest R and the local-light sum/BRDF records consistently, or the existing shadow sum gate will fail. |
| Depth and transmission | d=linear bed depth-water view z, a camera-axis gap. Pool a=max(ramp(d/4),.5*ramp(d/.1)*distanceFade)*FadeOpacity, ramp(x)=1-(1-clamp(x))^2. Huge template OpacityBlendDistance=0 is clamped to .0001 m, making open sea opaque. | Optical depth follows the refracted segment, not camera z. S8 tiles/steps should change visibility consistently with angle and distance; an arbitrary axis-depth ramp can create a flat opacity band. A path ramp is a useful intermediate, not Beer-Lambert. | Compose's refraction ray already has th in metres; reuse it without a reflection ray change. Need FS column/surface separation, runtime ramp parameters and d. Invalid/unresolved paths retain native transmission. Do not apply this to foam/caustic opacity. |
| Absorption / scattering colour | Grey 1-a transmits RGB equally. ColorShallow/Deep are interpolated into diffuse albedo and lit as a Burley surface. Pool/sea defaults shallow=(.009532,.018028,.006265), deep=(.011126,.016988,.018913), ColorBlendDistance=1.5 m, exponent=1. Huge adds a depth-masked caustic flipbook to surface colour and opacity. | For a supported homogeneous medium T_rgb=exp(-sigma_t*L); absorption and out-scattering remove light, with a separately accounted in-scatter source. Albedo colours are not extinction coefficients. At S8 a physical column would tint the bed progressively rather than paint a blue/green surface over it. Existing data do not determine sigma_a, sigma_s or phase. | Patch+compose can replace separated transmission and source terms once units and coefficients are supplied. Cannot recover RGB extinction from the final C or convert ColorDeep directly to sigma. Keep huge sea, unknown instances and caustics native until mapping is proven; no new volumetric integrator in this lane. |
| Shore / foam / flow | Depth-edge foam and flow-speed masks modulate foam texture, normal, colour, reflectance and roughness. Pool EdgeFoamDistance=.075 m, EdgeFoamWeight=0 by default; flow masks can still contribute. Huge Foam_Opacity=0 by default, not proof of absent runtime foam. Distorted refraction is foreground-depth guarded and mirrored at viewport edges. | Foam is a scattering bubble layer, distinct from clean-water Fresnel; shoreline contact should follow geometric separation, flow should advect coherently. S8 rim and S3 camp log bridge can otherwise show view-dependent bands or pasted foam; these need visual proof, not automatic removal. | Requires FS mask/coverage separation. Compose must not stretch contact foam by replacing its view-depth mask with the full refracted distance. Preserve flow art initially. No simulation, shore geometry or foam billboard rewrite is justified. |
| Fog / atmosphere | Surface=(Lw*X+G*k_env)*T_air+S_air, then mix(Refr,surface,a). X/G are atmosphere terms; froxel fog supplies T_air/S_air. Refr already contains the colour copy's prior processing. | Air and water are different media. S8 distant sea should retain the game's aerial haze; underwater attenuation must not double-fog the copied bed. Fog is not evidence for water absorption. | Compose already carries fog/atmosphere in A/B and sun rest. Preserve these factors and order in every patch. No blanket post-water fog replacement; reflected-path fog remains a separate reflection limitation outside this task. |

## Existing compose contract: keep these separations

The audited FS writes C=(1-a)Refr+a*((direct+(Kd+Ks)*k)*X+G*k_env)*T_air+a*S_air.
The reflection delta is A*L-B, with A=a*T_air*X*E and B=a*T_air*X*k*Ks.
L is lighting-unit radiance. A matching native probe radiance gives zero delta; unresolved,
rough, back-facing, non-owned or invalid records preserve the corresponding native term.
The separate water_reflect diagnostic uses a geometric normal and analytic Schlick; compose
traces with the actual FS shading N and its DFG coefficients. Its diagnostic E is not a second
Fresnel factor to apply to composed L. water_hit_shade shades reflected objects, not the water.

Sun visibility uses the stored rest R=C-D, computed before target quantization, instead of
subtracting a large D from the B10G11R11 target. Occluded -> R, lit/unknown -> C; M5.4c restores
the game's removed sun Dg where audited cascade-shadow families require it. Local visibility
subtracts individual occluded point/spot terms after a sum match; projectors remain native.
Refraction replaces only Refr by a screen-resolved Snell sample: delta=(1-a)*(Copy(hit)-Refr).
It does not yet correct path attenuation, split interface energy or provide hidden-bed transport.
Any surface patch must update C, R, A/B and affected local sum inputs together; storing a tiny
new term and subtracting a huge quantized glint would repeat the M5.4 precision failure.

One correction to the old prose: c4e92ccd refraction offset uses abs(normal.xz) followed by
screen-axis signs/aspect scaling, not a demonstrably zero-mean signed jitter. Its directional
bias must be distinguished from Snell displacement; no new measurement of its mean is claimed.

## Quantification and confidence

Exact Fresnel calculation for n=(1+sqrt(.0196))/(1-sqrt(.0196))=1.325581, air incidence:

| Angle from normal | Exact dielectric F | Schlick F | Schlick minus exact |
|---:|---:|---:|---:|
| 0 degrees | .019600 | .019600 | .000000 |
| 45 degrees | .026968 | .021713 | -.005255 |
| 60 degrees | .058291 | .050237 | -.008053 |
| 75 degrees | .210008 | .238893 | +.028885 |
| 80 degrees | .345417 | .397371 | +.051953 |
| 85 degrees | .581533 | .641022 | +.059489 |

These are smooth-interface analytic values, not measurements of the rough DFG LUT.
For a=.5, the current smooth-limit coefficient a*Schlick at 80 degrees is .198686;
the interface coefficient .397371 is twice that before any exact-F correction.
At a=0 the native coefficient vanishes; a physical interface still reflects .0196 at normal incidence.
Foam's authored F0=.04 must not be interpreted as the IOR of clean water.
The current refraction path derives IOR from recorded F0; a foam-blended .04 would imply
n=1.5. That is another reason to restrict clean-water transport to a proven mask and retain
native foam. Exact Fresnel can be patched in direct lighting without a new reflection trace,
but exact F(N.V) must not replace the rough integrated DFG LUT blindly. It ranks below the
interface/opacity separation and footprint fix; no independent exact-F toggle is proposed yet.

With pool template fades=1 and no foam, T at d={0,.05,.1,.5,1,2,3,4} m is
{1,.625,.5,.5,.5,.25,.0625,0}. The .5 plateau ends at d=4*(1-sqrt(.5))=1.171573 m.
For homogeneous Beer-Lambert, T(2L)=T(L)^2; this ramp fails it (T(.5)=T(1)=.5).
RGB ratios of the directly transmitted bed are unchanged at every depth: grey attenuation.

At the roughness floor r=.09, GGX peak D=1/(pi*r^4)=4851.55 sr^-1; this is D, not output
radiance or integrated reflected energy. The derivative cap alone can raise zero-authored r
to sqrt(.18)=.424264. Thus the game has substantial AA, but no proven subpixel moment closure.

Read deciding log captures/queue/logs/m5w4-water-shadows-s8-fg2.1.141148.log in main:
14:14:33 window: 271357 sun records/frame, 62.65% occluded, D=.21855 per sun record,
removed=.17562 (80.36% of D), 46.665% of owned mean C, clamped=0.
Compose GPU .113 ms, copy .022 ms, dispatch .091 ms. At 14:14:37 the independent window
has 62.68%, 46.728%, clamped=0, GPU .113 ms. These are historical sun-visibility costs,
not measurements of M5.6. Earlier R-fix analysis reports median sun share 66%, top decile >97%,
glint C up to about 976 against rest about .05; attribution is total sun, not isolated specular.

M5.5's retained flat-plane replay estimate: pool 2.05% and sea 7.61% of view; pool Snell
shift median 52 px, p90 55 px. Pool mean transmission .16; transmission-weighted rays
91.1% screen-resolved, 8.9% hidden. Median refracted length/game d=1.19 (p10 .67, p90 1.60).
Replacing d by that length in the authored ramp predicts hit-pixel transmission .225 -> .157
(-30.2% relative). This is a model prediction, not a Beer-Lambert result or a new live gate pass.

## Reference physics and verification

The physical comparisons use PBRT's [dielectric Fresnel](https://www.pbr-book.org/4ed/Reflection_Models/Specular_Reflection_and_Transmission)
and [transmittance](https://www.pbr-book.org/4ed/Volume_Scattering/Transmittance), and the authors'
[LEAN mapping](https://userpages.cs.umbc.edu/olano/papers/lean/) for filterable normal distributions.
These motivate models; they do not establish BG3 instance parameters or predict a measured gain.
Analytic numbers above were recomputed with Python math at BelowNormal. All three modules
decompiled successfully after selecting Vulkan GLSL. No source shader, setting, ABI or variant
request changed. No runtime tests are warranted for this documentation-only change.
