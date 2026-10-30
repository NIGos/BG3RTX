#pragma once
// Bounded CHAR_Fur_IDmsk StaticDepth/Shadow source helpers. No runtime hook or
// texture/sampler proof is implied. Unverified variants remain unsupported.
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace bg3::native_fur {
struct Recipe {uint64_t fs,vs;bool camera;};
inline constexpr Recipe recipes[]={
 {0xc22b20253e25e473ull,0x0d6885be08b2faa4ull,true},
 {0x1e144b6e40e48558ull,0xff2a169c3d58bac8ull,false},
};
inline const Recipe* lookup(uint64_t fs,uint64_t vs){
 for(const auto& r:recipes)if(r.fs==fs && r.vs==vs)return &r;
 return nullptr;
}
inline constexpr uint32_t material_set=1,material_binding=1,mask_set=1,mask_binding=3,
 sampler_set=0,sampler_binding=7,post_vs_stride=88;
struct Captured {
 // 0 Soupleness (already applied by captured VS), 1 DitherSoftenMaximum,
 // 2 DitherSoftenMinimum, 3 OpacityFresnelPower, 4 CastShadow (Shadow only).
 std::array<float,8> material{};
 std::array<float,16> view_projection{};
 std::array<float,2> frame{}; // Frame words2/3, not frame_index.
 bool camera{},valid{};
};
template<class Read> bool capture(uint64_t fs,uint64_t vs,Read&& read,Captured& out){
 out={};const auto* recipe=lookup(fs,vs);if(!recipe)return false;
 Captured value;value.camera=recipe->camera;
 if(!read(1u,1u,0u,value.material.data(),recipe->camera?16u:32u))return false;
 if(recipe->camera && (!read(0u,0u,8u,value.frame.data(),8u) ||
    !read(0u,2u,128u,value.view_projection.data(),64u)))return false;
 for(float x:value.material)if(!std::isfinite(x))return false;
 if(recipe->camera){
  for(float x:value.frame)if(!std::isfinite(x))return false;
  for(float x:value.view_projection)if(!std::isfinite(x))return false;
  if(value.frame[0]<0 || value.frame[0]>=16777216 || std::trunc(value.frame[0])!=value.frame[0])return false;
 }
 value.valid=true;out=value;return true;
}
using Vec3=std::array<float,3>;
inline float dot(Vec3 a,Vec3 b){return a[0]*b[0]+a[1]*b[1]+a[2]*b[2];}
inline bool normalize(Vec3 a,Vec3& b){
 float q=dot(a,a);if(!(q>0)||!std::isfinite(q))return false;
 float scale=1/std::sqrt(q);b={a[0]*scale,a[1]*scale,a[2]*scale};return true;
}
enum class Unknown:uint32_t {None,MissingState,Nonfinite,DegenerateFrame,SmoothStepOrder,PixelRange};
struct Scalar {float value{};Unknown unknown=Unknown::None;bool known()const{return unknown==Unknown::None;}};
inline Scalar angular(Vec3 view,Vec3 n,Vec3 t,Vec3 b,float power){
 for(auto v:{view,n,t,b})for(float x:v)if(!std::isfinite(x))return {0,Unknown::Nonfinite};
 if(!std::isfinite(power))return {0,Unknown::Nonfinite};
 Vec3 v,nn,local,world;
 if(!normalize(view,v)||!normalize(n,nn)||!normalize({dot(v,b),dot(v,n),dot(v,t)},local))
  return {0,Unknown::DegenerateFrame};
 if(!normalize({b[0]*local[0]+n[0]*local[1]+t[0]*local[2],
                b[1]*local[0]+n[1]*local[1]+t[1]*local[2],
                b[2]*local[0]+n[2]*local[1]+t[2]*local[2]},world))return {0,Unknown::DegenerateFrame};
 const float base=std::max(std::abs(1-std::abs(dot(world,nn))),9.9999997473787516355514526367188e-05f);
 const float result=std::clamp((1-std::pow(base,power))*5,0.0f,1.0f);
 if(!std::isfinite(result))return {0,Unknown::Nonfinite};
 return {result};
}
inline Scalar coverage(const Captured& c,float alpha,float blue,Vec3 view,Vec3 n,Vec3 t,Vec3 b){
 if(!c.valid)return {0,Unknown::MissingState};
 if(!std::isfinite(alpha)||!std::isfinite(blue))return {0,Unknown::Nonfinite};
 const float lo=c.material[1],hi=c.material[2];
 // The native instruction is SmoothStep(lo,hi,B), without sorting its edges.
 // Catalog defaults are reversed. Those live values/driver semantics have NOT
 // been proven by retained F10 packets: withhold a decision instead of silently
 // substituting an inverted/sorted ramp. Equal edges are likewise unsupported.
 if(!(lo<hi))return {0,Unknown::SmoothStepOrder};
 const float range=hi-lo;if(!std::isfinite(range))return {0,Unknown::Nonfinite};
 auto a=angular(view,n,t,b,c.material[3]);if(!a.known())return a;
 const float x=std::clamp((blue-lo)/range,0.0f,1.0f);
 const float smooth=(x*x)*(3-2*x);
 const float value=(alpha*smooth)*a.value;
 if(!std::isfinite(value))return {0,Unknown::Nonfinite};
 return {value};
}
struct Decision {bool survives{};Unknown unknown=Unknown::None;bool known()const{return unknown==Unknown::None;}};
inline Decision shadow(const Captured& c,Scalar cov){
 if(!c.valid || c.camera)return {false,Unknown::MissingState};
 if(!cov.known())return {false,cov.unknown};
 if(!std::isfinite(cov.value)||!std::isfinite(c.material[4]))return {false,Unknown::Nonfinite};
 const float hard=cov.value<0.33329999446868896484375f?0.0f:1.0f;
 return {!(std::fma(hard,c.material[4],-0.3333300054073333740234375f)<0)};
}
inline float fract(float x){return x-std::floor(x);}
inline Decision camera(const Captured& c,Scalar cov,float uv_x,float frag_x,float frag_y){
 if(!c.valid || !c.camera)return {false,Unknown::MissingState};
 if(!cov.known())return {false,cov.unknown};
 if(!std::isfinite(cov.value)||!std::isfinite(uv_x)||!std::isfinite(frag_x)||!std::isfinite(frag_y))return {false,Unknown::Nonfinite};
 if(frag_x<0 || frag_y<0 || frag_x>=2147483648.0f || frag_y>=2147483648.0f)return {false,Unknown::PixelRange};
 if(!std::isfinite(c.frame[0])||!std::isfinite(c.frame[1])||c.frame[0]<0 || c.frame[0]>=16777216 || std::trunc(c.frame[0])!=c.frame[0])return {false,Unknown::MissingState};
 static constexpr float phase[8]={0,.5f,.25f,.75f,.125f,.625f,.375f,.875f};
 const float index=c.frame[0]-8*std::trunc(c.frame[0]/8);
 const float seed=0.067110560834407806396484375f*float(int32_t(frag_x))+
                  0.005837149918079376220703125f*float(int32_t(frag_y));
 const float noise=fract(52.98291778564453125f*fract(seed));
 const float threshold=fract(std::fma(phase[uint32_t(index)],c.frame[1],std::fma(uv_x,1.16180002689361572265625f,noise)));
 if(!std::isfinite(threshold))return {false,Unknown::Nonfinite};
 return {cov.value>=threshold};
}
} // namespace bg3::native_fur
