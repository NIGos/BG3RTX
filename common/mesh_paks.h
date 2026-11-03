// Offline pak mesh candidate table. A sampled match is never a qualified GPU row.
#pragma once
#include "../layer/mesh_content_id.h"
#include <cmath>
#include <span>
#include <string>
#include <string_view>
#include <set>
#include <vector>
namespace bg3::mesh_paks {
constexpr size_t kRowBytes = 840;
struct Row {
  uint64_t content_id{};
  uint32_t index_count{}, vertex_span{}, flags{};
  mesh_content::Sample sample;
  std::array<float,64> uv{};
  std::vector<float> corners; // v2: full ordered position3/UV2 stream
  uint32_t color_layout{}; // v3: 1 = explicit native COLOR_0 RGBA
  std::vector<float> colors; // full ordered corner COLOR_0, never inferred/defaulted
  std::string material;
  bool compact{};
  std::string geometry_digest, color_digest;
  struct Texture {std::string parameter, id; bool operator==(const Texture&) const = default;};
  std::vector<Texture> textures;
};
inline bool uuid(const std::string& s) {
  if(s.size()!=36)return false;
  for(size_t i=0;i<s.size();++i) {
    if(i==8||i==13||i==18||i==23){if(s[i]!='-')return false;}
    else if(!((s[i]>='0'&&s[i]<='9')||(s[i]>='a'&&s[i]<='f')))return false;
  }
  return true;
}
// Transactional manifest contract: local basenames only, unique, bounded.
inline bool shard_names(std::string_view text,std::vector<std::string>& names,size_t max_count=10000){
 names.clear();if(text.empty() || text.size()>1024*1024)return false;
 std::vector<std::string> parsed;std::set<std::string> unique;
 while(!text.empty()){
  const auto end=text.find('\n');std::string name(text.substr(0,end));
  if(!name.empty() && name.back()=='\r')name.pop_back();
  if(name.empty() || !name.starts_with("mesh_paks_cov_paks2_") || !name.ends_with(".bin") ||
   name.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_.-")!=std::string::npos ||
   !unique.insert(name).second || parsed.size()>=max_count)return false;
  parsed.push_back(std::move(name));
  if(end==std::string_view::npos)break;text.remove_prefix(end+1);
 }
 names=std::move(parsed);return !names.empty();
}
inline bool shard_size(uint64_t size,uint64_t total){
 constexpr uint64_t cap=2ull*1024*1024*1024;
 return total<=cap && size<=512ull*1024*1024 && size<=cap-total;
}
// Transactional parser: malformed/truncated/oversized tables publish nothing.
inline bool parse(std::span<const uint8_t> bytes,std::vector<Row>& rows) {
  rows.clear();
  if(bytes.size()<16 || std::memcmp(bytes.data(),"B3PAKS1\0",8))return false;
  auto u32=[&](size_t at){uint32_t v{};for(unsigned i=0;i<4;++i)v|=uint32_t(bytes[at+i])<<(i*8);return v;};
  const uint32_t n=u32(12);
  const uint32_t version=u32(8);
  if((version<1||version>4) || n>1000000 || (version==1&&bytes.size()!=16+uint64_t(n)*kRowBytes))return false;
  if(n>(bytes.size()-16)/(version>=2?kRowBytes+64:kRowBytes))return false;
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
    if(version==2||version==3){
      if(bytes.size()-next<4)return false;const auto count=u32(next);next+=4;
      if(count!=r.index_count||uint64_t(count)*20>bytes.size()-next)return false;
      r.corners.resize(size_t(count)*5);
      for(size_t c=0;c<r.corners.size();++c){const uint32_t v=u32(next+c*4);std::memcpy(&r.corners[c],&v,4);if(!std::isfinite(r.corners[c]))return false;}
      next+=size_t(count)*20;
    }
    if(version==3){
      if(bytes.size()-next<8)return false;r.color_layout=u32(next);const auto count=u32(next+4);next+=8;
      if(r.color_layout>1 || (r.color_layout==0?count!=0:count!=r.index_count) || uint64_t(count)*16>bytes.size()-next)return false;
      r.colors.resize(size_t(count)*4);
      for(size_t c=0;c<r.colors.size();++c){const auto v=u32(next+c*4);std::memcpy(&r.colors[c],&v,4);if(!std::isfinite(r.colors[c]) || r.colors[c]<0 || r.colors[c]>1)return false;}
      next+=size_t(count)*16;
    }
    if(version==4){
      if(bytes.size()-next<72)return false;
      r.compact=true;r.color_layout=u32(next);next+=4;if(r.color_layout>1)return false;
      auto digest=[&](){std::string h;constexpr char hex[]="0123456789abcdef";
        for(size_t c=0;c<32;++c){auto b=bytes[next++];h+=hex[b>>4];h+=hex[b&15];}return h;};
      r.geometry_digest=digest();r.color_digest=digest();
      if(!r.color_layout && r.color_digest!=std::string(64,'0'))return false;
      const auto textures=u32(next);next+=4;if(textures>128)return false;
      std::set<std::string> parameters;
      for(uint32_t t=0;t<textures;++t){
        if(bytes.size()-next<4)return false;auto len=u32(next);next+=4;
        if(!len||len>128||bytes.size()-next<len+36)return false;
        Row::Texture tex;tex.parameter.assign(reinterpret_cast<const char*>(bytes.data()+next),len);next+=len;
        for(unsigned char c:tex.parameter)if(c<32||c>126)return false;
        tex.id.assign(reinterpret_cast<const char*>(bytes.data()+next),36);next+=36;
        if(!uuid(tex.id)||!parameters.insert(tex.parameter).second)return false;
        r.textures.push_back(std::move(tex));
      }
    }
    parsed.push_back(std::move(r));
  }
  if(next!=bytes.size())return false;
  rows=std::move(parsed);return true;
}
// Ordered full-stream witness, never the sampled key, authorizes fallback.
// v2/3 use full equality (UV tolerance 1e-3); v4 SHA-256 is stricter: UV bits
// must also agree. Hash failure/missing callback declines publication.
using Digest = std::string (*)(const void*,size_t);
inline bool full_match(const Row& row,const mesh_content::Input& in,mesh_content::Bytes uv,uint32_t uv_stride,Digest digest=nullptr) {
  mesh_content::Sample s;
  if(!mesh_content::sample(in,s)||(!row.compact&&!(s==row.sample))||in.count!=row.index_count||(!row.compact&&row.corners.size()!=size_t(in.count)*5)||!uv.data||uv_stride<8)return false;
  if(row.compact&&!digest)return false;
  std::vector<float> witness;if(row.compact)witness.resize(size_t(in.count)*5);
  for(uint32_t c=0;c<in.count;++c){
    uint32_t ix=c;
    if(in.index_bytes==1)ix=in.indices.data[c];
    else if(in.index_bytes==2){uint16_t v;std::memcpy(&v,in.indices.data+size_t(c)*2,2);ix=v;}
    else if(in.index_bytes==4)std::memcpy(&ix,in.indices.data+size_t(c)*4,4);
    const int64_t v=in.vertex_offset+ix;if(v<0||uint64_t(v)>UINT64_MAX/uv_stride)return false;
    const uint64_t off=uint64_t(v)*uv_stride;
    if(off>uv.size||uv.size-off<8)return false;
    if(row.compact){
      auto* dst=witness.data()+size_t(c)*5;
      std::memcpy(dst,in.positions.data+uint64_t(v)*in.stride,12);std::memcpy(dst+3,uv.data+off,8);
      for(unsigned k=0;k<5;++k)if(!std::isfinite(dst[k]))return false;
      continue;
    }
    const auto* expected=row.corners.data()+size_t(c)*5;
    if(std::memcmp(in.positions.data+uint64_t(v)*in.stride,expected,12))return false;
    float got[2];std::memcpy(got,uv.data+off,8);
    for(unsigned k=0;k<2;++k)if(!std::isfinite(got[k])||std::abs(got[k]-expected[3+k])>1e-3f)return false;
  }
  return !row.compact || digest(witness.data(),witness.size()*4)==row.geometry_digest;
}
// Compare the exact native COLOR input across every indexed corner. Supported
// encodings have explicit Vulkan component order, never a guessed BGRA swizzle.
enum class ColorEncoding { Unknown, Float4, Unorm8Rgba };
inline bool color_match(const Row& row,const mesh_content::Input& in,mesh_content::Bytes colors,uint32_t stride,ColorEncoding encoding,Digest digest=nullptr){
  const uint32_t width=encoding==ColorEncoding::Float4?16u:encoding==ColorEncoding::Unorm8Rgba?4u:0u;
  if(!width || stride<width || row.color_layout!=1 || (!row.compact&&row.colors.size()!=size_t(in.count)*4) || in.count!=row.index_count || !colors.data || (row.compact&&!digest))return false;
  std::vector<float> witness;if(row.compact)witness.resize(size_t(in.count)*4);
  // sample() validates the index stream bounds and signed vertex offset.
  mesh_content::Sample checked;if(!mesh_content::sample(in,checked))return false;
  for(uint32_t c=0;c<in.count;++c){
    uint32_t ix=c;if(in.index_bytes==1)ix=in.indices.data[c];
    else if(in.index_bytes==2){uint16_t v;std::memcpy(&v,in.indices.data+size_t(c)*2,2);ix=v;}
    else if(in.index_bytes==4)std::memcpy(&ix,in.indices.data+size_t(c)*4,4);
    const int64_t vertex=in.vertex_offset+ix;if(vertex<0 || uint64_t(vertex)>UINT64_MAX/stride)return false;
    const uint64_t at=uint64_t(vertex)*stride;if(at>colors.size || colors.size-at<width)return false;
    for(uint32_t k=0;k<4;++k){float got;
      if(encoding==ColorEncoding::Float4)std::memcpy(&got,colors.data+at+k*4,4);
      else got=float(colors.data[at+k])/255.f;
      if(row.compact){if(!std::isfinite(got)||got<0||got>1)return false;witness[size_t(c)*4+k]=got;continue;}
      const float expected=row.colors[size_t(c)*4+k];
      if(!std::isfinite(got)||!std::isfinite(expected)||got!=expected)return false;
    }
  }
  return !row.compact || digest(witness.data(),witness.size()*4)==row.color_digest;
}
// Paks-only native Tileset rows require an explicit local COLOR layout and
// exactly the captured static culling policy. Resource/clip checks remain separate.
inline bool tileset_color_publication(const Row* row,bool key_on,bool joined,bool cull_known,bool two_sided){
 return key_on && joined && row && row->color_layout==1 && (row->compact || row->colors.size()==size_t(row->index_count)*4) &&
  cull_known && two_sided==bool(row->flags&1u);
}
inline bool hlod_compatible(const Row& row,uint64_t vs,uint64_t fs,bool cull_known,bool two_sided) {
  return vs==0x2d2ac3c38992b7f0ull&&fs==0xb0ecc292e8661561ull&&
    (row.flags&4u)&&!(row.flags&2u)&&cull_known&&two_sided==bool(row.flags&1u);
}
enum class Match { Missing, Unique, Ambiguous, Invalid };
struct Result {Match status=Match::Missing;const Row* row{};};
// Draw-order UV signature: independent of position space, vertex numbering,
// index width and padding. XFB 64/88-byte streams use UV at byte 16.
inline bool uv_sample(const mesh_content::Input& in,mesh_content::Bytes uv,uint32_t stride,std::array<float,64>& out){
  if(!in.count||in.count%3||!uv.data||stride<8||
    (in.index_bytes!=0&&in.index_bytes!=1&&in.index_bytes!=2&&in.index_bytes!=4)||
    (in.index_bytes&&(!in.indices.data||uint64_t(in.count)*in.index_bytes>in.indices.size)))return false;
  for(uint32_t s=0;s<32;++s){
    uint32_t ix=uint32_t(uint64_t(s)*(in.count-1)/31);
    if(in.index_bytes==1)ix=in.indices.data[ix];
    else if(in.index_bytes==2){uint16_t v;std::memcpy(&v,in.indices.data+uint64_t(ix)*2,2);ix=v;}
    else if(in.index_bytes==4){uint32_t v;std::memcpy(&v,in.indices.data+uint64_t(ix)*4,4);ix=v;}
    if(in.vertex_offset>INT64_MAX-ix)return false;
    const auto v=in.vertex_offset+ix;if(v<0||uint64_t(v)>UINT64_MAX/stride)return false;
    const auto at=uint64_t(v)*stride;if(at>uv.size||uv.size-at<8)return false;
    std::memcpy(out.data()+s*2,uv.data+at,8);
    if(!std::isfinite(out[s*2])||!std::isfinite(out[s*2+1]))return false;
  }
  return true;
}
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
    if(out.row && (out.row->material!=r.material||out.row->flags!=r.flags||!(out.row->sample==r.sample)||out.row->uv!=r.uv||out.row->corners!=r.corners||out.row->color_layout!=r.color_layout||out.row->colors!=r.colors||out.row->geometry_digest!=r.geometry_digest||out.row->color_digest!=r.color_digest||out.row->textures!=r.textures))return {Match::Ambiguous,{}};
    out={Match::Unique,&r};
  }
  return out;
}
}
