// Decode exactly the catalogue's ordered object-position/UV byte witness.
#include "../layer/shader_cache.h"
namespace witness_fixture {
void decoders_and_budget(){
 using namespace bg3::mesh_paks;
 for(uint32_t width:{1u,2u,4u})for(bool half:{false,true}){
  // Nonzero vertex base, padded raw streams, repeated/reordered indices.
  const uint32_t order[6]={4,2,3,4,3,2};
  std::vector<uint8_t> indices(6*width),positions(3*20,0xcd),uv(3*12,0xcd);
  for(unsigned c=0;c<6;++c)std::memcpy(indices.data()+c*width,order+c,width);
  std::vector<float> expected;
  for(unsigned v=0;v<3;++v){const float p[3]={float(v),float(v+1),-float(v)};
   const float tex[2]={0.5f,0.25f};const uint16_t h[2]={0x3800,0x3400};
   std::memcpy(positions.data()+v*20,p,12);std::memcpy(uv.data()+v*12,half?static_cast<const void*>(h):tex,half?4:8);
  }
  for(uint32_t i:order){const unsigned v=i-2;expected.insert(expected.end(),{float(v),float(v+1),-float(v),0.5f,0.25f});}
  WitnessLayout l{6,width,3,20,12,half};uint32_t first{},last{};
  bg3::mesh_content::Bytes ix{indices.data(),indices.size()},p{positions.data(),positions.size()},u{uv.data(),uv.size()};
  CHECK(witness_span(l,ix,first,last)&&first==5&&last==7);
  std::vector<float> decoded;CHECK(decode_witness(l,ix,p,u,first,decoded)&&decoded==expected);
  Row row;row.index_count=6;row.compact=true;row.geometry_digest=bg3::shader_cache::sha256_hex(expected.data(),expected.size()*4);
  CHECK(witness_match(row,decoded,bg3::shader_cache::sha256_hex));CHECK(!witness_match(row,decoded));
  decoded[5]+=1;CHECK(!witness_match(row,decoded,bg3::shader_cache::sha256_hex));
  auto short_p=p;short_p.size=2*20+11;CHECK(!decode_witness(l,ix,short_p,u,first,decoded));
  auto short_u=u;short_u.size=2*12+(half?3:7);CHECK(!decode_witness(l,ix,p,short_u,first,decoded));
  auto short_ix=ix;--short_ix.size;CHECK(!witness_span(l,short_ix,first,last));
  CHECK(!decode_witness(l,ix,p,u,6,decoded));
  const float nan=std::numeric_limits<float>::quiet_NaN();std::memcpy(positions.data(),&nan,4);
  CHECK(!decode_witness(l,ix,p,u,5,decoded)&&decoded.empty());
  l.vertex_offset=INT64_MAX;CHECK(!witness_span(l,ix,first,last));
  l.vertex_offset=-5;CHECK(!witness_span(l,ix,first,last));
 }
 CHECK(witness_half(1)==std::ldexp(1.f,-24));CHECK(witness_half(0x8000)==0.f&&std::signbit(witness_half(0x8000)));
 CHECK(witness_half(0x3c00)==1.f&&witness_half(0xc000)==-2.f);
 CHECK(std::isinf(witness_half(0x7c00))&&std::isnan(witness_half(0x7e00)));
 uv_join::Budget b;using B=uv_join::Budget;
 CHECK(!b.take(1,0,0,0));CHECK(b.take(1,B::kFrameBytes,0,0));
 CHECK(!b.take(1,1,0,0));CHECK(b.take(2,1,0,0)); // next frame retries
 CHECK(!b.take(3,1,B::kJobs,0));CHECK(b.take(3,1,B::kJobs-1,0));
 CHECK(!b.take(4,2,0,B::kFlightBytes-1));CHECK(b.take(4,1,0,B::kFlightBytes-1));
 CHECK(!b.take(5,1,0,UINT64_MAX));CHECK(!b.take(6,B::kFrameBytes+1,0,0));
}
}
