#include "../common/native_tileset_surface.h"
#include <cstdio>
#include <cstdlib>
#include <limits>
using namespace bg3;
static unsigned checks;
static void check(bool b){++checks;if(!b){std::fprintf(stderr,"failed check %u\n",checks);std::exit(1);}}
int main(){
 constexpr uint64_t fs=0x7621aebd092c3889ull,vs=0xaf1d30a11685c516ull;
 native_tileset::Plan p{};
 const float tiny=std::nextafter(0.f,1.f),nan=std::numeric_limits<float>::quiet_NaN();
 // Exact native branch boundary and precedence, not a tuned blend threshold.
 for(float r:{-1.f,-tiny,-0.f,0.f,tiny,0.5f,1.f})for(float g:{-1.f,-tiny,-0.f,0.f,tiny,0.5f,1.f}){
  auto s=native_tileset::plan(fs,vs,true,{r,g},{-2.f,0.5f},{2.f,3.f,4.f},p);
  unsigned expected=g>0?0:r>0?1:2;
  check(s==native_tileset::Status::Supported);check(unsigned(p.channel)==expected);
  check(p.normal_binding==2+expected && p.albedo_binding==5+expected && p.physical_binding==8+expected);
  check(p.uv[0]==-2.f*float(expected+2) && p.uv[1]==0.5f*float(expected+2));
 }
 check(native_tileset::plan(fs,vs,false,{0,1},{0,0},{1,1,1},p)==native_tileset::Status::MissingColor);
 check(native_tileset::plan(fs,vs,true,{nan,1},{0,0},{1,1,1},p)==native_tileset::Status::InvalidInput);
 check(native_tileset::plan(fs,vs,true,{0,1},{0,0},{1,nan,nan},p)==native_tileset::Status::Supported);
 check(native_tileset::plan(fs,vs,true,{0,1},{0,0},{nan,1,1},p)==native_tileset::Status::InvalidInput);
 check(native_tileset::plan(fs,vs,true,{0,1},{nan,0},{1,1,1},p)==native_tileset::Status::InvalidInput);
 check(native_tileset::plan(fs,vs+1,true,{0,1},{0,0},{1,1,1},p)==native_tileset::Status::UnsupportedShader);
 check(native_tileset::plan(0xcad41eff979970e4ull,0xa618ddae98c2d1cfull,true,{0,0},{0,0},{1,1,1},p)==native_tileset::Status::Supported);
 // Actual DDS point codes from nine joined texture assets. These are numerical
 // decoder fixtures; albedo below is an explicitly supplied interpreted sample,
 // not an assumption that authored SRGB metadata proves an actual native view.
 const unsigned normal[3][4]={{0,109,255,119},{0,252,150,123},{0,48,206,41}};
 const unsigned physical[3][4]={{82,85,255,127},{107,125,247,127},{19,159,227,255}};
 for(unsigned c=0;c<3;++c){
  native_vt::Samples in{};in.available_layers=7;in.albedo={0.1f,0.2f,0.3f,1.f};
  in.reflectance_available=true;in.reflectance=0.5f;
  for(unsigned k=0;k<4;++k){in.normal[k]=float(normal[c][k])/255.f;in.physical[k]=float(physical[c][k])/255.f;}
  native_vt::Surface out{};
  check(native_tileset::decode_selected(in,out)==native_vt::Status::UnknownViewInterpretation);
  in.native_view_interpretation=true;
  check(native_tileset::decode_selected(in,out)==native_vt::Status::SupportedFields);
  double n[3]={2.*in.normal[3]-1.,2.*in.normal[2]-1.,1.-2.*in.normal[1]};
  double len=std::sqrt(n[0]*n[0]+n[1]*n[1]+n[2]*n[2]);
  for(unsigned k=0;k<3;++k)check(std::abs(out.native_frame_normal[k]-n[k]/len)<8*std::numeric_limits<float>::epsilon());
  check(out.perceptual_roughness==in.physical[1] && out.metalness==in.physical[0] && out.albedo_aux==in.physical[2]);
  check(out.reflectance==0.5f && out.albedo[0]==0.1f && out.coverage==native_vt::Coverage::Unknown);
  in.available_layers=3;check(native_tileset::decode_selected(in,out)==native_vt::Status::MissingSamples);
  in.available_layers=7;in.reflectance_available=false;check(native_tileset::decode_selected(in,out)==native_vt::Status::MissingReflectance);
 }
 std::printf("native tileset surface: %u checks passed\n",checks);
}
