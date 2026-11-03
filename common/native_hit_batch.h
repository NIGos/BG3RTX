#pragma once
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace bg3::native_hit::batch {
// CmdUpdateBuffer accepts at most 64 KiB. Keep sparse row holes (and the
// publication word between the two row domains) out of the upload entirely.
template<class Row> struct RowUploads {
 static_assert(sizeof(Row)%4==0 && sizeof(Row)<=65536);
 struct Entry {uint64_t offset;std::size_t index;};
 struct Run {uint64_t offset;std::size_t first,count;};
 std::vector<Entry> entries;
 std::vector<Row> staged;
 std::vector<Row> rows;
 std::vector<Run> runs;
 void reserve(std::size_t count){entries.reserve(count);staged.reserve(count);}
 void add(uint64_t offset,const Row& row){entries.push_back({offset,staged.size()});staged.push_back(row);}
 void finish(){
  std::stable_sort(entries.begin(),entries.end(),[](const auto& a,const auto& b){return a.offset<b.offset;});
  rows.clear();runs.clear();rows.reserve(entries.size());
  for(const auto& e:entries){
   if(runs.empty() || runs.back().offset+runs.back().count*sizeof(Row)!=e.offset ||
      (runs.back().count+1)*sizeof(Row)>65536)runs.push_back({e.offset,rows.size(),0});
   rows.push_back(staged[e.index]);++runs.back().count;
  }
 }
};
// Each join writes a distinct row and two disjoint query-basis slices. Reorder
// only these independent jobs; the common CS barrier still precedes all remaps.
struct JoinJob {uint32_t chunk;std::array<uint32_t,8> push;uint32_t groups;};
inline void group_chunks(std::vector<JoinJob>& jobs){
 std::stable_sort(jobs.begin(),jobs.end(),[](const auto& a,const auto& b){return a.chunk<b.chunk;});
}
}
