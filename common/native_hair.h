#pragma once
// Native CHAR_Hair DEP/DEPS/DEPST recipes, verified packaged FS hashes.
// Other passes are inventoried, not reinterpreted as these material layouts.
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace bg3::native_hair {
inline constexpr uint32_t stride = 88;
struct Recipe { uint64_t hash; uint8_t material, noise, mask; bool camera; };
inline constexpr Recipe recipes[] = {
 {0x5a03e5b678b2d040ull,1,3,4,true}, {0x0527bb148e1d6c03ull,0,2,3,true},
 {0x34951669ed4b3b0full,1,3,4,false}, {0x00a9ecbb9c2b4ff8ull,1,3,4,false},
 {0x8dddc0bc49e56efcull,0,2,3,false}, {0xa39086306bfc9c03ull,0,2,3,false},
};
inline const Recipe* lookup(uint64_t hash) { for (const auto& r:recipes) if(r.hash==hash)return &r;return nullptr; }
struct Captured {
 std::array<float,12> material{}; // Canonical camera ordering; shadow root/density ->8/9.
 std::array<float,16> view_projection{};
 std::array<float,2> frame{}, near_far{};
 float orthographic{};
 bool valid{};
};
template<class Read> bool capture(uint64_t hash, Read&& read, Captured& out) {
 out={};const auto* r=lookup(hash);if(!r)return false;
 Captured v;
 if(!read(1u,r->material,0u,v.material.data(),r->camera?48u:32u))return false;
 if(!r->camera){v.material[8]=v.material[4];v.material[9]=v.material[5];v.material[4]=v.material[5]=0;}
 else {
  float projection_type{};
  if(!read(0u,0u,8u,v.frame.data(),8u) || !read(0u,2u,128u,v.view_projection.data(),64u) ||
     !read(0u,2u,192u,v.near_far.data(),8u) || !read(0u,2u,124u,&projection_type,4u))return false;
  if(!std::isfinite(projection_type))return false;
  v.orthographic=projection_type==1.0f?1.0f:0.0f;
 }
 for(float x:v.material)if(!std::isfinite(x))return false;
 for(float x:v.view_projection)if(!std::isfinite(x))return false;
 for(float x:v.frame)if(!std::isfinite(x))return false;
 for(float x:v.near_far)if(!std::isfinite(x))return false;
 if(r->camera && v.near_far[0]==v.near_far[1])return false;
 v.valid=true;out=v;return true;
}
using Vec3=std::array<float,3>;
using Vec4=std::array<float,4>;
inline float dot(Vec3 a,Vec3 b){return a[0]*b[0]+a[1]*b[1]+a[2]*b[2];}
inline Vec3 scale(Vec3 a,float x){return {a[0]*x,a[1]*x,a[2]*x};}
inline Vec3 plus(Vec3 a,Vec3 b){return {a[0]+b[0],a[1]+b[1],a[2]+b[2]};}
inline bool normalize(Vec3 in,Vec3& out){float q=dot(in,in);if(!(q>0)||!std::isfinite(q))return false;out=scale(in,1/std::sqrt(q));return true;}
inline float sign(float v){return v>0?1.0f:v<0?-1.0f:0.0f;}
inline Vec4 combine_masks(Vec4 a,Vec4 b){
 Vec4 out{};for(unsigned i=0;i<4;++i){const float center=i==0?0.5f:0.0f;float x=a[i]-center,y=b[i]-center;
  out[i]=std::fma(std::max(sign(x),sign(y)),std::max(std::abs(x),std::abs(y)),center);}
 return out;
}
// Scalar oracle/helper for the actual strand-frame expression. Do not assume
// interpolated N/T/B remain orthogonal. Flipping N for FrontFacing leaves this
// absolute result invariant, including non-orthogonal finite strand frames.
inline bool angular(Vec3 view,Vec3 n,Vec3 t,Vec3 b,float& value){
 Vec3 v,l,w,nn;if(!normalize(view,v)||!normalize(n,nn))return false;
 if(!normalize({dot(v,b),dot(v,n),dot(v,t)},l))return false;
 if(!normalize(plus(plus(scale(b,l[0]),scale(n,l[1])),scale(t,l[2])),w))return false;
 value=std::clamp(std::abs(dot(w,nn))*5.0f,0.0f,1.0f);return std::isfinite(value);
}
inline float coverage(Vec4 mask,float root,float density,float angular_factor){
 return ((mask[3]*std::fma(-mask[2],root,1.0f))*angular_factor)*density;
}
} // namespace bg3::native_hair
