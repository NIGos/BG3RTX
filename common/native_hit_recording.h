#pragma once
#include <cstdint>
#include <mutex>
#include <memory>
#include <utility>
#include <algorithm>
#include <vector>
namespace bg3::native_hit {
// Only an already camera-validated query may be borrowed by the next frame.
inline bool borrow_query(uint64_t source_frame,uint64_t now,uint32_t query_slot,uint32_t slot,
                         uint32_t query_generation,uint32_t generation) {
 return now>source_frame && now-source_frame==1 && query_slot==slot && query_generation==generation;
}
struct QueryOwners {
 std::vector<uint64_t> ids;
 void add(uint64_t id){if(std::find(ids.begin(),ids.end(),id)==ids.end())ids.push_back(id);}
 template<class Retired> bool retired(Retired done) const {
  return !ids.empty() && std::all_of(ids.begin(),ids.end(),done);
 }
};
// Per-recording leaf lock: independent writers never wait behind query recording.
// Sealed snapshots survive command reset and registry retirement.
template<class T> struct RecordingList {
 const uint64_t command,recording;
 std::mutex mutex;
 T value;
 std::shared_ptr<const T> published;
 RecordingList(uint64_t c,uint64_t r,T initial):command(c),recording(r),value(std::move(initial)){}
 bool current(uint64_t c,uint64_t r) const {return c && r && c==command && r==recording;}
 std::shared_ptr<const T> seal(){
  std::lock_guard lock(mutex);
  if(!published)published=std::make_shared<const T>(value);
  return published;
 }
};
}
