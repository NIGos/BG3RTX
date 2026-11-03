#include "../common/animated_material.h"
#include "../common/native_hit_pool.h"
#include <cstdio>
#include <cstdlib>
#include <limits>
using namespace bg3::animated;
#define CHECK(x) do{if(!(x)){std::fprintf(stderr,"line %d: %s\n",__LINE__,#x);std::exit(1);}}while(0)
int main(){
 // Independent scalar reference of native GLSL _116 and _140; include negative
 // UVs/speeds/time, non-unit tiling, alpha endpoints and several flow strengths.
 unsigned count=0;
 for(float t:{-100.f,-1.f,0.f,.25f,12345.f})for(float u:{-2.f,0.f,.3f,2.f})
 for(float speed:{-.1f,0.f,2.f})for(float alpha:{0.f,.25f,1.f}){
  auto scroll=am_scroll(am_uv(u,1-u),am_uv(2,.5f),am_uv(speed,-speed),t);
  CHECK(scroll.x==u*2.f+speed*t && scroll.y==(1-u)*.5f-speed*t);
  auto flow=am_lava_flow(am_uv(u,1-u),am_uv(speed,-speed),t,am_uv(.25f,.75f),alpha,2.f);
  float x=u*(1-alpha)+(u+speed*t+(.25f-.5f)*2.f)*alpha;
  float y=(1-u)*(1-alpha)+(1-u-speed*t+(.75f-.5f)*2.f)*alpha;
  CHECK(std::abs(flow.x-x)<=8*std::numeric_limits<float>::epsilon()*std::max(1.f,std::abs(x)));
  CHECK(std::abs(flow.y-y)<=8*std::numeric_limits<float>::epsilon()*std::max(1.f,std::abs(y)));
  float phase=u+.5f, v=t*speed+phase+.5f;
  float tri=std::abs((v-std::floor(v))*2.f-1.f);
  CHECK(am_lava_bounce(t,speed,phase,2.f)==tri*tri*(3.f-2.f*tri)*2.f);
  float f=am_sprite_frame(t,speed,u*128.f,4,8);
  float expected=speed*t*128.f+std::fmod(u*128.f,32.f)+.00001f;
  CHECK(f==expected);
  auto atlas=am_sprite_uv(am_uv(.25f,.75f),f,4,8);
  CHECK(std::abs(atlas.x-(.25f+std::trunc(std::fmod(f,4.f)))/4.f)<1e-5f);
  CHECK(std::abs(atlas.y-(.75f+std::trunc(std::fmod(f/4.f,8.f)))/8.f)<1e-5f);
  ++count;
 }
 CHECK(am_lava_bounce(0,1,0,1)==0);
 CHECK(am_lava_bounce(.5f,1,0,1)==1);
 // A new layout must keep the published record size and native UBO offsets.
 bg3::native_vt::Recipe r{0x4b52d62ee893d3b6ull,0x2d2ac3c38992b7f0ull,0,32,36,
  bg3::native_vt::ReflectanceUbo,16,bg3::native_vt::CapturePool,bg3::native_vt::LayoutScroll};
 CHECK(bg3::native_vt::material_bytes(r)==20 && bg3::native_hit::pool::engine_bytes(r)==40);
 std::printf("%u scroll/flow/bounce/sprite formula cases passed\n",count);
}
