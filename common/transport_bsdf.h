#ifndef BG3_TRANSPORT_BSDF_H
#define BG3_TRANSPORT_BSDF_H
// Shared CPU/HLSL arithmetic prerequisite, not a native BG3 material adapter.
// Local +Z normal, both directions point away from surface, opaque reflection.
// rho/F0 are linear RGB [0,1]; alpha is GGX slope width, NOT perceptual roughness.
// Absolute nonnegative lobe weights sum <=1. This convex sum conserves energy;
// it is not a coupled dielectric layer or a guessed metalness conversion.
// Single-scatter GGX + Schlick + separable Smith. No delta, transmission,
// anisotropy, normal-map correction, or multiple scattering compensation.
// Sources and numerical/test limitations: tests/transport_bsdf_sources.md.
#ifdef __cplusplus
#include <cmath>
#include <cstdint>
namespace bg3::transport {
using TbUint=uint32_t;
struct TbVec3 { float x,y,z; };
inline TbVec3 tb_vec(float x,float y,float z) { return {x,y,z}; }
inline bool tb_finite(float x) { return std::isfinite(x); }
inline float tb_sqrt(float x) { return std::sqrt(x); }
inline float tb_sin(float x) { return std::sin(x); }
inline float tb_cos(float x) { return std::cos(x); }
inline float tb_abs(float x) { return std::fabs(x); }
#else
#define TbUint uint
#define TbVec3 float3
float3 tb_vec(float x,float y,float z) { return float3(x,y,z); }
bool tb_finite(float x) { return isfinite(x); }
float tb_sqrt(float x) { return sqrt(x); }
float tb_sin(float x) { return sin(x); }
float tb_cos(float x) { return cos(x); }
float tb_abs(float x) { return abs(x); }
#endif
static const float TbPi=3.14159265358979323846f;
static const TbUint TbOk=0, TbNull=1, TbInvalid=2, TbUnsupportedDelta=3, TbNumericFailure=4;
struct TbMaterial { TbVec3 rho,F0; float diffuse_weight,specular_weight,alpha; };
struct TbEval { TbVec3 f; float pdf; TbUint status; }; // f: sr^-1, pdf: sr^-1
struct TbSample { TbVec3 wi,f; float pdf; TbUint status; };
struct TbDirection { TbVec3 v; TbUint status; };
inline float tb_max(float x,float y) { return x>y?x:y; }
inline float tb_min(float x,float y) { return x<y?x:y; }
inline float tb_dot(TbVec3 a,TbVec3 b) { return a.x*b.x+a.y*b.y+a.z*b.z; }
inline bool tb_finite3(TbVec3 v) { return tb_finite(v.x)&&tb_finite(v.y)&&tb_finite(v.z); }
inline bool tb_color(TbVec3 v) { return tb_finite3(v)&&v.x>=0&&v.y>=0&&v.z>=0&&v.x<=1&&v.y<=1&&v.z<=1; }
inline TbDirection tb_direction(TbVec3 v) {
    TbDirection r;r.v=tb_vec(0,0,0);r.status=TbInvalid;
    if (!tb_finite3(v)) return r;
    float s=tb_max(tb_abs(v.x),tb_max(tb_abs(v.y),tb_abs(v.z)));
    if (!(s>0)) return r;
    v=tb_vec(v.x/s,v.y/s,v.z/s);
    float n=tb_sqrt(tb_dot(v,v));r.v=tb_vec(v.x/n,v.y/n,v.z/n);r.status=TbOk;
    return r; // Explicit normalization of any finite, nonzero input direction.
}
inline TbUint tb_material_status(TbMaterial m) {
    if (!tb_color(m.rho)||!tb_color(m.F0)||!tb_finite(m.alpha)||m.alpha<0||m.alpha>1||
        !tb_finite(m.diffuse_weight)||!tb_finite(m.specular_weight)||m.diffuse_weight<0||m.specular_weight<0)
        return TbInvalid;
    // When the larger weight is >=1/2, 1-larger is exact by Sterbenz's lemma.
    // Otherwise their sum is <1. Avoid accepting a sum rounded down to 1.
    float large=tb_max(m.diffuse_weight,m.specular_weight),small=tb_min(m.diffuse_weight,m.specular_weight);
    if (large>1||(large>=0.5f&&small>1-large)) return TbInvalid;
    if (m.specular_weight>0&&m.alpha==0) return TbUnsupportedDelta;
    if (m.specular_weight>0&&m.alpha*m.alpha==0) return TbNumericFailure;
    return TbOk; // alpha0 is unused/allowed for a diffuse-only or black material.
}
inline TbEval tb_zero(TbUint status) { TbEval r;r.f=tb_vec(0,0,0);r.pdf=0;r.status=status;return r; }
inline float tb_D(TbVec3 h,float a2) {
    // Avoid 1+(a2-1)*hz^2 cancellation near a narrow normal-incidence lobe.
    float d=h.x*h.x+h.y*h.y+a2*h.z*h.z;
    return (a2/d)/(TbPi*d);
}
inline TbEval tb_evaluate(TbMaterial m,TbVec3 wo_input,TbVec3 wi_input) {
    TbUint status=tb_material_status(m);if(status!=TbOk) return tb_zero(status);
    TbDirection od=tb_direction(wo_input),id=tb_direction(wi_input);
    if(od.status!=TbOk||id.status!=TbOk) return tb_zero(TbInvalid);
    TbVec3 wo=od.v,wi=id.v;
    if(wo.z<=0||wi.z<=0) return tb_zero(TbOk); // Valid zero reflection, not unknown.
    float total=m.diffuse_weight+m.specular_weight;
    if(total==0) return tb_zero(TbOk);
    float pd=m.diffuse_weight/total,ps=m.specular_weight/total;
    TbEval r=tb_zero(TbOk);
    float diffuse=m.diffuse_weight/TbPi;
    r.f=tb_vec(diffuse*m.rho.x,diffuse*m.rho.y,diffuse*m.rho.z);
    r.pdf=pd*wi.z/TbPi;
    if(m.specular_weight>0) {
        TbVec3 sum=tb_vec(wo.x+wi.x,wo.y+wi.y,wo.z+wi.z);
        TbDirection hd=tb_direction(sum);
        if(hd.status!=TbOk) return tb_zero(TbNumericFailure);
        // For unit vectors dot(wo,h)=dot(wi,h)=|wo+wi|/2. The symmetric form
        // also avoids avoidable FP32 reciprocity drift in grazing pairs.
        TbVec3 h=hd.v;float c=tb_min(1,0.5f*tb_sqrt(tb_dot(sum,sum))),a2=m.alpha*m.alpha;
        float D=tb_D(h,a2);
        float sv=tb_sqrt(a2+(1-a2)*wo.z*wo.z),si=tb_sqrt(a2+(1-a2)*wi.z*wi.z);
        // G1(v)/(2*Nv) =1/(Nv+sqrt(a2+(1-a2)*Nv^2)); no grazing 0/0.
        float g=m.specular_weight*D/(wo.z+sv)/(wi.z+si);
        float t=1-c,t2=t*t,t5=t2*t2*t;
        r.f=tb_vec(r.f.x+g*(m.F0.x+(1-m.F0.x)*t5),r.f.y+g*(m.F0.y+(1-m.F0.y)*t5),r.f.z+g*(m.F0.z+(1-m.F0.z)*t5));
        r.pdf+=ps*D*h.z/(4*c); // Full NDF half-vector density, reflection Jacobian.
    }
    if(!tb_finite3(r.f)||!tb_finite(r.pdf)||r.pdf<0) return tb_zero(TbNumericFailure);
    return r;
}
inline TbSample tb_sample(TbMaterial m,TbVec3 wo_input,float lobe_u,float u,float v) {
    TbSample r;r.wi=tb_vec(0,0,0);r.f=tb_vec(0,0,0);r.pdf=0;r.status=tb_material_status(m);
    if(r.status!=TbOk) return r;
    if(!tb_finite(lobe_u)||!tb_finite(u)||!tb_finite(v)||lobe_u<0||u<0||v<0||lobe_u>=1||u>=1||v>=1) { r.status=TbInvalid;return r; }
    TbDirection od=tb_direction(wo_input);if(od.status!=TbOk) { r.status=TbInvalid;return r; }
    TbVec3 wo=od.v;float total=m.diffuse_weight+m.specular_weight;
    if(wo.z<=0||total==0) { r.status=TbNull;return r; }
    float phi=2*TbPi*v;
    if(lobe_u<m.specular_weight/total) {
        float a2=m.alpha*m.alpha,d=(1-u)+a2*u;
        float hz=tb_sqrt((1-u)/d),hs=tb_sqrt(a2*u/d);
        TbVec3 h=tb_vec(hs*tb_cos(phi),hs*tb_sin(phi),hz);
        float c=tb_dot(wo,h);
        r.wi=tb_vec(2*c*h.x-wo.x,2*c*h.y-wo.y,2*c*h.z-wo.z);
        // Rejected NDF reflections are a real null atom. Never retry, clamp to
        // hemisphere, or renormalize the surviving continuous density.
        if(c<=0||r.wi.z<=0) { r.wi=tb_vec(0,0,0);r.status=TbNull;return r; }
    } else {
        float radius=tb_sqrt(u);r.wi=tb_vec(radius*tb_cos(phi),radius*tb_sin(phi),tb_sqrt(1-u));
    }
    TbEval e=tb_evaluate(m,wo,r.wi);r.f=e.f;r.pdf=e.pdf;r.status=e.status;
    if(r.status==TbOk&&!(r.pdf>0)) r.status=TbNumericFailure;
    return r;
}
#ifdef __cplusplus
} // namespace bg3::transport
#else
#undef TbUint
#undef TbVec3
#endif
#endif
