#include "../common/native_scalp_coverage.h"
#include "../common/native_scalp_capability.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <thread>

using namespace bg3::native_scalp;
static unsigned checks = 0;
static void check(bool ok) { ++checks; if (!ok) { std::fprintf(stderr, "check %u failed\n", checks); std::exit(1); } }
static long double sat(long double a) { return std::clamp(a, 0.0L, 1.0L); }
static long double frac(long double a) { return a - std::floor(a); }
// Independent higher precision GLSL reference, preserving source constants as
// float inputs. Comparison excludes the derived floating arithmetic envelope.
static long double coverage(Material m, Mask v) {
  long double e=m.edge, hi=sat(e+static_cast<long double>(0.7f));
  long double t=sat((static_cast<long double>(v.alpha)-e)/(hi-e));
  return (3*t*t-2*t*t*t)*(1+(static_cast<long double>(v.blue)-1)*m.root);
}
int main() {
  alignas(std::atomic_ref<uint32_t>::required_alignment) uint32_t capability=0;
  check(!has_capability(&capability)); // Fresh key-off session stays ordinary.
  check(!has_capability(nullptr));
  for (unsigned i=0;i<128;++i) {
    // A scene pointer publication represents the existing scene mutex handoff.
    // Acquiring a new scene before choosing the pipeline must see capability.
    std::atomic<bool> scene_ready=false;
    std::thread producer([&]{publish_capability(&capability);scene_ready.store(true,std::memory_order_release);});
    while(!scene_ready.load(std::memory_order_acquire))std::this_thread::yield();
    check(has_capability(&capability));
    producer.join();
    // A fresh core and key-off still read the SAME persistent word; retired
    // kind3 records cannot be reinterpreted by ordinary texture-alpha shaders.
    uint32_t* new_core_persist_pointer=&capability;
    check(has_capability(new_core_persist_pointer));
  }
  constexpr uint64_t depth=0x9e77575a9f377ec5ull, shadow=0x03a2d9f645625c44ull;
  const Frame no_dither{0,0,0,0,true,false};
  for (const auto& r : recipes) {
    check(lookup(r.hash)==&r);
    check(r.mask_binding==r.material_binding+1);
    auto a=evaluate(r.hash,{0,0,true},{1,1,true},no_dither);
    auto b=evaluate(r.hash,{0,0,true},{1,0,true},no_dither);
    check(a.status==Status::CompleteScalar && a.survives);
    check(b.status==Status::CompleteScalar && !b.survives);
    Captured c;
    unsigned calls=0;
    const bool captured=capture(r.hash,[&](unsigned set,unsigned binding,unsigned offset,void* dst,size_t size){
      ++calls;
      if(binding==2 && set==0) {check(offset==128 && size==64);float matrix[16]={1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};std::memcpy(dst,matrix,64);return true;}
      check(size==8);
      const float data[2]={0.25f,0.75f};
      if(set==1){check(binding==r.material_binding && offset==0);}
      else {check(set==0 && binding==0 && offset==8);}
      std::memcpy(dst,data,size);return true;
    },c);
    check(captured && c.material.available && c.material.edge==0.25f && c.material.root==0.75f);
    check(calls==(r.pass==Pass::CameraDepth?3u:1u));
    check(!c.frame.coordinates_available);
    check(c.projection_available==(r.pass==Pass::CameraDepth));
    if(c.projection_available)check(c.projection[0]==1 && c.projection[15]==1);
    check(c.frame.available==(r.pass==Pass::CameraDepth));
    // A failed read must clear prior output, never reuse another draw's state.
    check(!capture(r.hash,[](unsigned,unsigned,unsigned,void*,size_t){return false;},c));
    check(!c.material.available && !c.frame.available);
  }
  check(!lookup(0x5a03e5b678b2d040ull)); // Hair is NOT Scalp.
  for(uint64_t hash:{0x9e77575a9f377ec5ull,0x36eeb96ba83ea027ull}) {
    Captured c;
    auto reader=[&](bool nonfinite,unsigned set,unsigned binding,unsigned offset,void* dst,size_t bytes) {
      if(set==0 && binding==2) {
        check(offset==128 && bytes==64);
        if(!nonfinite)return false;
        float matrix[16]{};matrix[3]=std::numeric_limits<float>::quiet_NaN();std::memcpy(dst,matrix,64);return true;
      }
      check(bytes==8);const float data[2]={0.1f,1.0f};std::memcpy(dst,data,8);return true;
    };
    for(bool nonfinite:{false,true}) {
      c.material.available=true;c.projection_available=true;
      check(!capture(hash,[&](unsigned set,unsigned binding,unsigned offset,void* dst,size_t bytes){return reader(nonfinite,set,binding,offset,dst,bytes);},c));
      check(!c.material.available && !c.frame.available && !c.projection_available);
    }
  }
  check(evaluate(0,{},{}).status==Status::UnsupportedShader);
  check(evaluate(depth,{0,0,true},{1,1,true}).status==Status::MissingInputs);
  check(evaluate(shadow,{},{}).status==Status::MissingInputs);
  check(evaluate(shadow,{1,0,true},{1,1,true}).status==Status::UndefinedSmoothstep);
  check(evaluate(shadow,{0,0,true},{0,std::numeric_limits<float>::quiet_NaN(),true}).status==Status::Nonfinite);
  check(evaluate(depth,{0,0,true},{1,1,true},{0,1,0,0,true,false}).status==Status::MissingInputs);
  // Real counterexamples to raw A >= .3333. Root suppression alone can erase
  // a fully opaque mask; material smoothstep can reject alpha above raw cut.
  check(!evaluate(shadow,{0,1,true},{0,1,true}).survives);
  check(!evaluate(shadow,{0.6f,0,true},{1,0.5f,true}).survives);
  check(evaluate(shadow,{-0.4f,0,true},{1,0.2f,true}).survives);
  constexpr long double eps=std::numeric_limits<float>::epsilon();
  // 64 rounded ops bounds this well-conditioned grid (edge in [0,.6],
  // denominator >= .4). Factor16 bounds intermediates and division scaling.
  constexpr long double bound=(64*eps/(1-64*eps))*16;
  unsigned compared=0;
  for (int e=0;e<=6;++e) for (int root=0;root<=10;++root)
    for (int a=0;a<=32;++a) for (int b=0;b<=16;++b) {
      Material m{e/10.0f,root/10.0f,true}; Mask v{b/16.0f,a/32.0f,true};
      auto x=evaluate(shadow,m,v); const auto ref=coverage(m,v);
      check(x.status==Status::CompleteScalar);
      check(std::abs(x.coverage-ref)<=bound);
      if (std::abs(ref-static_cast<long double>(0.3333f))>bound) { check(x.survives==(ref>=0.3333f)); ++compared; }
      auto d=evaluate(depth,m,v,no_dither);check(d.survives==x.survives);
    }
  // Reference original raster coordinates and native frame phase, including
  // negative phase (truncation remainder). No stochastic RT hash substitutes.
  unsigned dither_compared=0;
  for (int ph=-3;ph<=4;++ph) for (int y=0;y<8;++y) for (int x=0;x<8;++x)
    for (int a=1;a<8;++a) {
      Material m{0,0,true};Mask v{1,a/8.0f,true};Frame f{float(ph),1,x+0.5f,y+0.5f,true,true};
      long double xx=f.frag_x,yy=f.frag_y;
      long double n1=((yy*2+xx-1.5L)+(ph-4*std::trunc(ph/4.0L)))*static_cast<long double>(0.2f);
      long double n2=xx*static_cast<long double>(2.4084f)+yy*static_cast<long double>(3.2535f);
      // fract discontinuities need their own guard. This bound exceeds 64-op
      // gamma times every intermediate magnitude for coordinates <=7.5.
      constexpr long double dbound=(64*eps/(1-64*eps))*128;
      auto near_integer=[](long double t){return std::min(frac(t),1-frac(t));};
      if(near_integer(n1)<=dbound || near_integer(n2)<=dbound)continue;
      long double dot=static_cast<long double>(0.833f)*frac(n1)+static_cast<long double>(0.1666f)*frac(n2);
      if(near_integer(dot)<=dbound)continue;
      long double gate=coverage(m,v)+frac(dot)-static_cast<long double>(0.6667f)-static_cast<long double>(0.3333f);
      if(std::abs(gate)<=dbound)continue;
      auto actual=evaluate(depth,m,v,f);check(actual.status==Status::CompleteScalar);check(actual.survives==(gate>=0));++dither_compared;
    }
  check(compared>40000);check(dither_compared>1000);
  std::printf("%u checks; %u shadow / %u dither reference decisions\n",checks,compared,dither_compared);
}
