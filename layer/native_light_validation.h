// Diagnostic native lighting controls. Never mutates a game's uniform buffer.
// Only recognized scalar reads of the native lighting flags are replaced.
#pragma once
#include <spirv/unified1/spirv.hpp>
#include <cstddef>
#include <cstdint>
#include <map>
#include <set>
#include <vector>

namespace bg3::native_light_validation {
enum class Status { NotTarget, Unsupported, Applied };
struct Result {
  Status status = Status::NotTarget;
  uint32_t loads = 0, components = 0;
  std::vector<uint32_t> words;
};
inline Result patch(const uint32_t* data, size_t count) {
  Result r;
  if (!data || count < 5 || data[0] != spv::MagicNumber) return r;
  struct Inst { size_t at; uint32_t n; spv::Op op; };
  std::vector<Inst> ins;
  std::map<uint32_t,std::vector<uint32_t>> types;
  std::map<uint32_t,uint32_t> sets, bindings, constants, constant_types, variables;
  std::map<std::pair<uint32_t,uint32_t>,uint32_t> offsets;
  bool compute=false;
  for(size_t at=5;at<count;) {
    uint32_t n=data[at]>>16;auto op=spv::Op(data[at]&65535u);
    if(!n || n>count-at)return r;
    ins.push_back({at,n,op});const uint32_t* w=data+at;
    if(op==spv::OpEntryPoint && n>=4 && w[1]==spv::ExecutionModelGLCompute)compute=true;
    if(op==spv::OpDecorate && n==4) {
      if(w[2]==spv::DecorationDescriptorSet)sets[w[1]]=w[3];
      if(w[2]==spv::DecorationBinding)bindings[w[1]]=w[3];
    }
    if(op==spv::OpMemberDecorate && n==5 && w[3]==spv::DecorationOffset)offsets[{w[1],w[2]}]=w[4];
    if((op==spv::OpTypeFloat || op==spv::OpTypeInt || op==spv::OpTypeVector ||
        op==spv::OpTypeStruct || op==spv::OpTypePointer) && n>=2)
      types[w[1]]=std::vector<uint32_t>(w,w+n);
    if(op==spv::OpConstant && n==4) {constants[w[2]]=w[3];constant_types[w[2]]=w[1];}
    if(op==spv::OpVariable && n>=4 && w[3]==spv::StorageClassUniform)variables[w[2]]=w[1];
    at+=n;
  }
  if(!compute)return r;
  std::set<uint32_t> native_bindings;
  uint32_t flags=0;
  for(auto [id,binding]:bindings)if(sets.contains(id) && sets[id]==1) {
    native_bindings.insert(binding);
    if(binding==0 && variables.contains(id))flags=id;
  }
  if(!flags || !native_bindings.contains(10) || !native_bindings.contains(11) || !native_bindings.contains(23))return r;
  auto ptr=types[variables[flags]];
  if(ptr.size()!=4 || (ptr[0]&65535u)!=spv::OpTypePointer || ptr[2]!=spv::StorageClassUniform)return r;
  uint32_t structure=ptr[3];auto st=types[structure];
  if(st.size()<4 || st[0]!=(uint32_t(spv::OpTypeStruct)|(uint32_t(st.size())<<16)))return r;
  auto vec=types[st[2]];auto res=types[st[3]];
  if(vec.size()!=4 || (vec[0]&65535u)!=spv::OpTypeVector || vec[3]!=4 ||
     res.size()!=4 || (res[0]&65535u)!=spv::OpTypeVector || res[3]!=2)return r;
  uint32_t float_type=vec[2];auto ft=types[float_type];auto rt=types[res[2]];
  if(ft.size()!=3 || (ft[0]&65535u)!=spv::OpTypeFloat || ft[2]!=32 ||
     rt.size()!=4 || (rt[0]&65535u)!=spv::OpTypeInt || rt[2]!=32 || rt[3]!=0 ||
     !offsets.contains({structure,0}) || offsets[{structure,0}]!=0 ||
     !offsets.contains({structure,1}) || offsets[{structure,1}]!=16)return r;
  r.status=Status::Unsupported;
  std::map<uint32_t,std::vector<uint32_t>> paths;paths[flags]={};
  std::map<size_t,uint32_t> replace;
  for(auto i:ins) {
    const uint32_t* w=data+i.at;
    if((i.op==spv::OpAccessChain || i.op==spv::OpInBoundsAccessChain) && i.n>=5 && paths.contains(w[3])) {
      auto path=paths[w[3]];
      for(uint32_t k=4;k<i.n;++k) {
        if(!constants.contains(w[k]))return r;
        auto ct=types[constant_types[w[k]]];
        if(ct.size()!=4 || (ct[0]&65535u)!=spv::OpTypeInt || ct[2]!=32)return r;
        path.push_back(constants[w[k]]);
      }
      paths[w[2]]=path;
    } else if(i.op==spv::OpCopyObject && i.n==4 && paths.contains(w[3]))paths[w[2]]=paths[w[3]];
    else if(i.op==spv::OpLoad && i.n>=4 && paths.contains(w[3])) {
      const auto& path=paths[w[3]];
      // Whole struct/vector, dynamic indexing and pointer escapes are refused,
      // rather than partially patching an unproven native interface.
      if(path.empty() || (path[0]==0 && path.size()!=2))return r;
      if(path[0]!=0)continue;
      if(path[1]>3 || w[1]!=float_type)return r;
      if(path[1]!=2) {replace[i.at]=path[1];r.components|=1u<<path[1];}
    } else if(i.op==spv::OpFunctionCall || i.op==spv::OpPhi || i.op==spv::OpSelect ||
              i.op==spv::OpCopyMemory || i.op==spv::OpCopyMemorySized || i.op==spv::OpPtrAccessChain) {
      for(uint32_t k=1;k<i.n;++k)if(paths.contains(w[k]))return r;
    }
  }
  if(replace.empty() || r.components!=11u)return r;
  uint32_t next=data[3],zero=0,one=0;
  for(auto [id,value]:constants)if(constant_types[id]==float_type) {
    if(value==0)zero=id;
    if(value==0x3f800000u)one=id;
  }
  if(next>0xfffffff0u)return r;
  std::vector<uint32_t> added;
  auto make_const=[&](uint32_t& id,uint32_t bits) {
    if(!id) {id=next++;added.insert(added.end(),{(4u<<16)|spv::OpConstant,float_type,id,bits});}
  };
  make_const(zero,0);make_const(one,0x3f800000u);
  r.words.assign(data,data+5);r.words[3]=next;bool inserted=false;
  for(auto i:ins) {
    const uint32_t* w=data+i.at;
    if(i.op==spv::OpFunction && !inserted) {
      r.words.insert(r.words.end(),added.begin(),added.end());inserted=true;
    }
    if(auto f=replace.find(i.at);f!=replace.end()) {
      r.words.insert(r.words.end(),{(4u<<16)|spv::OpCopyObject,w[1],w[2],f->second==1?zero:one});
      ++r.loads;
    } else r.words.insert(r.words.end(),w,w+i.n);
  }
  if(!inserted) {r.words.clear();return r;}
  r.status=Status::Applied;return r;
}
} // namespace bg3::native_light_validation
