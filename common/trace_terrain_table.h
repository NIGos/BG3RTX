#pragma once
#include <cstdint>
namespace bg3::trace_terrain {
// Fixed metadata budget, never inferred descriptor count from observed writes.
inline constexpr uint32_t table_cap=4096;
inline bool requested(bool graphics,bool tracing,bool request,uint64_t vs,uint64_t fs){
 return graphics && tracing && request && vs==0x64e2568916ede3d6ull && fs==0x6650c75f3d3e4968ull;
}
struct Range {uint32_t declared{},emitted{},truncated{};};
inline Range range(bool declaredKnown,uint32_t count){
 if(!declaredKnown)return {};
 const uint32_t emitted=count<table_cap?count:table_cap;
 return {count,emitted,count-emitted};
}
}
