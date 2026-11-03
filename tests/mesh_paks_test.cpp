#include "../common/mesh_paks.h"
#include <cstdio>
#include <cstdlib>
#include <fstream>
using namespace bg3;
#define CHECK(x) do{if(!(x)){std::printf("FAIL %d: %s\n",__LINE__,#x);std::exit(1);}}while(0)
int main(int argc,char** argv){
  if(argc==2){std::ifstream file(argv[1],std::ios::binary|std::ios::ate);CHECK(file.good());const auto bytes_count=file.tellg();CHECK(bytes_count>0&&bytes_count<512ll*1024*1024);file.seekg(0);
    std::vector<uint8_t> bytes(static_cast<size_t>(bytes_count));CHECK(bool(file.read(reinterpret_cast<char*>(bytes.data()),std::streamsize(bytes.size()))));std::vector<mesh_paks::Row> rows;
    CHECK(mesh_paks::parse(bytes,rows));size_t hlod=0,full=0;for(const auto& r:rows){hlod+=bool(r.flags&4u);full+=!r.corners.empty();}
    std::printf("pak real table: %zu rows, %zu full streams, %zu audited HLOD recipe rows\n",rows.size(),full,hlod);return 0;}
  std::array<float,9> p{0,0,0,1,0,0,0,1,0};std::array<uint16_t,3> ix{0,1,2};
  mesh_paks::Row row;CHECK(mesh_content::sample({{reinterpret_cast<const uint8_t*>(p.data()),sizeof p},{reinterpret_cast<const uint8_t*>(ix.data()),sizeof ix},12,3,2,0},row.sample));
  row.content_id=mesh_content::hash(row.sample);row.index_count=3;row.vertex_span=3;row.material="12345678-1234-1234-1234-123456789abc";
  CHECK(row.content_id==0xd5ac4d013e3b5a83ull);
  std::vector<mesh_paks::Row> rows{row};auto r=mesh_paks::lookup(rows,3,&row.sample,row.uv.data());CHECK(r.status==mesh_paks::Match::Unique);
  rows[0]=row;rows.push_back(row);CHECK(mesh_paks::lookup(rows,3,&row.sample,row.uv.data()).status==mesh_paks::Match::Unique);
  rows[1].material[0]='a';CHECK(mesh_paks::lookup(rows,3,&row.sample,row.uv.data()).status==mesh_paks::Match::Ambiguous);
  rows.resize(1);auto bad=row.sample;bad.words[3]^=1;CHECK(mesh_paks::lookup(rows,3,&bad,row.uv.data()).status==mesh_paks::Match::Missing);
  auto uv=row.uv;uv[3]=0.002f;CHECK(mesh_paks::lookup(rows,3,&row.sample,uv.data()).status==mesh_paks::Match::Missing);
  uv[3]=0.0009f;CHECK(mesh_paks::lookup(rows,3,nullptr,uv.data()).status==mesh_paks::Match::Unique);
  uv[3]=NAN;CHECK(mesh_paks::lookup(rows,3,nullptr,uv.data()).status==mesh_paks::Match::Invalid);
  CHECK(mesh_paks::lookup(rows,3,nullptr,nullptr).status==mesh_paks::Match::Invalid);
  std::vector<uint8_t> bytes(16+840);std::memcpy(bytes.data(),"B3PAKS1\0",8);
  auto word=[&](size_t at,uint32_t v){for(unsigned b=0;b<4;++b)bytes[at+b]=uint8_t(v>>(b*8));};
  word(8,1);word(12,1);word(16,uint32_t(row.content_id));word(20,uint32_t(row.content_id>>32));word(24,3);word(28,3);
  for(size_t i=0;i<row.sample.words.size();++i)word(40+i*4,row.sample.words[i]);std::memcpy(bytes.data()+820,row.material.data(),36);
  CHECK(mesh_paks::parse(bytes,rows));CHECK(rows.size()==1&&rows[0].material==row.material);
  bytes.push_back(0);CHECK(!mesh_paks::parse(bytes,rows)&&rows.empty());bytes.pop_back();
  word(32,8);CHECK(!mesh_paks::parse(bytes,rows)&&rows.empty());word(32,0);
  word(40,2);CHECK(!mesh_paks::parse(bytes,rows)&&rows.empty());word(40,1);
  word(564,0x7fc00000);CHECK(!mesh_paks::parse(bytes,rows)&&rows.empty());word(564,0);
  bytes[820]='z';CHECK(!mesh_paks::parse(bytes,rows)&&rows.empty());
  bytes[820]='1';word(8,2);const size_t full=bytes.size();bytes.resize(full+4+60);word(full,3);
  float corners[15]{0,0,0,0,0,1,0,0,0,0,0,1,0,0,0};
  std::memcpy(bytes.data()+full+4,corners,sizeof corners);
  CHECK(mesh_paks::parse(bytes,rows)&&rows.size()==1);
  std::array<float,6> raw_uv{};mesh_content::Input input{{reinterpret_cast<const uint8_t*>(p.data()),sizeof p},{reinterpret_cast<const uint8_t*>(ix.data()),sizeof ix},12,3,2,0};
  mesh_content::Bytes uvbytes{reinterpret_cast<const uint8_t*>(raw_uv.data()),sizeof raw_uv};
  CHECK(mesh_paks::full_match(rows[0],input,uvbytes,8));
  p[6]=0.5f;CHECK(!mesh_paks::full_match(rows[0],input,uvbytes,8));p[6]=0;
  raw_uv[5]=0.002f;CHECK(!mesh_paks::full_match(rows[0],input,uvbytes,8));raw_uv[5]=0;
  auto shortuv=uvbytes;shortuv.size-=1;CHECK(!mesh_paks::full_match(rows[0],input,shortuv,8));
  bytes.pop_back();CHECK(!mesh_paks::parse(bytes,rows)&&rows.empty());
  bytes.resize(16);word(12,1000000);CHECK(!mesh_paks::parse(bytes,rows)&&rows.empty());
  row.flags=4;CHECK(mesh_paks::hlod_compatible(row,0x2d2ac3c38992b7f0ull,0xb0ecc292e8661561ull,true,false));
  CHECK(!mesh_paks::hlod_compatible(row,0,0xb0ecc292e8661561ull,true,false));
  CHECK(!mesh_paks::hlod_compatible(row,0x2d2ac3c38992b7f0ull,0xb0ecc292e8661561ull,false,false));
  CHECK(!mesh_paks::hlod_compatible(row,0x2d2ac3c38992b7f0ull,0xb0ecc292e8661561ull,true,true));
  row.flags=0;CHECK(!mesh_paks::hlod_compatible(row,0x2d2ac3c38992b7f0ull,0xb0ecc292e8661561ull,true,false));
  row.flags=6;CHECK(!mesh_paks::hlod_compatible(row,0x2d2ac3c38992b7f0ull,0xb0ecc292e8661561ull,true,false));
  row.flags=5;CHECK(mesh_paks::hlod_compatible(row,0x2d2ac3c38992b7f0ull,0xb0ecc292e8661561ull,true,true));
  std::vector<float> many_p(99*3),many_uv(99*2);std::vector<uint32_t> many_i(99);
  for(uint32_t c=0;c<99;++c){many_i[c]=c;many_p[c*3]=float(c);}
  mesh_content::Input many{{reinterpret_cast<const uint8_t*>(many_p.data()),many_p.size()*4},{reinterpret_cast<const uint8_t*>(many_i.data()),many_i.size()*4},12,99,4,0};
  mesh_paks::Row fullrow;fullrow.index_count=99;CHECK(mesh_content::sample(many,fullrow.sample));
  for(uint32_t c=0;c<99;++c){fullrow.corners.insert(fullrow.corners.end(),{many_p[c*3],0,0,0,0});}
  mesh_content::Bytes manyuv{reinterpret_cast<const uint8_t*>(many_uv.data()),many_uv.size()*4};
  CHECK(mesh_paks::full_match(fullrow,many,manyuv,8));
  many_p[3]+=0.5f;mesh_content::Sample unchanged;CHECK(mesh_content::sample(many,unchanged)&&unchanged==fullrow.sample);
  CHECK(!mesh_paks::full_match(fullrow,many,manyuv,8));many_p[3]-=0.5f;
  many_uv[2]=1e-3f;CHECK(mesh_paks::full_match(fullrow,many,manyuv,8));
  many_uv[2]=std::nextafter(1e-3f,1.f);CHECK(!mesh_paks::full_match(fullrow,many,manyuv,8));
  std::puts("mesh_paks: parser, candidate joins, ambiguity and publication fail-closed paths passed");
}
