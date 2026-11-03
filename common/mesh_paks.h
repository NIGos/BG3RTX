// Offline pak mesh candidate table. A sampled match is never a qualified GPU row.
#pragma once
#include "../layer/mesh_content_id.h"
#include <cmath>
#include <span>
#include <string>
#include <vector>
namespace bg3::mesh_paks {
constexpr size_t kRowBytes = 840;
struct Row {
  uint64_t content_id{};
  uint32_t index_count{}, vertex_span{}, flags{};
  mesh_content::Sample sample;
  std::array<float,64> uv{};
  std::vector<float> corners; // v2: full ordered position3/UV2 stream
  std::string material;
};
inline bool uuid(const std::string& s) {
  if(s.size()!=36)return false;
  for(size_t i=0;i<s.size();++i) {
    if(i==8||i==13||i==18||i==23){if(s[i]!='-')return false;}
    else if(!((s[i]>='0'&&s[i]<='9')||(s[i]>='a'&&s[i]<='f')))return false;
  }
  return true;
}
// Transactional parser: malformed/truncated/oversized tables publish nothing.
inline bool parse(std::span<const uint8_t> bytes,std::vector<Row>& rows) {
  rows.clear();
  if(bytes.size()<16 || std::memcmp(bytes.data(),"B3PAKS1\0",8))return false;
  auto u32=[&](size_t at){uint32_t v{};for(unsigned i=0;i<4;++i)v|=uint32_t(bytes[at+i])<<(i*8);return v;};
  const uint32_t n=u32(12);
  const uint32_t version=u32(8);
  if((version!=1&&version!=2) || n>1000000 || (version==1&&bytes.size()!=16+uint64_t(n)*kRowBytes))return false;
  if(n>(bytes.size()-16)/(version==2?kRowBytes+4+60:kRowBytes))return false;
  std::vector<Row> parsed;parsed.reserve(n);
  size_t next=16;
  for(uint32_t i=0;i<n;++i) {
    const size_t at=next;Row r;
    if(at>bytes.size()||bytes.size()-at<kRowBytes)return false;
    next+=kRowBytes;
    r.content_id=uint64_t(u32(at))|(uint64_t(u32(at+4))<<32);
    r.index_count=u32(at+8);r.vertex_span=u32(at+12);r.flags=u32(at+16);
    if(!r.index_count||r.index_count%3||!r.vertex_span||(r.flags&~7u)||u32(at+20))return false;
    for(size_t w=0;w<r.sample.words.size();++w)r.sample.words[w]=u32(at+24+w*4);
    if(r.sample.words[0]!=1||r.sample.words[1]!=r.vertex_span||r.sample.words[2]!=r.index_count||mesh_content::hash(r.sample)!=r.content_id)return false;
    for(size_t s=0;s<32;++s){const auto* w=r.sample.words.data()+3+s*4;
      if(w[0]>=r.vertex_span)return false;
      for(unsigned c=1;c<4;++c)if((w[c]&0x7f800000u)==0x7f800000u)return false;
    }
    for(size_t c=0;c<64;++c){const uint32_t v=u32(at+548+c*4);std::memcpy(&r.uv[c],&v,4);if(!std::isfinite(r.uv[c]))return false;}
    r.material.assign(reinterpret_cast<const char*>(bytes.data()+at+804),36);
    if(!uuid(r.material))return false;
    if(version==2){
      if(bytes.size()-next<4)return false;const auto count=u32(next);next+=4;
      if(count!=r.index_count||uint64_t(count)*20>bytes.size()-next)return false;
      r.corners.resize(size_t(count)*5);
      for(size_t c=0;c<r.corners.size();++c){const uint32_t v=u32(next+c*4);std::memcpy(&r.corners[c],&v,4);if(!std::isfinite(r.corners[c]))return false;}
      next+=size_t(count)*20;
    }
    parsed.push_back(std::move(r));
  }
  if(next!=bytes.size())return false;
  rows=std::move(parsed);return true;
}
// Full equality, not the sampled hash, authorizes a captured-resource fallback.
// Positions retain GR2 float bits; UV tolerance is the groom contract.
inline bool full_match(const Row& row,const mesh_content::Input& in,mesh_content::Bytes uv,uint32_t uv_stride) {
  mesh_content::Sample s;
  if(!mesh_content::sample(in,s)||!(s==row.sample)||in.count!=row.index_count||row.corners.size()!=size_t(in.count)*5||!uv.data||uv_stride<8)return false;
  for(uint32_t c=0;c<in.count;++c){
    uint32_t ix=c;
    if(in.index_bytes==1)ix=in.indices.data[c];
    else if(in.index_bytes==2){uint16_t v;std::memcpy(&v,in.indices.data+size_t(c)*2,2);ix=v;}
    else if(in.index_bytes==4)std::memcpy(&ix,in.indices.data+size_t(c)*4,4);
    const int64_t v=in.vertex_offset+ix;if(v<0||uint64_t(v)>UINT64_MAX/uv_stride)return false;
    const uint64_t off=uint64_t(v)*uv_stride;
    if(off>uv.size||uv.size-off<8)return false;
    const auto* expected=row.corners.data()+size_t(c)*5;
    if(std::memcmp(in.positions.data+uint64_t(v)*in.stride,expected,12))return false;
    float got[2];std::memcpy(got,uv.data+off,8);
    for(unsigned k=0;k<2;++k)if(!std::isfinite(got[k])||std::abs(got[k]-expected[3+k])>1e-3f)return false;
  }
  return true;
}
inline bool hlod_compatible(const Row& row,uint64_t vs,uint64_t fs,bool cull_known,bool two_sided) {
  return vs==0x2d2ac3c38992b7f0ull&&fs==0xb0ecc292e8661561ull&&
    (row.flags&4u)&&!(row.flags&2u)&&cull_known&&two_sided==bool(row.flags&1u);
}
enum class Match { Missing, Unique, Ambiguous, Invalid };
struct Result {Match status=Match::Missing;const Row* row{};};
inline Result lookup(std::span<const Row> rows,uint32_t count,const mesh_content::Sample* sample,
                     const float* uv) {
  if(!count||(!sample&&!uv))return {Match::Invalid,{}};
  if(uv)for(size_t c=0;c<64;++c)if(!std::isfinite(uv[c]))return {Match::Invalid,{}};
  const uint64_t id=sample?mesh_content::hash(*sample):0;Result out;
  for(const auto& r:rows){
    if(r.index_count!=count || (sample&&(r.content_id!=id||!(r.sample==*sample))))continue;
    bool same=true;if(uv)for(size_t c=0;c<64;++c)if(std::abs(uv[c]-r.uv[c])>1e-3f){same=false;break;}
    if(!same)continue;
    // Even equal UUIDs with different geometry/UV candidates remain disputed.
    if(out.row && (out.row->material!=r.material||out.row->flags!=r.flags||!(out.row->sample==r.sample)||out.row->uv!=r.uv))return {Match::Ambiguous,{}};
    out={Match::Unique,&r};
  }
  return out;
}
}
