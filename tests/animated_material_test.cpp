#include "../common/animated_material.h"
#include "../common/native_hit_pool.h"
#include <cstdio>
#include <cstdlib>
#include <limits>
using namespace bg3::animated;
#define CHECK(x) do{if(!(x)){std::fprintf(stderr,"line %d: %s\n",__LINE__,#x);std::exit(1);}}while(0)
int main(){
 // Eight float epsilon bounds cover the rounded operations in each scalar
 // flow/atlas expression; no image-tuned tolerance.
 // Independent scalar reference of native GLSL _116 and _140; include negative
 // UVs/speeds/time, non-unit tiling, alpha endpoints and several flow strengths.
 unsigned count=0,sprites=0;
 for(float t:{-100.f,-1.f,0.f,.25f,12345.f})for(float u:{-2.f,0.f,.3f,2.f})
 for(float speed:{-.1f,0.f,2.f})for(float alpha:{0.f,.25f,1.f})for(float strength:{-2.f,0.f,.3f,2.f}){
  auto scroll=am_scroll(am_uv(u,1-u),am_uv(2,.5f),am_uv(speed,-speed),t);
  CHECK(scroll.x==u*2.f+speed*t && scroll.y==(1-u)*.5f-speed*t);
  auto flow=am_lava_flow(am_uv(u,1-u),am_uv(speed,-speed),t,am_uv(.25f,.75f),alpha,strength);
  float x=u*(1-alpha)+(u+speed*t+(.25f-.5f)*strength)*alpha;
  float y=(1-u)*(1-alpha)+(1-u-speed*t+(.75f-.5f)*strength)*alpha;
  CHECK(std::abs(flow.x-x)<=8*std::numeric_limits<float>::epsilon()*std::max(1.f,std::abs(x)));
  CHECK(std::abs(flow.y-y)<=8*std::numeric_limits<float>::epsilon()*std::max(1.f,std::abs(y)));
  float phase=u+.5f, v=t*speed+phase+.5f;
  float tri=std::abs((v-std::floor(v))*2.f-1.f);
  CHECK(am_lava_bounce(t,speed,phase,2.f)==tri*tri*(3.f-2.f*tri)*2.f);
  float f=am_sprite_frame(t,speed,u*128.f,4,8);
  float expected=std::fma(speed*t,128.f,std::fmod(u*128.f,32.f))+.00001f;
  CHECK(f==expected);
  auto atlas=am_sprite_uv(am_uv(.25f,.75f),f,4,8);
  CHECK(std::abs(atlas.x-(.25f+std::trunc(std::fmod(f,4.f)))/4.f)<=8*std::numeric_limits<float>::epsilon());
  CHECK(std::abs(atlas.y-(.75f+std::trunc(std::fmod(f/4.f,8.f)))/8.f)<=8*std::numeric_limits<float>::epsilon());
  ++count;
 }
 // Native sprite template _209.._370, independently expressed using fmod
 // and nearbyint (default ties-to-even), including BaseInstance and seed wrap.
 for(unsigned base:{0u,17u,2048u})for(unsigned instance:{0u,1u,682u,10000u})
 for(float seed:{-.3f,0.f,.25f,1.7f})for(float t:{-2.f,0.f,.125f,123.f}){
  float id=float(base+instance),x=std::fmod(id,2047.1416015625f);
  auto fract=[](float v){return v-std::floor(v);};
  float rnd=std::clamp(std::nearbyint(fract(fract(fract(x*1.6180340051651001f)*x)+seed)*100.f)*.01f,0.f,1.f);
  float actual=am_sprite_random(id,seed);CHECK(actual==rnd);
  float advance=((.8999999761581421f+rnd*.20000004768371582f)*t)*.3f;
  float phase=std::fma(rnd,128.f,0.f);
  float reference=std::fma(advance,128.f,std::fmod(phase,32.f))+.00001f;
  float frame=am_sprite_native_frame(t,actual,.3f,4,8);
  CHECK(frame==reference);++sprites;
  auto atlas=am_sprite_uv(am_uv(.3f,.7f),frame,4,8);
  CHECK(std::abs(atlas.x-(.3f+std::trunc(std::fmod(reference,4.f)))/4.f)<=8*std::numeric_limits<float>::epsilon());
  CHECK(std::abs(atlas.y-(.7f+std::trunc(std::fmod(reference/4.f,8.f)))/8.f)<=8*std::numeric_limits<float>::epsilon());
 }
 // Native roundEven at exact half-way quantization boundaries.
 CHECK(am_sprite_random(0,.005f)==0.f);
 CHECK(am_sprite_random(0,.015f)==.02f);
 CHECK(am_lava_bounce(0,1,0,1)==0);
 CHECK(am_lava_bounce(.5f,1,0,1)==1);
 // A new layout must keep the published record size and native UBO offsets.
 bg3::native_vt::Recipe r{0x4b52d62ee893d3b6ull,0x2d2ac3c38992b7f0ull,0,32,36,
  bg3::native_vt::ReflectanceUbo,16,bg3::native_vt::CapturePool,bg3::native_vt::LayoutScroll};
 CHECK(bg3::native_vt::material_bytes(r)==20 && bg3::native_hit::pool::engine_bytes(r)==40);
 std::printf("%u scroll/flow/bounce/sprite formula cases and %u native sprite hash/frame/atlas cases passed\n",count,sprites);
}
