#include "../common/native_vt_surface.h"
#include "native_vt_surface_samples.h"
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <random>
using namespace bg3::native_vt;
static unsigned checks;
static void check(bool x){++checks;if(!x){std::fprintf(stderr,"native VT surface check %u failed\n",checks);std::exit(1);}}
static void oracle(const Samples& in,const Surface& out){
 // Independent conventional XYZ decode, then permute to native(T,N,B) weights.
 const double x=2.*in.normal[3]-1,y=-(2.*in.normal[1]-1),z=2.*in.normal[2]-1;
 const double length=std::sqrt(x*x+y*y+z*z);
 const double expected[]={x/length,z/length,y/length};
 // 16 rounded scalar operations bound this CPU decode vs ideal double inputs;
 // this is not a bound on native GPU texture sampling/normalize implementation.
 const double eps=std::numeric_limits<float>::epsilon(),gamma16=16*eps/(1-16*eps);
 double lengthOut=0;
 for(unsigned i=0;i<3;++i){check(std::abs(out.native_frame_normal[i]-expected[i])<=gamma16);lengthOut+=double(out.native_frame_normal[i])*out.native_frame_normal[i];}
 check(std::abs(lengthOut-1)<=gamma16*2);
 check(out.perceptual_roughness==std::clamp(in.physical[1],0.f,1.f));
 check(out.metalness==std::clamp(in.physical[0],0.f,1.f));
 check(out.reflectance==std::clamp(in.reflectance,0.f,1.f));
 check(out.albedo_aux==in.physical[2]);check(out.coverage==Coverage::Unknown);
}
int main(){
 Samples in{};Surface out{};
 check(recipe(recipes[0].fs)->transform_offset==64 && recipe(recipes[0].fs)->tileset_index_offset==100);
 check(recipe(recipes[1].fs)->transform_offset==0 && recipe(recipes[1].fs)->tileset_index_offset==32);
 check(decode(0,in,out)==Status::UnsupportedShader);
 const auto fs=recipes[0].fs;
 check(decode(fs,in,out)==Status::MissingSamples);
 in.available_layers=7;check(decode(fs,in,out)==Status::UnknownViewInterpretation);
 in.native_view_interpretation=true;check(decode(fs,in,out)==Status::MissingTileset);
 in.tileset_magic=2202;check(decode(fs,in,out)==Status::MissingReflectance);
 in.reflectance_available=true;in.reflectance=.5f;
 in.albedo={.2f,.3f,.4f,0};in.normal={0,.5f,1,.5f};in.physical={.1f,.2f,.3f,0};
 check(decode(fs,in,out)==Status::SupportedFields);check(out.native_frame_normal==V3({0,1,0}));
 // Albedo alpha 0 does not prove discard: earlier depth-pass coverage is absent.
 check(out.coverage==Coverage::Unknown);check(out.albedo==V3({.2f,.3f,.4f}));
 in.normal[1]=1;check(decode(fs,in,out)==Status::SupportedFields && out.native_frame_normal[2]<0);
 in.normal[1]=in.normal[2]=in.normal[3]=.5f;check(decode(fs,in,out)==Status::InvalidNormal);
 in.normal={0,.5f,1,.5f};in.normal[0]=std::numeric_limits<float>::quiet_NaN();check(decode(fs,in,out)==Status::InvalidSample);
 in.normal[0]=0;in.reflectance=std::numeric_limits<float>::infinity();check(decode(fs,in,out)==Status::InvalidSample);in.reflectance=.5f;
 in.tileset_magic=2201;check(decode(fs,in,out)==Status::MissingTileset);in.tileset_magic=2202;
 in.available_layers=3;check(decode(fs,in,out)==Status::MissingSamples);in.available_layers=7;
 // Actual archive-decoded point codes exercise CHANNEL ARITHMETIC only. Setting
 // the input contract flags here is a fixture, not actual native view/residency proof.
 for(const auto& point:native_vt_points){
  for(unsigned i=0;i<4;++i){in.albedo[i]=point.albedo[i]/255.f;in.normal[i]=point.normal[i]/255.f;in.physical[i]=point.physical[i]/255.f;}
  for(const auto& r:recipes){check(decode(r.fs,in,out)==Status::SupportedFields);oracle(in,out);}
 }
 std::mt19937 random(0x14678345);std::uniform_real_distribution<float> unit(0,1);
 for(unsigned i=0;i<16384;++i){
  in.normal={unit(random),unit(random),unit(random),unit(random)};
  check(decode(fs,in,out)==Status::SupportedFields);oracle(in,out);
 }
 in.normal={0,.5f,1,.5f};in.albedo={-1,2,.25f,0};in.physical={-1,2,.75f,0};in.reflectance=2;
 check(decode(fs,in,out)==Status::SupportedFields);check(out.albedo==V3({0,1,.25f}));oracle(in,out);
 std::printf("native VT post-sampling fields: %u checks, no GPU/runtime provider claim\n",checks);
}
