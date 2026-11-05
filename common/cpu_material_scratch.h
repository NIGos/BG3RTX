#pragma once
#include <algorithm>
#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace bg3::cpu_opt {
// Per-query indices only. Never retain source pointers or reuse an epoch's rows.
// Duplicate records preserve find_if's first winner; out-of-range records use
// the original scan, so malformed inputs cannot change association semantics.
template<uint32_t Limit> class FirstRecordIndex {
  std::vector<size_t> first_;
public:
  template<class Rows> void build(const Rows& rows) {
    size_t extent=0;
    for(const auto& row:rows)if(row.record<Limit)extent=(std::max)(extent,size_t(row.record)+1);
    first_.assign(extent, size_t(-1));
    for(size_t i=0;i<rows.size();++i)
      if(rows[i].record<Limit && first_[rows[i].record]==size_t(-1))first_[rows[i].record]=i;
  }
  template<class Rows> auto find(const Rows& rows,uint32_t record) const {
    if(record>=Limit)return std::find_if(rows.begin(),rows.end(),[&](const auto& r){return r.record==record;});
    if(record>=first_.size())return rows.end();
    const size_t i=first_[record];return i==size_t(-1)?rows.end():rows.begin()+i;
  }
};
// A material references at most nine table views. Stage unique additions on the
// stack; the caller commits only after the entire material fits the capacity.
template<class T,size_t Limit> class UniqueAppend {
  std::array<const T*,Limit> added_{};
  size_t size_{};
public:
  template<class Rows,class Same> void need(const T& value,const Rows& rows,Same same) {
    if(std::any_of(rows.begin(),rows.end(),[&](const auto& row){return same(row,value);}) ||
       std::any_of(begin(),end(),[&](const T* row){return same(*row,value);}))return;
    assert(size_<Limit);
    added_[size_++]=&value; // caller's statically bounded material layout
  }
  bool fits(size_t current,size_t capacity)const{return current<=capacity && size_<=capacity-current;}
  size_t size()const{return size_;}
  auto begin()const{return added_.begin();}
  auto end()const{return added_.begin()+size_;}
};
}
