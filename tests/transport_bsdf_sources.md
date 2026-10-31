# Opaque transport arithmetic prerequisite

`common/transport_bsdf.h` is the same source for C++ and HLSL. It is not wired
into a renderer or the user trial. A separate explicit native-parameter adapter
now targets the declared FresnelBlend model below. The compile
fixture exercises dynamic eval/sample/PDF paths without creating a device.

## Original convex model: physical convention and scope

Local normal is +Z. Both input directions point away from the surface; finite,
nonzero directions are explicitly normalized. Reflection has zero density/value
when either direction is outside the open upper hemisphere. `rho` and `F0` are
linear RGB reflectances in [0,1]. `alpha` is isotropic GGX slope width, not a
perceptual roughness parameter. Nothing converts a BG3 texture or scalar here.

The BRDF is `wd * rho/pi + ws * D * F * G1(wo) * G1(wi)/(4*No*Ni)`.
F is Schlick's fifth-power approximation. G is **separable Smith**, not the
height-correlated variant used by pbrt 4. Nonnegative absolute weights satisfy
`wd+ws <= 1`, checked without accepting a rounded excess. Each component is
energy bounded; their convex sum is energy bounded and reciprocal. This is a
convex mixture of two opaque materials, not a layered dielectric prescription.
GGX single scattering loses energy to unmodeled multiple scattering. No energy
compensation, transmission, delta, anisotropy, shading-normal correction or
polarization is implemented. A BRDF value can exceed 1; it is never clamped.

The Smith bound can also be seen through the normalized visible-normal density:
`p_visible(h)=G1(wo)*D(h)*max(wo.h,0)/No`. For a valid reflected ray,
`f*Ni/p_visible(wi)=F*G1(wi) <= 1`. This argument explains energy boundedness;
the actual sampler below uses the simpler full NDF and retains its null mass.

## Sampling measure and failure semantics

Lobe probabilities normalize active weights; eval retains their absolute values.
Diffuse sampling is cosine weighted. Specular sampling draws an upper-hemisphere
GGX NDF normal with density `D(h)*h.z`, reflects, and uses Jacobian
`1/(4*abs(wo.h))`. The continuous mixture PDF is with respect to solid angle.
Rejected NDF samples are an explicit **null atom**; they are never retried or
renormalized. At normal incidence, continuous specular mass is
`1/(1+alpha^2)` and null mass is `alpha^2/(1+alpha^2)`.

`TbOk` includes known zero eval. `TbNull` is a known zero sample, including a
black material or rejected direction. `TbInvalid`, `TbUnsupportedDelta` and
`TbNumericFailure` must invalidate a future reference sample, not become black
transport. Alpha 0 is unsupported when specular weight is active; it is unused
for diffuse-only/black inputs. Random values must be finite in [0,1). Parameter
underflow or nonfinite computed output returns numeric failure. No hidden
roughness floor is applied. Extreme subnormal behavior is device dependent;
CPU/compile tests do not certify GPU arithmetic or execute those cases on GPU.

## Primary references

- [Walter et al. 2007, Microfacet Models for Refraction through Rough Surfaces](https://www.graphics.cornell.edu/~bjw/microfacetbsdf.pdf):
  equations 23 and 33-36 supply separable Smith GGX and full-normal sampling.
- [pbrt, Sampling Reflection Functions](https://www.pbr-book.org/3ed-2018/Light_Transport_I_Surface_Reflection/Sampling_Reflection_Functions):
  microfacet reflection change of variables and rejected hemisphere samples.
- [pbrt, Fresnel Incidence Effects](https://www.pbr-book.org/3ed-2018/Reflection_Models/Fresnel_Incidence_Effects):
  Schlick approximation convention.
- [pbrt, Lambertian Reflection](https://www.pbr-book.org/3ed-2018/Reflection_Models/Lambertian_Reflection):
  reflectance divided by pi.

Equations were implemented from these models; no renderer code was copied.

## Verification limits

The CPU test uses separate angular long-double equations (binary64 on MSVC), analytic Lambert and
normal-incidence alpha=1 cases (specular energy `1-ln(2)`), reciprocity, independent
hemisphere quadrature, and a monotone NDF-CDF azimuth integral. For the latter,
upper/lower Riemann sums have mathematical width `1/N`; double accumulation is
separately bounded. This is not a formal certificate for the library's `acos`.

Point error envelopes derive from FP32 gamma terms and GGX/half-vector angular
conditioning. Quadrature compares 128/256 or 256/512 Gauss-Legendre resolutions;
its refinement difference is a convergence indicator, not a rigorous continuous
error certificate. Sampled energy checks do not replace the model's argument.
There is no native material, scene closure, rendered-quality, GPU parity or pass
cost claim. Root owns CMake integration and future transport callers.

## Declared opaque FresnelBlend extension

`TbFresnelBlend` / `tb_fresnel_evaluate` / `tb_fresnel_sample` implement the
published Ashikhmin–Shirley/PBRT FresnelBlend form with isotropic GGX NDF. The
same GGX D, direction normalization and inverse-CDF reflection/cosine direction
mapping serve both models. There is no second reference renderer.

With `g(x)=1-(1-x/2)^5`, the diffuse term is
`28/(23*pi) * Rd*(1-Rs)*g(No)*g(Ni)`. Specular is
`D(h)*[Rs+(1-Rs)*(1-VoH)^5]/[4*VoH*max(No,Ni)]`. The polynomial implementation
of g avoids grazing subtraction cancellation. No BRDF value or energy is clamped.
Both reflectances must be finite unit-range RGB; alpha is positive GGX slope
width. Delta, transmission, anisotropy and normal correction remain unsupported.

This model follows the [PBRT Fresnel Incidence Effects equations](https://www.pbr-book.org/3ed-2018/Reflection_Models/Fresnel_Incidence_Effects).
The [original Ashikhmin–Shirley paper](https://collections.lib.utah.edu/dl_files/77/ea/77eac0c124401e935d08fd9208b98c0990cb078c.pdf)
derives substrate attenuation using Fresnel reflection and reciprocity. This
implementation uses GGX, not the paper's original anisotropic Phong distribution.

The solid-angle PDF is `0.5*cos(Ni)/pi + 0.5*D(h)*h.z/(4*VoH)`. Independent
random lobe/u/v inputs use fixed 1/2 cosine and 1/2 full-NDF selection, matching
the sampling strategy in [PBRT's reference source](https://github.com/mmp/pbrt-v3/blob/master/src/core/reflection.cpp).
These are sampling probabilities, not material weights; eval never halves its
BRDF. The NDF branch keeps its null atom, without retries or survivor
renormalization. Normal-incidence null mass is `0.5*alpha²/(1+alpha²)`.
The hemisphere PDF therefore generally integrates below 1; PDF plus null mass
is normalized. Eval returns PDF alongside f; sample returns that same full PDF.
Equations were implemented independently; no PBRT source was copied.

`nt_to_fresnel_blend` is an explicit physical-mode **policy**: qualified native
diffuse_color -> Rd, native F0 -> Rs and intrinsic base_alpha -> alpha. It does
not select lobe weights or claim that BG3's Burley/native-visibility terms equal
this scattering law. Unknown native-provider status propagates. Unsupported
sampling/coverage and actual material acquisition remain runtime prerequisites.

Additional verification: 1,700 independent binary64 pointwise f/PDF comparisons
and reciprocity, normal-incidence analytic f/PDF cases, exact diffuse normalization,
analytic rough-white specular integral, independently enclosed oblique PDF mass,
and explicit null sampling. Fifty white-furnace endpoint cases cover alpha
{0.0081,0.05,0.2,0.6,1} and No {0.001,0.02,0.2,0.6,1}, Rd=1 and Rs={0,1}.
The model is linear per channel in Rd and Rs (with nonnegative diffuse), so
these are the worst reflectance corners at the tested alpha/No values.
Specular integrates actual shared eval in NDF-CDF coordinates to resolve narrow
lobes; diffuse uses separate cosine quadrature. Refinement 256 -> 512 gives an
observed numerical error indicator, not a proof over the continuous parameter
domain or GPU floating-point behavior. No failing case was hidden or retuned.
Exact measured results are retained in the accompanying evidence JSON.
