#pragma once
#include <cstdint>
#include <mutex>
#include <memory>
#include <utility>
namespace bg3::native_hit {
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
