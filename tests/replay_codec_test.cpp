#include "../common/replay_codec.h"
#include <cstdio>
#include <random>

namespace codec=bg3::replay::codec;
static int failures=0;
static void check(bool pass,const char* name){std::printf("%s %s\n",pass?"PASS":"FAIL",name);failures+=!pass;}
static std::string pack(const std::vector<uint8_t>& data,size_t stride=12345) {
  std::ostringstream out(std::ios::binary);codec::Writer writer(out,data.size());
  for(size_t i=0;i<data.size();i+=stride)check(writer.write(data.data()+i,(std::min)(stride,data.size()-i)),"stream write");
  check(writer.finish(),"stream finish");check(writer.encoded_bytes()==out.str().size(),"encoded byte count");return out.str();
}
int main(){
  const size_t count=size_t(codec::kBlock)*2+73;
  std::vector<uint8_t> data(count);
  for(size_t i=0;i<data.size();++i)data[i]=uint8_t((i/1024)%7);
  const auto packed=pack(data,1024*1024+17);
  check(codec::api().available(),"Windows Compression API available");
  check(packed.size()<data.size()/4,"repeated cache bytes compress");
  std::istringstream input(packed,std::ios::binary);codec::Reader reader(input);
  check(reader.good()&&reader.compressed()&&reader.size()==data.size(),"indexed file header");
  for(uint64_t offset:{uint64_t(codec::kBlock)-13,uint64_t(codec::kBlock)+7,0ull,uint64_t(count)-23}){
    const size_t n=(std::min)(size_t(37),count-size_t(offset));std::vector<uint8_t> bytes(n);
    check(reader.read(offset,n,bytes.data())&&std::equal(bytes.begin(),bytes.end(),data.begin()+size_t(offset)),"random read across blocks");
  }
  std::vector<uint8_t> roundtrip;std::string error;
  const std::vector<uint8_t> encoded(packed.begin(),packed.end());
  check(codec::encoded(encoded)&&codec::decode(encoded,roundtrip,error)&&roundtrip==data,"complete roundtrip");
  std::mt19937 rng(41);std::vector<uint8_t> noise(100003);for(auto& b:noise)b=uint8_t(rng());
  const auto raw_blocks=pack(noise,20000);const std::vector<uint8_t> noise_encoded(raw_blocks.begin(),raw_blocks.end());
  check(codec::decode(noise_encoded,roundtrip,error)&&roundtrip==noise,"incompressible raw block fallback");
  check(raw_blocks.size()==noise.size()+40,"raw block has bounded overhead");
  {
    const auto empty=pack({});std::istringstream in(empty,std::ios::binary);codec::Reader r(in);
    check(r.good()&&r.size()==0&&r.read(0,0,nullptr),"empty stream");
  }
  {
    std::string raw("legacy raw bytes\0tail",21);std::istringstream in(raw,std::ios::binary);codec::Reader r(in);
    std::vector<uint8_t> bytes(raw.size());check(r.good()&&!r.compressed()&&r.read(0,bytes.size(),bytes.data())&&std::equal(bytes.begin(),bytes.end(),reinterpret_cast<const uint8_t*>(raw.data())),"legacy raw file");
  }
  for(size_t length:{size_t(8),size_t(23),size_t(33),packed.size()-1}){
    std::istringstream in(packed.substr(0,length),std::ios::binary);codec::Reader r(in);check(!r.good(),"truncated file rejected");
  }
  {
    auto corrupt=raw_blocks;corrupt.back()^=1;std::istringstream in(corrupt,std::ios::binary);codec::Reader r(in);std::vector<uint8_t> bytes(noise.size());
    check(r.good()&&!r.read(0,bytes.size(),bytes.data()),"raw block corruption rejected");
  }
  {
    auto corrupt=packed;corrupt[32]^=1;std::istringstream in(corrupt,std::ios::binary);codec::Reader r(in);std::vector<uint8_t> bytes(codec::kBlock);
    check(r.good()&&!r.read(0,bytes.size(),bytes.data()),"compressed block checksum rejected");
  }
  {
    std::istringstream in(packed,std::ios::binary);codec::Reader r(in,1024);check(!r.good(),"decoded size limit enforced before allocation");
    std::istringstream trailing(packed+"x",std::ios::binary);codec::Reader t(trailing);check(!t.good(),"trailing bytes rejected");
    uint8_t b=0;check(!reader.read(count,1,&b),"read outside file rejected");
  }
  {
    std::ostringstream out(std::ios::binary);codec::Writer writer(out,3);const uint8_t b[4]{};
    check(!writer.write(b,4),"writer rejects excess bytes");
    std::ostringstream short_out(std::ios::binary);codec::Writer short_writer(short_out,3);check(!short_writer.finish(),"writer rejects incomplete file");
  }
  return failures?1:0;
}
