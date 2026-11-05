#pragma once
#include <algorithm>
#include <array>
#include <cstdint>
#include <span>
#include <vector>
namespace bg3::cpu_opt {
// Successful CPU metadata only. Dynamic offsets are compared in full; a hash
// selects a bounded slot but is never an identity certificate.
template<class Value,size_t Slots=4096> class DescriptorReceipts {
 public:
 using Key=std::array<uint64_t,8>;
 template<class Load,class Use> bool query(const Key& key,std::span<const uint32_t> dynamic,Value& out,Load&& load,Use&& use){
  uint64_t h=1469598103934665603ull;for(auto w:key)h=(h^w)*1099511628211ull;
  for(auto w:dynamic)h=(h^w)*1099511628211ull;h^=h>>33;h*=0xff51afd7ed558ccdull;h^=h>>33;
  auto& e=entries_[h%Slots];
  if(e.valid && e.key==key && std::equal(dynamic.begin(),dynamic.end(),e.dynamic.begin(),e.dynamic.end())){
   out=e.value;use(out);return true;
  }
  if(!load(out))return false; // Failed queries retain their original partial side effects.
  e.key=key;e.dynamic.assign(dynamic.begin(),dynamic.end());e.value=out;e.valid=true;return true;
 }
 private:
 struct Entry {Key key{};std::vector<uint32_t> dynamic;Value value{};bool valid{};};
 std::array<Entry,Slots> entries_{};
};
// Keep the ordinary query's lifetime-note order, including last-note dedup.
template<class Binding,class Note> void descriptor_uses(const Binding& b,Note&& note){
 note(uint64_t(b.view));note(uint64_t(b.buffer));if(b.view && b.image)note(uint64_t(b.image));
}
}
