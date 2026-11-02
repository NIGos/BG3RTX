#include "../common/perf_hook_state.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
using namespace bg3::perf;
static void check(bool b) { if(!b) { std::fprintf(stderr,"perf hook state check failed\n"); std::abort(); } }
int main() {
  GenerationCache<uint64_t, int> cache;
  int loads=0;
  auto load=[&] { return ++loads; };
  check(cache.get(17, 1, load)==1);
  check(cache.get(17, 1, load)==1); // immutable hit
  check(cache.get(18, 1, load)==2); // pipeline switch
  check(cache.get(18, 2, load)==3); // destroy/recreate with the same handle
  check(cache.get(18, 3, load)==4); // cached rejection must also expire
  check(cache.get(17, 3, load)==5); // value owns no CmdState, reset/free cannot dangle
  alignas(16) float source[24]={1,0,0,2,0,1,0,3,0,0,1,4, 1,0,0,5,0,1,0,6,0,0,1,7};
  auto key=[&](int offset) { MatrixSource s; for(int k=0;k<3;++k)s.rows[k]=reinterpret_cast<const uint8_t*>(source+offset+4*k);return s; };
  int reads=0;
  auto read=[&](float* dst,const uint8_t* src) { ++reads;std::memcpy(dst,src,16); };
  SubmitMatrices submit;
  auto first=submit.get(key(0),read);
  check(first.valid && first.rows[3]==2 && reads==3);
  check(submit.get(key(0),read).valid && reads==3); // duplicate destination
  check(submit.get(key(12),read).rows[3]==5 && reads==6); // overlapping fixup counts
  auto changed=key(0);changed.rows[2]=key(12).rows[2];
  check(submit.get(changed,read).rows[11]==7 && reads==9); // all three ranges form the key
  source[3]=9;
  SubmitMatrices next_submit;
  check(next_submit.get(key(0),read).rows[3]==9 && reads==12); // late writes, no cross-submit cache
  float copy[12];std::memcpy(copy,first.rows.data(),48);
  copy[0]=std::numeric_limits<float>::quiet_NaN();check(!valid_matrix(copy));
  copy[0]=std::numeric_limits<float>::infinity();check(!valid_matrix(copy));
  copy[0]=1e7f;check(!valid_matrix(copy));
  copy[0]=0;check(!valid_matrix(copy)); // singular
  source[0]=0;
  SubmitMatrices invalid;
  auto bad=invalid.get(key(0),read);check(!bad.valid);
  auto raw=bad.rows;
  check(invalid.get(key(0),read).rows==raw); // job scatter cannot poison instance scatter
  check(submit.size()==3);
  uint8_t instance[64],job[64];
  std::memset(instance,0x5A,sizeof instance);std::memset(job,0x5A,sizeof job);
  scatter_matrix(job,bad.rows.data(),bad.valid,false);
  scatter_matrix(instance,bad.rows.data(),bad.valid,true);
  float invalid_job;std::memcpy(&invalid_job,job,4);check(std::isnan(invalid_job));
  check(instance[51]==0 && std::memcmp(instance,bad.rows.data(),48)==0);
  check(instance[48]==0x5A && instance[50]==0x5A && instance[52]==0x5A && job[48]==0x5A);
  scatter_matrix(instance,first.rows.data(),true,true);
  scatter_matrix(job,first.rows.data(),true,false);
  check(instance[51]==0xFF && std::memcmp(instance,first.rows.data(),48)==0);
  check(std::memcmp(job,first.rows.data(),48)==0 && job[51]==0x5A);
  const int invalid_reads=reads;
  check(!invalid.get(key(0),read).valid && reads==invalid_reads); // invalid sources dedup too
  std::puts("perf hook state tests passed");
}
