#pragma once
// Sufficient ordinary-renderpass bound for the post-pass native uniform gather:
// no graphics stage in this pass may write a storage/physical buffer or image.
// Transfers/dispatch are forbidden inside the pass; legal host writes precede
// submission. Unknown pointer destinations decline. This is not content caching.
#include <cstddef>
#include <cstdint>
#include <unordered_map>
#include <string>
namespace bg3::native_hit {
inline bool no_graphics_storage_writes(const uint32_t* code,size_t words){
 if(!code || words<5 || code[0]!=0x07230203)return false;
 std::unordered_map<uint32_t,uint32_t> classes,pointers;
 std::unordered_map<uint32_t,bool> pure_ext;
 for(size_t p=5;p<words;){auto n=code[p]>>16,op=code[p]&65535u;if(!n || n>words-p)return false;
  if(op==32){if(n<4)return false;classes[code[p+1]]=code[p+2];}
  if(op==11){
   if(n<3)return false;std::string name;
   const auto* chars=reinterpret_cast<const char*>(code+p+2);size_t count=(n-2)*4;
   size_t i=0;for(;i<count && chars[i];++i)name+=chars[i];if(i==count)return false;
   pure_ext[code[p+1]]=name=="GLSL.std.450" || name.rfind("NonSemantic.",0)==0;
  }p+=n;
 }
 for(size_t p=5;p<words;){auto n=code[p]>>16;
  if(n>=3){auto type=classes.find(code[p+1]);if(type!=classes.end())pointers[code[p+2]]=type->second;}p+=n;
 }
 auto local=[&](uint32_t id){auto i=pointers.find(id);return i!=pointers.end() && (i->second==3 || i->second==6 || i->second==7);};
 for(size_t p=5;p<words;){auto n=code[p]>>16,op=code[p]&65535u;
  if(op==62 || op==63 || op==64 || op==228 || op==319){if(n<2 || !local(code[p+1]))return false;}
  if((op>=229 && op<=242) || op==318 || op==6035 || op==5614 || op==5615){if(n<4 || !local(code[p+3]))return false;}
  if(op==99 || op==4458 || op==5360)return false;
  if(op==12 && (n<5 || !pure_ext[code[p+3]]))return false;
  // Future/vendor memory operations are outside this deliberately small bound.
  if(op>=512 && op!=4416 && op!=5380 && op!=5632 && op!=5633)return false;
  p+=n;
 }return true;
}
}
