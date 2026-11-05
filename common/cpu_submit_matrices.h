#pragma once
#include "perf_hook_state.h"

namespace bg3::cpu_opt {
// Persist address lookup slots across submissions, but refresh source rows on
// the first use in EVERY submit. Never reuse a matrix merely by its address.
// Collisions in the current submit go to its owning map so no source can be
// evicted and sampled twice during one submit (including concurrent host writes).
class SubmitMatrices {
  struct Entry {perf::MatrixSource source{};uint64_t epoch=0,submit=0;perf::MatrixValue value;};
  std::array<Entry,8192> entries{};
  std::unordered_map<perf::MatrixSource,perf::MatrixValue,perf::MatrixHash> overflow;
  size_t unique=0;
public:
  void begin(){unique=0;overflow.clear();}
  template<class Load>
  const perf::MatrixValue& get(const perf::MatrixSource& source,uint64_t epoch,uint64_t submit,Load&& load){
    auto& entry=entries[perf::MatrixHash{}(source)%entries.size()];
    auto gather=[&](perf::MatrixValue& value){
      for(unsigned k=0;k<3;++k)load(value.rows.data()+4*k,source.rows[k]);
      value.valid=perf::valid_matrix(value.rows.data());++unique;
    };
    if(entry.submit==submit){
      if(entry.epoch==epoch && entry.source==source)return entry.value;
      auto [it,inserted]=overflow.try_emplace(source);if(inserted)gather(it->second);return it->second;
    }
    entry.source=source;entry.epoch=epoch;entry.submit=submit;gather(entry.value);return entry.value;
  }
  size_t size()const{return unique;}
};
}
