#include "../common/native_tileset_coverage.h"
#include "../common/native_tileblend_coverage.h"
#include "../common/native_tileset_capability.h"
#include "../layer/spirv_xfb.h"
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
#include <limits>
using namespace bg3::native_tileset_coverage;
static int checks;
static void check(bool ok){++checks;if(!ok)std::abort();}
int main(int argc,char** argv){
 const float nan=std::numeric_limits<float>::quiet_NaN(),inf=std::numeric_limits<float>::infinity();
 // Independent native reference: coverage=green>0?alpha:1;
 // discard iff fma(coverage,1,-.5)<0. No stochastic threshold or erosion.
 for(float g:{-inf,-1.f,-0.f,0.f,std::nextafter(0.f,1.f),.5f,1.f,inf,nan})
  for(float a:{-1.f,0.f,std::nextafter(.5f,0.f),.5f,std::nextafter(.5f,1.f),1.f,nan,inf}){
   const auto r=ntc_evaluate(NtcKnown,true,g,true,a);
   const bool finite_g=std::isfinite(g),need=finite_g&&g>0;
   const bool known=finite_g&&(!need||std::isfinite(a));
   check((r.status==NtcKnown)==known);
   if(known)check(r.solid==!(std::fma(need?double(a):1.0,1.0,-.5)<0.0));
  }
 for(float mode:{-1.f,0.f,std::nextafter(1.f,0.f),1.f,std::nextafter(1.f,2.f),2.f})
  check(ntc_material(true,1.f,mode)==(mode==1.f?NtcSeeThrough:NtcKnown));
 check(ntc_material(false,1,0)==NtcMissing);
 check(ntc_material(true,nan,0)==NtcNonfinite);
 check(ntc_material(true,1,inf)==NtcNonfinite);
 check(ntc_evaluate(NtcSeeThrough,true,0,true,1).status==NtcSeeThrough);
 check(ntc_evaluate(NtcKnown,false,0,true,1).status==NtcMissing);
 check(ntc_evaluate(NtcKnown,true,1,false,0).status==NtcMissing);
 check(ntc_evaluate(NtcKnown,true,0,false,nan).solid);
 // Bounded actual packet read. Distinct middle scalars catch layout shortcuts.
 const std::array<float,4> packet{2.f,17.f,29.f,1.f};int reads=0;
 const auto m=capture([&](unsigned set,unsigned binding,unsigned off,void* dst,size_t bytes){
  ++reads;check(set==1&&binding==0&&off==0&&bytes==16);std::memcpy(dst,packet.data(),bytes);return true;
 });
 check(reads==1&&m.known&&m.tiling==2&&m.see_through==1);
 const auto absent=capture([](unsigned,unsigned,unsigned,void*,size_t){return false;});
 check(!absent.known&&ntc_material(absent.known,absent.tiling,absent.see_through)==NtcMissing);
 // Model the real descriptor range and dynamic offset, not a zero-filled
 // missing UBO. Every truncated native packet must remain unavailable.
 std::array<float,12> storage{};storage[4]=3.f;storage[7]=0.f;
 for(size_t available=0;available<=16;++available){
  const auto bounded=capture([&](unsigned,unsigned,unsigned off,void* dst,size_t bytes){
   if(off>available || bytes>available-off)return false;
   std::memcpy(dst,reinterpret_cast<const char*>(storage.data())+16+off,bytes);return true;
  });
  check(bounded.known==(available==16));
  if(bounded.known)check(bounded.tiling==3 && bounded.see_through==0);
 }
 // Exact two-pair table; ST shifts only its own material/Green bindings.
 check(lookup(depth_fs)->material_binding==0 && lookup(depth_fs)->green_binding==1);
 check(lookup(static_depth_fs)->material_binding==1 && lookup(static_depth_fs)->green_binding==2);
 check(matches(depth_vs,depth_fs) && matches(static_depth_vs,static_depth_fs));
 check(!matches(depth_vs,static_depth_fs) && !matches(static_depth_vs,depth_fs));
 check(vertex(depth_vs) && vertex(static_depth_vs) && !vertex(0));
 for(const auto& recipe:recipes){
  if(recipe.subtype)continue;
  for(size_t available=0;available<=16;++available){
   unsigned reads=0;
   const auto actual=capture([&](unsigned set,unsigned binding,unsigned off,void* dst,size_t bytes){
    ++reads;check(set==1 && binding==recipe.material_binding && off==0 && bytes==16);
    if(bytes>available)return false;
    const float values[4]={4.f,71.f,91.f,0.f};std::memcpy(dst,values,bytes);return true;
   },recipe.fs);
   check(reads==1 && actual.known==(available==16));
   if(actual.known)check(actual.tiling==4 && actual.see_through==0);
  }
 }
 bool read_unknown=false;
 const auto unsupported=capture([&](unsigned,unsigned,unsigned,void*,size_t){read_unknown=true;return true;},0);
 check(!unsupported.known && !read_unknown && lookup(0)==nullptr);
 // Native TileBlend packet and independent double-precision scalar oracle.
 bool wrong_packet_read=false;check(!capture([&](unsigned,unsigned,unsigned,void*,size_t){wrong_packet_read=true;return true;},tileblend_fs).known && !wrong_packet_read);
 check(matches(tileblend_vs,tileblend_fs) && !matches(depth_vs,tileblend_fs));
 check(lookup(tileblend_fs)->uv==0 && lookup(tileblend_fs)->subtype==1 && !vertex(tileblend_vs));
 std::array<float,8> blendPacket{2.f,-3.f,.5f,.125f,2.f,0.f,1.f,0.f};
 for(size_t bytes=0;bytes<=32;++bytes){
  auto material=capture_blend([&](unsigned set,unsigned binding,unsigned off,void* dst,size_t count){
   check(set==1 && binding==0 && off==0 && count==32);
   if(count>bytes)return false;std::memcpy(dst,blendPacket.data(),count);return true;
  });
  check(material.known==(bytes==32));check(ntb_material(material)==(bytes==32?NtcKnown:NtcMissing));
 }
 for(unsigned i=0;i<8;++i){auto bad=capture_blend([&](unsigned,unsigned,unsigned,void* dst,size_t bytes){std::memcpy(dst,blendPacket.data(),bytes);static_cast<float*>(dst)[i]=nan;return true;});check(ntb_material(bad)==NtcNonfinite);}
 for(float mode:{0.f,1.f,2.f}){BlendMaterial m;m.known=true;std::memcpy(m.packet,blendPacket.data(),32);m.packet[7]=mode;check(ntb_material(m)==(mode==1?NtcSeeThrough:NtcKnown));}
 for(float base:{0.f,.25f,.5f,.75f,1.f})for(float red:{0.f,.25f,.5f,1.f})for(float green:{0.f,.25f,.75f,1.f})for(float exponent:{0.f,1.f,2.f,-1.f}){
  const auto result=ntb_evaluate(NtcKnown,true,base,red,green,.125f,exponent,0.f,1.f);
  const double weight=std::clamp(std::pow(std::max(std::abs(double(green)+.125),.0001),double(exponent)),0.,1.);
  const double expected=((1.-weight)+double(base)*weight)*(1.-red);
  check(result.status==NtcKnown);check(result.solid==(expected>=.5));
 }
 check(ntb_evaluate(NtcKnown,true,0,0,0,0,1,0,1).solid); // raw alpha would incorrectly discard
 check(!ntb_evaluate(NtcKnown,true,1,1,0,0,1,0,1).solid); // maskR cuts even opaque base
 check(ntb_evaluate(NtcKnown,false,1,0,0,0,1,0,1).status==NtcMissing);
 check(ntb_evaluate(NtcKnown,true,1,0,0,0,1,1,0).status==NtcMissing);
 check(ntb_evaluate(NtcKnown,true,1,0,0,0,-1000,0,1).status==NtcNonfinite);
 check(ntb_evaluate(NtcSeeThrough,true,1,0,0,0,1,0,1).status==NtcSeeThrough);
 check(ntb_evaluate(NtcKnown,true,.5f,0,1,0,1,0,1).solid);
 check(!ntb_evaluate(NtcKnown,true,std::nextafter(.5f,0.f),0,1,0,1,0,1).solid);
 // Native DEPS uses only28B; camera-only SeeThrough must not affect it.
 check(lookup_shadow(tileblend_shadow_fs) && !lookup_camera(tileblend_shadow_fs));
 check(lookup_camera(tileblend_fs) && !lookup_shadow(tileblend_fs));
 check(matches(tileblend_shadow_vs,tileblend_shadow_fs) && !matches(tileblend_vs,tileblend_shadow_fs));
 check(!matches(tileblend_shadow_vs,tileblend_fs) && !vertex(tileblend_shadow_vs));
 for(float unused_mode:{0.f,1.f,2.f,nan,inf}){
  auto source=blendPacket;source[7]=unused_mode;
  for(size_t available=0;available<=32;++available){
   auto shadow=capture_blend([&](unsigned set,unsigned binding,unsigned off,void* dst,size_t bytes){
    check(set==1 && binding==0 && off==0 && bytes==28);
    if(bytes>available)return false;std::memcpy(dst,source.data(),bytes);return true;
   },true);
   check(shadow.known==(available>=28));check(shadow.native_shadow && shadow.packet[7]==0.f);
   check(ntb_material(shadow)==(available>=28?NtcKnown:NtcMissing));
  }
 }
 for(unsigned i=0;i<7;++i){auto bad=capture_blend([&](unsigned,unsigned,unsigned,void* dst,size_t bytes){std::memcpy(dst,blendPacket.data(),bytes);static_cast<float*>(dst)[i]=nan;return true;},true);check(ntb_material(bad)==NtcNonfinite);}
 BlendMaterial shadow;shadow.known=shadow.native_shadow=true;std::memcpy(shadow.packet,blendPacket.data(),32);shadow.packet[7]=nan;
 check(ntb_material(shadow)==NtcKnown); // deliberately ignored, never copied torecord
 shadow.packet[5]=2.f;shadow.packet[6]=1.f;check(ntb_material(shadow)==NtcMissing);
 // Minimal parser fixture with COLOR0 vec4 and UV1 vec2, not executable GPU code.
 std::vector<uint32_t> code={0x07230203,0x10000,0,100,0};
 auto emit=[&](std::initializer_list<uint32_t> v){code.insert(code.end(),v);};
 emit({(2u<<16)|17,1});emit({(5u<<16)|15,0,50,0x6e69616d,0});
 emit({(4u<<16)|71,20,11,0});emit({(4u<<16)|71,30,30,0});emit({(4u<<16)|71,31,30,1});
 emit({(3u<<16)|22,1,32});emit({(4u<<16)|23,2,1,2});emit({(4u<<16)|23,4,1,4});
 emit({(4u<<16)|32,5,3,2});emit({(4u<<16)|32,7,3,4});
 emit({(4u<<16)|59,7,20,3});emit({(4u<<16)|59,7,30,3});emit({(4u<<16)|59,5,31,3});
 for(uint32_t stride:{40u,64u,80u,88u}){
  std::vector<uint32_t> patched,off,explicit_off;int uv=-1;uint32_t color=0,hair=0;
  check(bg3::patch_vs(code.data(),code.size(),patched,stride,&uv,&hair,false,nullptr,true,&color));
  check(uv==1 && color==1 && hair==0);
  bool offset=false,step=false;
  for(size_t i=5;i<patched.size();i+=patched[i]>>16){
   if((patched[i]&0xffff)==71 && (patched[i]>>16)==4 && patched[i+1]==30){
    offset|=patched[i+2]==35 && patched[i+3]==24;
    step|=patched[i+2]==37 && patched[i+3]==stride;
   }
  }
  check(offset&&step);
  check(bg3::patch_vs(code.data(),code.size(),off,stride));
  check(bg3::patch_vs(code.data(),code.size(),explicit_off,stride,nullptr,nullptr,false,nullptr,false,&color));
  check(off==explicit_off && color==0);
 }
 emit({(3u<<16)|71,30,14}); // flat COLOR is not advertised as native smooth data.
 std::vector<uint32_t> patched;uint32_t color=1;
 check(bg3::patch_vs(code.data(),code.size(),patched,40,nullptr,nullptr,false,nullptr,true,&color));check(color==0);
 check(!bg3::native_tileset::requested(false,false));
 check(bg3::native_tileset::requested(false,true));
 check(bg3::native_tileset::requested(true,false));
 check(bg3::native_tileset::requested(true,true));
 uint32_t capability=0;check(!bg3::native_tileset::enabled(&capability));
 bg3::native_tileset::publish(&capability);check(bg3::native_tileset::enabled(&capability));
 bg3::native_tileset::publish(&capability);check(capability==1); // held/core reload never clears it.
 // Optional retained native module proof; no game resources required by CTest.
 // Writes only beside the caller-supplied private output prefix for spirv-val.
 if(argc==3){
  std::ifstream in(argv[1],std::ios::binary);
  std::vector<char> bytes((std::istreambuf_iterator<char>(in)),{});
  check(bytes.size()>=20 && bytes.size()%4==0);
  std::vector<uint32_t> native(bytes.size()/4);std::memcpy(native.data(),bytes.data(),bytes.size());
  for(uint32_t stride:{40u,64u,80u,88u}){
   std::vector<uint32_t> output,off,explicit_off;int uv=-1;uint32_t color=0,hair=0;
   check(bg3::patch_vs(native.data(),native.size(),output,stride,&uv,&hair,false,nullptr,true,&color));
   check(uv==1 && color==1 && hair==0);
   check(bg3::patch_vs(native.data(),native.size(),off,stride));
   check(bg3::patch_vs(native.data(),native.size(),explicit_off,stride,nullptr,nullptr,false,nullptr,false,&color));
   check(off==explicit_off && color==0);
   std::ofstream f(std::string(argv[2])+std::to_string(stride)+".spv",std::ios::binary);
   f.write(reinterpret_cast<const char*>(output.data()),output.size()*4);check(bool(f));
   std::ofstream baseline(std::string(argv[2])+std::to_string(stride)+".off.spv",std::ios::binary);
   baseline.write(reinterpret_cast<const char*>(off.data()),off.size()*4);check(bool(baseline));
  }
 }
 if(argc==4){
  std::ifstream in(argv[3],std::ios::binary);std::vector<char> bytes((std::istreambuf_iterator<char>(in)),{});
  check(bytes.size()>=20 && bytes.size()%4==0);std::vector<uint32_t> native(bytes.size()/4);std::memcpy(native.data(),bytes.data(),bytes.size());
  for(uint32_t stride:{40u,64u,80u,88u}){
   std::vector<uint32_t> ordinary,explicitOff;int uv=-1;uint32_t color=9;
   check(bg3::patch_vs(native.data(),native.size(),ordinary,stride,&uv));check(uv==0);
   check(bg3::patch_vs(native.data(),native.size(),explicitOff,stride,nullptr,nullptr,false,nullptr,false,&color));
   check(color==0 && ordinary==explicitOff); // native TileBlend needs no COLOR rewrite
   std::ofstream out(std::string(argv[2])+std::to_string(stride)+".tileblend.spv",std::ios::binary);
   out.write(reinterpret_cast<const char*>(ordinary.data()),ordinary.size()*4);check(bool(out));
  }
 }
 std::printf("%d native tileset coverage checks passed\n",checks);
}
