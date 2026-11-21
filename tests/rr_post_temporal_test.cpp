#include "../common/rr_post_temporal.h"
#include <cstdio>
#include <limits>
using namespace bg3::post_temporal;
int main() {
  int failures = 0;
  auto check = [&](bool ok, const char* name) { if (!ok) { std::printf("FAIL %s\n", name); ++failures; } };
  auto near = [](double a, double b) { return std::abs(a-b)<1e-11; };
  Value lo{}, hi{}, c{2,3,4,0.5,5,6,7}; lo.fill(-100); hi.fill(100);
  Value h=c;
  for (int i=0;i<1024;++i) h=accumulate(c,h,lo,hi,true);
  check(h==c,"constant energy invariant, all seven channels");
  h={}; double sum=0, square=0, chain=0, sumA=0, sumC=0, sumB=0;
  for (int i=0;i<4096;++i) {
    Value v{}; v[3]=i%2; v[0]=2*(1-v[3]); v[4]=4*v[3];
    h=accumulate(v,h,lo,hi,true);
    if(i>=2048) { sum+=h[3]; square+=h[3]*h[3];
      sumA+=h[3];sumC+=h[0];sumB+=h[4];chain+=h[3]*3+h[0]+h[4]; }
  }
  double mean=sum/2048, var=square/2048-mean*mean;
  check(near(mean,0.5),"50 percent flipping coverage converges to unbiased mean");
  check(near(var,0.25/(31*31)),"alternating EMA variance reduction is 961x");
  check(near(chain/2048,(sumA*3+sumC+sumB)/2048) && near(chain/2048,4.5),"affine reconstruction mean and linearity");
  // Independent random flips: stationary EMA variance alpha/(2-alpha) = 1/31.
  unsigned rng=73; h={}; sum=square=0;
  for(int i=0;i<300000;++i) { rng^=rng<<13;rng^=rng>>17;rng^=rng<<5;
    Value v{};v[3]=rng&1;h=accumulate(v,h,lo,hi,true);
    if(i>=10000){sum+=h[3];square+=h[3]*h[3];} }
  mean=sum/290000;var=square/290000-mean*mean;
  check(std::abs(mean-0.5)<0.004 && std::abs(var-0.25/31)<0.0004,"random EMA variance reduction is 31x");
  check(accumulate(c,h,lo,hi,false)==c,"disocclusion/cut/resolution reset uses fresh sample");
  h.fill(std::numeric_limits<double>::quiet_NaN());
  check(accumulate(c,h,lo,hi,true)==c,"nonfinite history recovers directly");
  h.fill(100);hi=c;
  check(accumulate(c,h,lo,hi,true)==c,"neighbourhood clamp bounds history");
  check(accepts(.5,.5,.5,{0,0},{0,0}),"static depth/motion accepted");
  check(!accepts(.6,.5,.5,{0,0},{0,0}),"disoccluded depth rejected");
  check(!accepts(.5,.5,.5,{0,0},{2,0}),"motion mismatch rejected at one-pixel footprint");
  check(accepts(.5+1.0/16777215,.5,.5,{0,0},{0,0}),"D24 rounding accepted");
  check(!accepts(.5,.5,.5,{NAN,0},{0,0}),"nonfinite motion rejected");
  {  // rr_post_jitter_align: a render-resolution step edge rendered with 16 Halton jitters (texel p holds the scene at
     // p + 0.5 - j, temporal_final's convention), read bilinearly at output position x. Key 1 is stationary, key 0
     // moves with the jitter, key 2 moves twice as far; the mean over the phases is the same for 0 and 1.
    check(jitter_align_sign(0)==0 && jitter_align_sign(1)==1 && jitter_align_sign(2)==-1 && jitter_align_sign(7)==0,
          "jitter align keys: 0 off, 1 plus, 2 minus, others off");
    auto halton = [](int i, int b) { double f=1, r=0; for (; i>0; i/=b) { f/=b; r+=f*(i%b); } return r; };
    auto scene = [](double s) { return std::clamp((s-8.0)/6.0, 0.0, 1.0); };   // linear over every tap read below
    auto texel = [&](int p, double j) { return scene(p+0.5-j); };
    auto read = [&](double u, double j) {   // bilinear at render-pixel position u (texel centres at p + 0.5)
      const double t=u-0.5; const int p=int(std::floor(t)); const double f=t-p;
      return texel(p,j)*(1-f)+texel(p+1,j)*f; };
    double spread[3]={}, mean[3]={};
    for (int key=0; key<3; ++key) {
      double lo=1e9, hi=-1e9;
      for (int i=0;i<16;++i) {
        const double j=halton(i,2)+1.0/32-0.5;   // the 16 phases, centred: mean jitter 0
        const double v=read(align_lookup(10.5,j,key),j);
        lo=std::min(lo,v); hi=std::max(hi,v); mean[key]+=v/16;
      }
      spread[key]=hi-lo;
    }
    check(spread[1]<1e-12,"aligned lookup is stationary across the 16 jitter phases");
    check(spread[0]>0.1 && spread[2]>1.9*spread[0],"unaligned lookup moves with the jitter, the wrong sign twice as far");
    check(near(mean[0],mean[1]),"alignment keeps the mean over the jitter cycle");
  }
  std::printf("post temporal CPU reference: %s; alternating mean 0.5, variance 1/3844, iid variance 1/124\n",failures?"FAIL":"PASS");
  return failures?1:0;
}
