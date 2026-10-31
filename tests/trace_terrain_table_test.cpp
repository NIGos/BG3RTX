#include "../common/trace_terrain_table.h"
#include <cstdio>
#include <cstdlib>
#include <initializer_list>
using namespace bg3::trace_terrain;
int main(){
 unsigned checks=0;auto check=[&](bool value){++checks;if(!value)std::abort();};
 constexpr uint64_t vs=0x64e2568916ede3d6ull,fs=0x6650c75f3d3e4968ull;
 for(unsigned mask=0;mask<32;++mask){
  bool graphics=mask&1,tracing=mask&2,request=mask&4,vertex=mask&8,fragment=mask&16;
  check(requested(graphics,tracing,request,vertex?vs:0,fragment?fs:0)==(mask==31));
 }
 for(uint32_t count:{0u,1u,31u,32u,33u,388u,4095u,4096u,4097u,UINT32_MAX}){
  auto r=range(true,count);check(r.declared==count);check(r.emitted<=table_cap);
  check(uint64_t(r.emitted)+r.truncated==count);
  auto unknown=range(false,count);check(!unknown.declared&&!unknown.emitted&&!unknown.truncated);
 }
 auto actual=range(true,388);check(actual.emitted==388 && !actual.truncated);
 for(uint32_t required:{63u,64u,65u,98u,99u,100u,208u,209u,210u,383u,384u,385u})check(required<actual.emitted);
 std::printf("terrain table trace: %u CPU selection/range checks; no GPU\n",checks);
}
