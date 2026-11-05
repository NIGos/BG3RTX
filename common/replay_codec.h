#pragma once
// Lossless replay files: indexed, independently compressed 4 MiB blocks. Only the
// diagnostic writer/replay tools instantiate this codec; no rendering dependency.
#include <windows.h>
#include <compressapi.h>
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <istream>
#include <ostream>
#include <sstream>
#include <string>
#include <vector>

namespace bg3::replay::codec {
inline constexpr char kMagic[8] = {'B','G','3','R','P','K','Z','1'};
inline constexpr uint32_t kBlock = 4u << 20;
inline constexpr uint64_t kMaxDecoded = 16ull << 30;
inline uint32_t hash(const uint8_t* p, size_t n) {
  uint32_t h=2166136261u; for(size_t i=0;i<n;++i) h=(h^p[i])*16777619u; return h;
}
inline void put32(std::ostream& f,uint32_t n) { for(int i=0;i<4;++i)f.put(char(n>>(8*i))); }
inline void put64(std::ostream& f,uint64_t n) { for(int i=0;i<8;++i)f.put(char(n>>(8*i))); }
inline bool get32(std::istream& f,uint32_t& n) {
  n=0;for(int i=0;i<4;++i){int c=f.get();if(c==EOF)return false;n|=uint32_t(uint8_t(c))<<(8*i);}return true;
}
inline bool get64(std::istream& f,uint64_t& n) {
  n=0;for(int i=0;i<8;++i){int c=f.get();if(c==EOF)return false;n|=uint64_t(uint8_t(c))<<(8*i);}return true;
}
struct Api {
  HMODULE module=nullptr;
  decltype(&CreateCompressor) create=nullptr;
  decltype(&CloseCompressor) close=nullptr;
  decltype(&Compress) compress=nullptr;
  decltype(&CreateDecompressor) create_d=nullptr;
  decltype(&CloseDecompressor) close_d=nullptr;
  decltype(&Decompress) decompress=nullptr;
  Api() {
    module=LoadLibraryExW(L"cabinet.dll",nullptr,LOAD_LIBRARY_SEARCH_SYSTEM32);
    if(!module)return;
    create=reinterpret_cast<decltype(create)>(GetProcAddress(module,"CreateCompressor"));
    close=reinterpret_cast<decltype(close)>(GetProcAddress(module,"CloseCompressor"));
    compress=reinterpret_cast<decltype(compress)>(GetProcAddress(module,"Compress"));
    create_d=reinterpret_cast<decltype(create_d)>(GetProcAddress(module,"CreateDecompressor"));
    close_d=reinterpret_cast<decltype(close_d)>(GetProcAddress(module,"CloseDecompressor"));
    decompress=reinterpret_cast<decltype(decompress)>(GetProcAddress(module,"Decompress"));
  }
  ~Api(){if(module)FreeLibrary(module);}
  bool available()const{return create&&close&&compress&&create_d&&close_d&&decompress;}
};
inline Api& api(){static Api a;return a;}
inline bool encoded(const std::vector<uint8_t>& bytes){return bytes.size()>=8&&std::memcmp(bytes.data(),kMagic,8)==0;}

class Writer {
  std::ostream& out_; COMPRESSOR_HANDLE handle_=nullptr;
  uint64_t expected_=0,written_=0,encoded_=24;
  std::vector<uint8_t> pending_,packed_;
  bool ok_=true,finished_=false,limit_exceeded_=false;
  uint64_t encoded_limit_=~0ull;
  bool flush() {
    if(pending_.empty())return ok_;
    SIZE_T n=0; bool compressed=false;
    if(handle_) {
      packed_.resize(pending_.size()+65536);
      compressed=api().compress(handle_,pending_.data(),pending_.size(),packed_.data(),packed_.size(),&n)!=FALSE&&n<pending_.size();
    }
    const size_t stored=compressed?size_t(n):pending_.size();
    if (encoded_>encoded_limit_ || 16+stored>encoded_limit_-encoded_) { limit_exceeded_=true;ok_=false;return false; }
    put32(out_,uint32_t(pending_.size()));put32(out_,uint32_t(stored));
    put32(out_,hash(pending_.data(),pending_.size()));put32(out_,compressed?1u:0u);
    out_.write(reinterpret_cast<const char*>(compressed?packed_.data():pending_.data()),std::streamsize(stored));
    encoded_+=16+stored;pending_.clear();ok_=ok_&&bool(out_);return ok_;
  }
 public:
  Writer(std::ostream& out,uint64_t decoded_bytes,uint64_t encoded_limit=~0ull):out_(out),expected_(decoded_bytes),encoded_limit_(encoded_limit) {
    if(encoded_limit<24){limit_exceeded_=true;ok_=false;return;}
    if(decoded_bytes>kMaxDecoded){ok_=false;return;}
    if(api().available())api().create(COMPRESS_ALGORITHM_XPRESS_HUFF,nullptr,&handle_);
    out_.write(kMagic,8);put64(out_,decoded_bytes);put32(out_,kBlock);put32(out_,0);
    pending_.reserve(kBlock);ok_=bool(out_);
  }
  Writer(const Writer&)=delete;Writer& operator=(const Writer&)=delete;
  ~Writer(){if(handle_)api().close(handle_);}
  bool good()const{return ok_;}
  uint64_t encoded_bytes()const{return encoded_;}
  bool limit_exceeded()const{return limit_exceeded_;}
  bool write(const uint8_t* data,size_t count) {
    if(!ok_||finished_||count>expected_-written_){ok_=false;return false;}
    written_+=count;
    while(count){const size_t n=(std::min)(count,size_t(kBlock)-pending_.size());pending_.insert(pending_.end(),data,data+n);data+=n;count-=n;if(pending_.size()==kBlock&&!flush())return false;}
    return true;
  }
  bool finish(){if(finished_)return ok_;finished_=true;ok_=ok_&&written_==expected_;return flush()&&ok_;}
};

class Reader {
  struct Block {uint64_t offset,file;uint32_t raw,stored,checksum,flags;};
  std::istream& in_;DECOMPRESSOR_HANDLE handle_=nullptr;
  uint64_t size_=0;bool compressed_=false;
  std::string error_;std::vector<Block> blocks_;
  std::vector<uint8_t> packed_,decoded_;size_t cached_=size_t(-1);
  bool fail(const char* e){error_=e;return false;}
 public:
  Reader(std::istream& in,uint64_t max_decoded=kMaxDecoded):in_(in) {
    in_.clear();in_.seekg(0,std::ios::end);auto end=in_.tellg();
    if(end<0){fail("cannot size replay file");return;}
    const uint64_t length=uint64_t(end);in_.seekg(0);
    char magic[8]{};if(length>=8)in_.read(magic,8);
    compressed_=length>=8&&std::memcmp(magic,kMagic,8)==0;
    if(!compressed_){size_=length;in_.clear();in_.seekg(0);if(size_>max_decoded)fail("replay file exceeds decoded limit");return;}
    uint32_t block=0,reserved=0;
    if(!get64(in_,size_)||!get32(in_,block)||!get32(in_,reserved)||size_>max_decoded||block!=kBlock||reserved){fail("invalid replay codec header");return;}
    uint64_t total=0,pos=24;
    while(total<size_) {
      Block b{};b.offset=total;
      if(length-pos<16||!get32(in_,b.raw)||!get32(in_,b.stored)||!get32(in_,b.checksum)||!get32(in_,b.flags)){fail("truncated replay block header");return;}
      pos+=16;b.file=pos;
      if(b.raw!=(std::min)(uint64_t(kBlock),size_-total)||b.stored>kBlock||!b.stored||b.flags>1||(!b.flags&&b.stored!=b.raw)||(b.flags&&b.stored>=b.raw)||b.stored>length-pos){fail("invalid replay block size");return;}
      blocks_.push_back(b);total+=b.raw;pos+=b.stored;in_.seekg(std::streamoff(pos));
    }
    if(pos!=length){fail("trailing replay codec bytes");return;}
  }
  Reader(const Reader&)=delete;Reader& operator=(const Reader&)=delete;
  ~Reader(){if(handle_)api().close_d(handle_);}
  bool good()const{return error_.empty();}
  bool compressed()const{return compressed_;}
  uint64_t size()const{return size_;}
  const std::string& error()const{return error_;}
  bool read(uint64_t offset,size_t count,uint8_t* dst) {
    if(!good())return false;
    if(offset>size_||count>size_-offset)return fail("replay read outside decoded file");
    if(!compressed_){in_.clear();in_.seekg(std::streamoff(offset));in_.read(reinterpret_cast<char*>(dst),std::streamsize(count));return bool(in_)||fail("truncated raw replay file");}
    while(count) {
      const auto it=std::upper_bound(blocks_.begin(),blocks_.end(),offset,[](uint64_t x,const Block& b){return x<b.offset;});
      if(it==blocks_.begin())return fail("missing replay block");
      const size_t index=size_t(it-blocks_.begin()-1);const Block& b=blocks_[index];
      if(cached_!=index) {
        packed_.resize(b.stored);decoded_.resize(b.raw);in_.clear();in_.seekg(std::streamoff(b.file));in_.read(reinterpret_cast<char*>(packed_.data()),b.stored);
        if(!in_)return fail("truncated replay block");
        if(b.flags) {
          if(!handle_&&(!api().available()||!api().create_d(COMPRESS_ALGORITHM_XPRESS_HUFF,nullptr,&handle_)))return fail("replay decompressor unavailable");
          SIZE_T n=0;if(!api().decompress(handle_,packed_.data(),packed_.size(),decoded_.data(),decoded_.size(),&n)||n!=b.raw)return fail("corrupt compressed replay block");
        }else std::copy(packed_.begin(),packed_.end(),decoded_.begin());
        if(hash(decoded_.data(),decoded_.size())!=b.checksum)return fail("replay block checksum mismatch");
        cached_=index;
      }
      const size_t local=size_t(offset-b.offset),n=(std::min)(count,decoded_.size()-local);
      if(!n)return fail("invalid replay block offset");
      std::memcpy(dst,decoded_.data()+local,n);dst+=n;offset+=n;count-=n;
    }
    return true;
  }
};
inline bool decode(const std::vector<uint8_t>& bytes,std::vector<uint8_t>& decoded,std::string& error,uint64_t max_decoded=kMaxDecoded) {
  std::string memory;if(!bytes.empty())memory.assign(reinterpret_cast<const char*>(bytes.data()),bytes.size());std::istringstream in(memory,std::ios::binary);
  Reader reader(in,max_decoded);if(!reader.good()){error=reader.error();return false;}
  decoded.resize(size_t(reader.size()));if(!reader.read(0,decoded.size(),decoded.data())){error=reader.error();decoded.clear();return false;}return true;
}
} // namespace bg3::replay::codec
