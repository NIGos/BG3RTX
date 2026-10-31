#include "../common/native_opaque_transport.h"
#include "native_vt_surface_samples.h"
#include <cstdio>
#include <cstdlib>
#include <limits>
using namespace bg3::transport;
static unsigned checks;
static void check(bool v){++checks;if(!v){std::fprintf(stderr,"native parameter check %u failed\n",checks);std::exit(1);}}
static void near(float value,double expected){
    const double u=std::numeric_limits<float>::epsilon()/2;
    const double gamma=12*u/(1-12*u); // <=12 rounded elementary operations, unit-range terms.
    check(std::abs(double(value)-expected)<=gamma*(1+std::abs(expected)));
}
constexpr unsigned verified=NtSamplesKnown|NtLinearColorKnown|NtOpaqueCoverageKnown|NtLobesKnown;
NtInput input(float rough=.5f,float refl=.5f,float metal=0){return {{.2f,.4f,.8f},rough,refl,metal,0,verified};}
int main(){
    auto dielectric=nt_class0_parameters(input());check(dielectric.status==NtOk);
    near(dielectric.value.F0.x,.04);near(dielectric.value.diffuse_color.x,.2);
    auto metal=nt_class0_parameters(input(.5f,.5f,1));
    near(metal.value.F0.z,.8);check(metal.value.diffuse_color.z==0);
    auto polished=nt_class0_parameters(input(0));near(polished.value.base_roughness,.09);near(polished.value.base_alpha,.0081);
    auto black_input=input();black_input.linear_albedo={0,0,0};black_input.metalness=1;
    auto black=nt_class0_parameters(black_input);check(black.status==NtOk && black.value.F0.x==0 && black.value.diffuse_color.x==0);
    // Native scalar inputs at all UNORM8 codes; independent binary64 equations
    // use the exact float input values and the native real-valued expression.
    for(unsigned r=0;r<256;++r)for(unsigned m=0;m<256;++m){
        auto in=input(float(r)/255.f,float((r*73+m*17)&255)/255.f,float(m)/255.f);
        auto got=nt_class0_parameters(in);check(got.status==NtOk);
        check(got.value.reflectance==in.reflectance && got.value.metalness==in.metalness && got.value.linear_albedo.z==in.linear_albedo.z);
        const double q=double(in.reflectance),f0=.16*q*q,mix=double(in.metalness),rr=std::max(double(.09f),double(in.roughness));
        const float colors[]={in.linear_albedo.x,in.linear_albedo.y,in.linear_albedo.z};
        const float diffuse[]={got.value.diffuse_color.x,got.value.diffuse_color.y,got.value.diffuse_color.z};
        const float F[]={got.value.F0.x,got.value.F0.y,got.value.F0.z};
        for(unsigned k=0;k<3;++k){near(diffuse[k],double(colors[k])*(1-mix));near(F[k],f0*(1-mix)+double(colors[k])*mix);}
        near(got.value.base_alpha,rr*rr);
        // Native quad derivative input, not guessed from roughness or ray cones.
        TbVec3 dx={float(m)/255.f,0,0},dy={0,float(r)/510.f,0};
        auto aa=nt_native_quad_roughness(got,dx,dy,true);check(aa.status==NtOk);
        double variance=double(dx.x)*dx.x+double(dy.y)*dy.y;
        double expected=std::sqrt(std::min(1.,rr*rr+std::min(variance,double(.18f))));
        near(aa.native_roughness,expected);near(aa.alpha,expected*expected);
    }
    for(unsigned cls=1;cls<8;++cls){auto in=input();in.material_class=cls;check(nt_class0_parameters(in).status==NtUnsupportedClass);}
    for(unsigned flag:{NtTransmission,NtAnisotropy,NtExtraLobes,128u}){auto in=input();in.evidence|=flag;check(nt_class0_parameters(in).status==NtUnsupportedLobes);}
    const unsigned bits[]={NtSamplesKnown,NtLinearColorKnown,NtOpaqueCoverageKnown,NtLobesKnown};
    const unsigned states[]={NtUnknownSamples,NtUnknownLinearColor,NtUnknownOpaqueCoverage,NtUnsupportedLobes};
    for(unsigned k=0;k<4;++k){auto in=input();in.evidence&=~bits[k];check(nt_class0_parameters(in).status==states[k]);}
    for(float bad:{-1.f,std::nextafter(1.f,2.f),std::numeric_limits<float>::infinity(),std::numeric_limits<float>::quiet_NaN()}){
        for(unsigned field=0;field<4;++field){auto in=input();if(field==0)in.linear_albedo.x=bad;if(field==1)in.roughness=bad;if(field==2)in.reflectance=bad;if(field==3)in.metalness=bad;
            auto r=nt_class0_parameters(in);check(r.status==NtInvalid);check(r.value.diffuse_color.x==0 && r.value.F0.x==0);}
    }
    check(nt_native_quad_roughness(dielectric,{0,0,0},{0,0,0},false).status==NtUnknownFootprint);
    check(nt_native_quad_roughness(dielectric,{std::numeric_limits<float>::quiet_NaN(),0,0},{0,0,0},true).status==NtInvalid);
    auto aa=nt_native_quad_roughness(nt_class0_parameters(input(1)),{1,0,0},{0,1,0},true);check(aa.native_roughness==1 && aa.alpha==1);
    // Actual native VT decoder's sixteen physical/normal point-code fixtures.
    // Albedo below is a declared synthetic LINEAR sample, not inferred DDS sRGB.
    // The actual AlphaTest_VT asset has UNKNOWN opaque coverage and must refuse.
    for(const auto& point:native_vt_points){
        bg3::native_vt::Samples s{};s.albedo={.2f,.4f,.8f,1};
        for(unsigned k=0;k<4;++k){s.normal[k]=float(point.normal[k])/255.f;s.physical[k]=float(point.physical[k])/255.f;}
        s.available_layers=7;s.native_view_interpretation=true;s.tileset_magic=2202;s.reflectance_available=true;s.reflectance=.5f;
        bg3::native_vt::Surface surface;auto decoded=bg3::native_vt::decode(0x8995da1e4060f53bull,s,surface);
        check(decoded==bg3::native_vt::Status::SupportedFields);
        check(nt_from_native_surface(decoded,surface,0,verified&~NtOpaqueCoverageKnown).status==NtUnknownOpaqueCoverage);
        auto mapped=nt_from_native_surface(decoded,surface,0,verified);check(mapped.status==NtOk);
        near(mapped.value.F0.x,.16*double(surface.reflectance)*surface.reflectance*(1-surface.metalness)+double(surface.albedo[0])*surface.metalness);
        check(nt_from_native_surface(bg3::native_vt::Status::UnknownViewInterpretation,surface,0,verified).status==NtUnsupportedDecode);
    }
    std::printf("native class0 parameters: %u CPU checks passed; no TbMaterial weights or runtime qualification\n",checks);
}
