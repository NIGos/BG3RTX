#include "../common/native_terrain_surface.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <cstdio>
#include <limits>
#include <array>
#include <algorithm>
using namespace bg3::native_terrain;
constexpr long double U=0x1p-24L;
long double gamma(int n) {return n*U/(1-n*U);}
void close(float got,long double expected,long double absolute_operations,int operations=16) {
    assert(std::isfinite(got));assert(std::abs((long double)got-expected)<=gamma(operations)*absolute_operations+std::numeric_limits<float>::denorm_min());
}
int main() {
    int checks=0;
    NttInputEvidence unknown={0,0,0,0};assert(ntt_input_status(unknown)==NttUnknownInputs);
    unknown={1,1,1,0};assert(ntt_input_status(unknown)==NttUnknownInputs);checks+=2;
    // Analytic native axis planes/signs, not a mirrored implementation oracle.
    struct Axis {Ntt3 rgb;float u,v,o;unsigned rotate;};
    const Axis axes[]={{{.5f,1,.5f},3,-7,1,0},{{1,.5f,.5f},7,-2,1,1},{{.5f,.5f,1},-3,-2,-1,0},{{.5f,.5f,0},3,-2,1,0}};
    for(auto a:axes) {auto r=ntt_project(3,7,2,a.rgb);assert(r.status==NttOk&&r.uv.x==a.u&&r.uv.y==a.v&&r.orientation==a.o&&r.rotate_xy==a.rotate);++checks;}
    assert(ntt_project(0,0,0,{.5f,.5f,.5f}).status==NttUndefined);
    assert(ntt_project(INFINITY,0,0,{.5f,1,.5f}).status==NttInvalid);checks+=2;
    // Actual authored contrast1/scale/offset tuples as EXPLICIT native-member
    // arithmetic cases only. This does not prove those values were uploaded.
    for(auto so:std::array<std::array<float,2>,4>{{{5,5},{5,10},{3.5f,0},{3.5f,0}}})
      for(int m=0;m<=16;++m) for(int a=0;a<=16;++a) {
        float mask=m/16.f,alpha=a/16.f;auto r=ntt_height_score(mask,alpha,1,so[0],so[1],0);
        assert(r.status==NttOk);
        long double h=(m==0||a+m<=16)?0:(long double)mask*alpha*so[0];
        if(h>0.009999999776482582L)h+=so[1];
        long double expected=h>0.009999999776482582L?h:-1;
        close(r.value,expected,std::abs(expected)+std::abs(so[1])+1);++checks;
      }
    assert(ntt_height_score(1,0,0,1,0,-2).status==NttUndefined);
    assert(ntt_height_score(1,1,1,std::numeric_limits<float>::max(),std::numeric_limits<float>::max(),0).status==NttUndefined);checks+=2;
    auto none=ntt_blend({-1,-1,-1,-1},{0,0,0,0},.1f);assert(none.status==NttOk&&none.base==1&&none.painted.x==0);++checks;
    // Equal competing heights have symmetric weights independently of width.
    for(int j=1;j<100;++j) {auto r=ntt_blend({1,1,0,0},{1,1,0,0},j/100.f);assert(r.status==NttOk&&r.painted.x==.5f&&r.painted.y==.5f&&r.base==0);++checks;}
    // Independent conservation and invariance to relabeling equal-score layers.
    for(int j=1;j<=64;++j) {
      float m=j/64.f;auto r=ntt_blend({2,2,2,2},{m,m,m,m},.25f);
      assert(r.status==NttOk);assert(r.painted.x==.25f&&r.painted.y==.25f&&r.painted.z==.25f&&r.painted.w==.25f&&r.base==0);++checks;
    }
    assert(ntt_blend({0,0,0,0},{NAN,0,0,0},.1f).status==NttInvalid);
    assert(ntt_blend({1,0,0,0},{std::numeric_limits<float>::denorm_min(),0,0,0},.1f).status==NttUndefined);checks+=2;
    // Curve factor2 analytic result and zero-curve native first-branch rule.
    for(int j=0;j<=32;++j) {
      float b=j/32.f;auto r=ntt_base_curves(b,{1,1},{1,1});assert(r.status==NttOk);
      close(r.weights.x,(long double)b/3,b,3);close(r.weights.y,2*(long double)b/3,2*b,3);
      auto z=ntt_base_curves(b,{0,0},{0,0});assert(z.status==NttOk&&z.weights.x==b&&z.weights.y==0);++checks;
    }
    assert(ntt_base_curves(1,{-1,0},{1,1}).status==NttInvalid);++checks;
    std::printf("native terrain surface: %d analytic/domain checks PASS; no native upload/sampling certificate\n",checks);
}
