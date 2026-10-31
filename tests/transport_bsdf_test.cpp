#include "../common/transport_bsdf.h"
#include <algorithm>
#include <array>
#ifdef NDEBUG
#undef NDEBUG // The standalone/CMake Release test must execute its checks.
#endif
#include <cassert>
#include <cstdio>
#include <limits>
#include <vector>
using namespace bg3::transport;
constexpr double Pi=3.1415926535897932384626433832795;
constexpr double U=0x1p-24;
double gamma_n(int n) { return n*U/(1-n*U); }
TbMaterial material(float a=1,float wd=0,float ws=1) { return {{.7f,.3f,.1f},{1,1,1},wd,ws,a}; }

// Independent long-double angular equations (Walter2007), not shared helpers.
using V=std::array<long double,3>;
V unit(TbVec3 p) { long double n=std::sqrt((long double)p.x*p.x+(long double)p.y*p.y+(long double)p.z*p.z);return {p.x/n,p.y/n,p.z/n}; }
long double reference(TbMaterial m,TbVec3 o,TbVec3 i) {
    V a=unit(o),b=unit(i);if(a[2]<=0||b[2]<=0)return 0;
    V h={a[0]+b[0],a[1]+b[1],a[2]+b[2]};long double norm=std::sqrt(h[0]*h[0]+h[1]*h[1]+h[2]*h[2]);
    for(auto& x:h)x/=norm;
    long double aa=(long double)m.alpha*m.alpha;
    long double tan2=(h[0]*h[0]+h[1]*h[1])/(h[2]*h[2]);
    long double D=aa/(Pi*std::pow(h[2],4)*std::pow(aa+tan2,2));
    auto G1=[&](V v) { long double t=(v[0]*v[0]+v[1]*v[1])/(v[2]*v[2]);return 2/(1+std::sqrt(1+aa*t)); };
    long double c=a[0]*h[0]+a[1]*h[1]+a[2]*h[2];
    long double F=m.F0.x+(1-m.F0.x)*std::pow(1-c,5);
    return m.diffuse_weight*m.rho.x/Pi+m.specular_weight*D*F*G1(a)*G1(b)/(4*a[2]*b[2]);
}

// Standard Gauss-Legendre construction, independent of BSDF evaluation.
std::vector<std::array<double,2>> gauss(int n) {
    std::vector<std::array<double,2>> q;
    for(int i=0;i<n;i++) {
        double x=std::cos(Pi*(i+.75)/(n+.5)),derivative=0;
        for(int k=0;k<32;k++) {
            double p=1,prev=0;
            for(int j=1;j<=n;j++) { double last=prev;prev=p;p=((2*j-1)*x*prev-(j-1)*last)/j; }
            derivative=n*(x*p-prev)/(x*x-1);double delta=p/derivative;x-=delta;
            if(std::abs(delta)<=4*std::numeric_limits<double>::epsilon())break;
        }
        q.push_back({(x+1)*.5,1/((1-x*x)*derivative*derivative)});
    }
    return q;
}
struct Integral { double pdf=0,energy=0; };
Integral integrate(TbMaterial m,TbVec3 wo,int n) {
    Integral r;auto q=gauss(n);int phi_n=2*n;
    for(auto v:q)for(int j=0;j<phi_n;j++) {
        double mu=v[0],phi=2*Pi*(j+.5)/phi_n,s=std::sqrt(1-mu*mu);
        TbVec3 wi={(float)(s*std::cos(phi)),(float)(s*std::sin(phi)),(float)mu};
        auto e=tb_evaluate(m,wo,wi);assert(e.status==TbOk);
        double weight=v[1]*2*Pi/phi_n;r.pdf+=weight*e.pdf;r.energy+=weight*mu*e.f.x;
    }
    return r;
}

// Independent monotone upper/lower Riemann enclosure in uniform NDF-CDF u.
// For oblique wo, valid reflection iff cos(phi)>T(u); T increases with u.
// Thus the fraction of valid azimuths decreases from1 to0, giving a rigorous
// integration-width bound1/N, unlike an assumed Monte-Carlo error bar.
std::array<double,2> valid_mass_enclosure(double alpha,double nv,int n=32768) {
    if(nv==1) { double exact=1/(1+alpha*alpha);return {exact,exact}; }
    auto fraction=[&](double u) {
        if(u==0)return 1.;if(u==1)return 0.;
        double z2=(1-u)/(1-u+alpha*alpha*u);
        double t=nv*(1-2*z2)/(2*std::sqrt(1-nv*nv)*std::sqrt(z2*(1-z2)));
        return t<=-1?1.:t>=1?0.:std::acos(t)/Pi;
    };
    double lower=0,upper=0;
    for(int i=0;i<n;i++) { upper+=fraction(double(i)/n)/n;lower+=fraction(double(i+1)/n)/n; }
    double accumulation=n*std::numeric_limits<double>::epsilon()/(1-n*std::numeric_limits<double>::epsilon());
    assert(upper-lower<=1./n+2*accumulation);
    return {lower-accumulation,upper+accumulation};
}

int main() {
    int checks=0;double max_relative=0,max_pdf_quadrature_delta=0,max_energy=0;
    for(float alpha:{.05f,.15f,.5f,1.f})for(float nv:{.01f,.2f,.6f,1.f})for(float ni:{.01f,.2f,.6f,1.f})for(int p=0;p<13;p++) {
        double phi=2*Pi*p/13;TbVec3 wo={std::sqrt(1-nv*nv),0,nv};
        TbVec3 wi={(float)(std::sqrt(1-ni*ni)*std::cos(phi)),(float)(std::sqrt(1-ni*ni)*std::sin(phi)),ni};
        auto m=material(alpha,.25f,.5f);m.F0={.04f,.2f,.8f};auto e=tb_evaluate(m,wo,wi),rev=tb_evaluate(m,wi,wo);
        assert(e.status==TbOk&&rev.status==TbOk&&e.pdf>=0&&e.f.x>=0);
        double ref=(double)reference(m,wo,wi),rel=std::abs(e.f.x-ref)/std::max(ref,1e-30);
        // Forward error bound reflects half-vector normalization and GGX
        // angular sensitivity, rather than an empirically chosen epsilon.
        auto a=unit(wo),b=unit(wi);double hlen=(double)std::sqrt((a[0]+b[0])*(a[0]+b[0])+(a[1]+b[1])*(a[1]+b[1])+(a[2]+b[2])*(a[2]+b[2]));
        double envelope=gamma_n(64)*(1+16/(alpha*hlen)+4/alpha);
        assert(rel<=envelope);assert(std::abs(e.f.x-rev.f.x)<=gamma_n(16)*std::max(e.f.x,rev.f.x));
        max_relative=std::max(max_relative,rel);checks++;
    }
    // Independent analytic cases: Lambert hemispherical reflectance rho, and
    // GGX alpha=1/F0=1/normal energy1-ln2, continuous probability mass1/2.
    auto lam=material(0,1,0);auto l=integrate(lam,{0,0,1},128);
    assert(std::abs(l.pdf-1)<=gamma_n(32));assert(std::abs(l.energy-lam.rho.x)<=gamma_n(32));
    auto normal=integrate(material(),{0,0,1},256);
    assert(std::abs(normal.pdf-.5)<=gamma_n(32));assert(std::abs(normal.energy-(1-std::log(2.)))<=gamma_n(32));
    for(float a:{.15f,.5f,1.f})for(float nv:{.2f,.6f,1.f}) {
        TbVec3 wo={std::sqrt(1-nv*nv),0,nv};auto m=material(a,.25f,.5f);
        auto coarse=integrate(m,wo,128),fine=integrate(m,wo,256);
        auto mass=valid_mass_enclosure(a,(double)unit(wo)[2]);double pd=1./3,ps=2./3;
        double delta=std::abs(fine.pdf-coarse.pdf);
        // Quadrature refinement is sampled numerical evidence, not a global
        // certificate. The separate monotone NDF integral brackets the answer.
        double error=4*delta+gamma_n(128);
        assert(fine.pdf>=pd+ps*mass[0]-error&&fine.pdf<=pd+ps*mass[1]+error);
        double energy_error=4*std::abs(fine.energy-coarse.energy)+gamma_n(128);
        assert(fine.energy>=0&&fine.energy<=m.diffuse_weight+m.specular_weight+energy_error);
        max_pdf_quadrature_delta=std::max(max_pdf_quadrature_delta,delta);max_energy=std::max(max_energy,fine.energy);
        std::printf("alpha %.2f Nv %.2f pdf %.9f energy %.9f\n",a,nv,fine.pdf,fine.energy);
    }
    double max_white_specular_energy=0;
    for(float a:{.05f,.15f,.5f,1.f})for(float nv:{.02f,.2f,.6f,1.f}) {
        TbVec3 wo={std::sqrt(1-nv*nv),0,nv};auto m=material(a);
        auto coarse=integrate(m,wo,256),fine=integrate(m,wo,512);
        double error=4*std::abs(fine.energy-coarse.energy)+gamma_n(128);
        assert(fine.energy>=0&&fine.energy<=1+error);
        max_white_specular_energy=std::max(max_white_specular_energy,fine.energy);
    }
    // Normal incidence NDF null mass is analytic, including narrow alpha.
    for(float a:{.05f,.2f,.5f,1.f}) {
        int nulls=0,n=16384;auto m=material(a);
        for(int i=0;i<n;i++) {
            auto s=tb_sample(m,{0,0,1},0,(i+.5f)/n,.37f);
            if(s.status==TbNull)nulls++;else {assert(s.status==TbOk&&s.pdf>0);auto e=tb_evaluate(m,{0,0,1},s.wi);assert(std::abs(e.pdf-s.pdf)<=gamma_n(32)*e.pdf);}
        }
        double exact=(double)a*a/(1+(double)a*a);
        assert(std::abs(double(nulls)/n-exact)<=1./n+gamma_n(16));
    }
    // Sample mapping compared to separate double inverse-CDF/reflection math.
    for(int i=0;i<10000;i++) {
        float u=(i+.5f)/10000,v=((i*7919)%10000+.5f)/10000;auto m=material(.35f);
        TbVec3 wo={.6f,0,.8f};auto s=tb_sample(m,wo,0,u,v);
        double tan2=(double)m.alpha*m.alpha*u/(1-u),hz=1/std::sqrt(1+tan2),hs=std::sqrt(tan2)*hz;
        V h={hs*std::cos(2*Pi*v),hs*std::sin(2*Pi*v),hz},o=unit(wo);
        long double c=o[0]*h[0]+o[1]*h[1]+o[2]*h[2];V wi={2*c*h[0]-o[0],2*c*h[1]-o[1],2*c*h[2]-o[2]};
        if(wi[2]<-gamma_n(32))assert(s.status==TbNull);
        if(wi[2]>gamma_n(32)) { assert(s.status==TbOk);assert(std::abs(s.wi.x-wi[0])+std::abs(s.wi.y-wi[1])+std::abs(s.wi.z-wi[2])<=gamma_n(128)); }
    }
    auto m=material();m.alpha=0;assert(tb_sample(m,{0,0,1},0,.5f,.5f).status==TbUnsupportedDelta);
    m=material();m.alpha=std::numeric_limits<float>::denorm_min();assert(tb_evaluate(m,{0,0,1},{0,0,1}).status==TbNumericFailure);
    m=material();m.diffuse_weight=0x1p-25f;assert(tb_material_status(m)==TbInvalid); // rounded sum would hide excess energy
    m=material();m.F0.x=1.001f;assert(tb_material_status(m)==TbInvalid);
    m=material();m.rho.x=std::numeric_limits<float>::quiet_NaN();assert(tb_material_status(m)==TbInvalid);
    m=material(0,0,0);assert(tb_sample(m,{0,0,1},.5f,.5f,.5f).status==TbNull);assert(tb_evaluate(m,{0,0,1},{0,0,1}).status==TbOk);
    m=material();assert(tb_sample(m,{0,0,1},0,1,.5f).status==TbInvalid);
    assert(tb_sample(m,{0,0,1},0,.5f,std::numeric_limits<float>::quiet_NaN()).status==TbInvalid);
    assert(tb_sample(m,{0,0,-1},0,.5f,.5f).status==TbNull);
    assert(tb_evaluate(m,{0,0,0},{0,0,1}).status==TbInvalid);
    assert(tb_evaluate(m,{0,0,1},{0,0,-1}).status==TbOk);
    assert(tb_evaluate(m,{0,0,1},{0,0,-1}).pdf==0);
    assert(tb_evaluate(m,{0,0,1e30f},{0,0,1e-30f}).status==TbOk);
    for(float alpha:{1e-19f,1e-20f}) {
        auto extreme=tb_evaluate(material(alpha),{0,0,1},{0,0,1});
        assert(extreme.status==TbNumericFailure||(extreme.status==TbOk&&std::isfinite(extreme.f.x)&&std::isfinite(extreme.pdf)));
    }
    std::printf("PASS %d independent point checks; max relative %.9g; max quadrature refinement %.9g; max mixture energy %.9g; max white specular energy %.9g\n",checks,max_relative,max_pdf_quadrature_delta,max_energy,max_white_specular_energy);
}
