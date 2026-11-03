#pragma once
// Same 32 ordered-corner UV contract as native hair. This is a candidate key,
// never a replacement for full geometry verification before publication.
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
namespace bg3::mesh_paks::uv_join {
constexpr uint32_t kSamples=32, kBytes=kSamples*8;
struct Budget {
 static constexpr uint64_t kFrameBytes=4ull<<20,kFlightBytes=16ull<<20;static constexpr uint32_t kJobs=16;
 uint64_t frame=UINT64_MAX,bytes{};
 bool take(uint64_t now,uint64_t amount,uint32_t jobs,uint64_t pending){
  if(frame!=now){frame=now;bytes=0;}
  if(!amount||jobs>=kJobs||amount>kFrameBytes-bytes||pending>kFlightBytes||amount>kFlightBytes-pending)return false;
  bytes+=amount;return true;
 }
};
inline bool offsets(uint32_t count,uint32_t stride,uint64_t bytes,std::array<uint64_t,kSamples>& out) {
  if(!count || count%3 || stride<24 || uint64_t(count)*stride>bytes)return false;
  for(uint32_t s=0;s<kSamples;++s)out[s]=(uint64_t(s)*(count-1)/(kSamples-1))*stride+16;
  return true;
}
enum class State { Empty, Pending, Ready, Rejected };
struct Readback {
  State state=State::Empty;
  std::array<float,64> samples{};
  bool begin(){if(state!=State::Empty)return false;state=State::Pending;return true;}
  // An unsubmitted/reset recording never resolves to a candidate. A readback
  // is consumed once, after actual successful-submit retirement.
  bool finish(bool retired,bool submitted,const float* uv){
    if(state!=State::Pending || !retired)return false;
    state=State::Rejected;
    if(!submitted || !uv)return true;
    for(uint32_t c=0;c<64;++c)if(!std::isfinite(uv[c]))return true;
    for(uint32_t c=0;c<64;++c)samples[c]=uv[c];
    state=State::Ready;return true;
  }
};
}
