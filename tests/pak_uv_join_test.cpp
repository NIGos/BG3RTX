#include "../common/pak_uv_join.h"
#include "../common/mesh_paks.h"
#include <cstdio>
#include <cstdlib>
using namespace bg3::mesh_paks;
#define CHECK(x) do{if(!(x)){std::printf("FAIL %d: %s\n",__LINE__,#x);std::exit(1);}}while(0)
#include "pak_witness_fixture.h"
int main(){
  witness_fixture::decoders_and_budget();
  std::array<uint64_t,32> off{};
  CHECK(!uv_join::offsets(0,24,0,off));CHECK(!uv_join::offsets(4,24,96,off));
  CHECK(!uv_join::offsets(3,23,69,off));CHECK(!uv_join::offsets(3,24,71,off));
  CHECK(uv_join::offsets(3,24,72,off));CHECK(off[0]==16&&off[31]==64);
  CHECK(uv_join::offsets(3000000,64,192000000,off));
  for(unsigned s=0;s<32;++s)CHECK(off[s]==(uint64_t(s)*2999999/31)*64+16&&off[s]+8<=192000000);
  uv_join::Readback rb;std::array<float,64> uv{};
  CHECK(rb.begin());CHECK(!rb.begin());CHECK(!rb.finish(false,true,uv.data()));
  CHECK(rb.state==uv_join::State::Pending);CHECK(rb.finish(true,true,uv.data()));
  CHECK(rb.state==uv_join::State::Ready);CHECK(!rb.begin());CHECK(!rb.finish(true,true,uv.data()));
  rb={};CHECK(rb.begin());CHECK(rb.finish(true,false,uv.data()));CHECK(rb.state==uv_join::State::Rejected);
  rb={};CHECK(rb.begin());uv[63]=NAN;CHECK(rb.finish(true,true,uv.data()));CHECK(rb.state==uv_join::State::Rejected);
  rb={};CHECK(rb.begin());uv[63]=0;CHECK(rb.finish(true,true,uv.data()));CHECK(rb.state==uv_join::State::Ready);
  Row r;r.index_count=3;r.flags=4;std::vector<Row> rows{r};
  // Camp's ordinary STI pool pair must reach discovery even though it cannot
  // publish the HLOD fallback. This count/UV fixture used to be filtered out
  // before scheduling any readback, leaving attempts and draws at zero.
  CHECK(has_candidates(rows,3));CHECK(!has_candidates(rows,6));CHECK(!has_candidates(rows,0));
  CHECK(!has_candidates({},3));
  CHECK(!hlod_compatible(r,0x2d2ac3c38992b7f0ull,0x14678345c10aefbcull,true,false));
  CHECK(hlod_compatible(r,0x2d2ac3c38992b7f0ull,0xb0ecc292e8661561ull,true,false));
  CHECK(lookup(rows,3,nullptr,uv.data()).status==Match::Unique);
  rows.push_back(r);rows[1].corners={1};
  CHECK(lookup(rows,3,nullptr,uv.data()).status==Match::Ambiguous);
  std::puts("pak_uv_join: catalogue lookup, raw SHA witness decoder and budget passed");
}
