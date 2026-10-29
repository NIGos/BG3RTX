#undef NDEBUG
#include "../common/ufloat_interval.h"
#include <cassert>
#include <limits>
#include <iostream>

// Independent mathematical decoder, exhaustive across BOTH finite codebooks.
static float reference(unsigned code, unsigned m) {
  unsigned e=code>>m, f=code% (1u<<m);
  return e ? std::ldexp(1.0f+float(f)/float(1u<<m),int(e)-15) : std::ldexp(float(f),-14-int(m));
}
int main() {
  unsigned checks=0;
  for(unsigned m:{5u,6u}) {
    unsigned count=31u<<m;
    for(unsigned c=0;c<count;++c) {
      float v=reference(c,m);
      assert(bg3::uf_decode(c,m)==v);
      auto b=bg3::uf_interval(v,m);
      assert(b.status==(c==count-1?2u:1u));
      assert(b.lower==(c?reference(c-1,m):0.0f));
      if(c==count-1) { assert(!bg3::uf_contains(b,v,v)); continue; }
      assert(b.upper==reference(c+1,m));
      assert(bg3::uf_contains(b,v,v));
      assert(!bg3::uf_contains(b,v,b.upper));
      assert(bg3::uf_contains(b,v,std::nextafter(b.upper,v)));
      assert(!bg3::uf_contains(b,v,std::nextafter(b.upper,std::numeric_limits<float>::infinity())));
      if(c) {
        assert(!bg3::uf_contains(b,v,b.lower));
        assert(bg3::uf_contains(b,v,std::nextafter(b.lower,v)));
        assert(!bg3::uf_contains(b,v,std::nextafter(b.lower,-1.0f)));
      }
      // A midpoint is not a value that could have been loaded from this format.
      assert(bg3::uf_interval((v+b.upper)*0.5f,m).status==0);
      checks++;
    }
    for(float x:{-1.0f,std::numeric_limits<float>::infinity(),std::numeric_limits<float>::quiet_NaN(),65536.0f})
      assert(bg3::uf_interval(x,m).status==0);
    auto z=bg3::uf_interval(0.0f,m);
    assert(!bg3::uf_contains(z,0.0f,-std::numeric_limits<float>::min()));
    assert(bg3::uf_interval(reference(1,m)*0.5f,m).status==0);
  }
  std::cout<<checks<<" bounded codewords checked, both finite maxima inconclusive\n";
}
