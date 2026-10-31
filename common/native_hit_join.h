#pragma once
// One recording's exact direct-draw identity. Duplicates never use first/last wins.
// This CPU association is necessary but not sufficient: the consumer additionally
// compares the actual prepass/deferred post-VS triangle clip positions and UVs.
#include <array>
#include <cstddef>
#include <cstdint>
#include <unordered_map>
namespace bg3::native_hit {
struct DrawKey {
  std::array<uint64_t,60> words{};
  bool operator==(const DrawKey&) const = default;
};
struct DrawHash {size_t operator()(const DrawKey& key) const {
  uint64_t h=1469598103934665603ull;
  for(auto w:key.words){h^=w;h*=1099511628211ull;}return size_t(h);
}};
struct Association {uint32_t record{},pre_count{},deferred_count{};};
class DrawJoin {
  uint64_t command_{},recording_{};
  std::unordered_map<DrawKey,Association,DrawHash> rows_;
public:
  void begin(uint64_t command,uint64_t recording){command_=command;recording_=recording;rows_.clear();}
  bool current(uint64_t command,uint64_t recording) const {
    return command && recording && command==command_ && recording==recording_;
  }
  bool note(uint64_t command,uint64_t recording,const DrawKey& key,uint32_t record){
    if(!current(command,recording))return false;
    auto& row=rows_[key];
    if(row.pre_count!=UINT32_MAX)++row.pre_count;
    if(row.pre_count==1)row.record=record;
    return row.pre_count==1;
  }
  bool claim(uint64_t command,uint64_t recording,const DrawKey& key,uint32_t& record){
    if(!current(command,recording))return false;
    auto i=rows_.find(key);if(i==rows_.end())return false;
    auto& row=i->second;if(row.deferred_count!=UINT32_MAX)++row.deferred_count;
    record=row.record;return row.pre_count==1 && row.deferred_count==1;
  }
  bool assign(uint64_t command,uint64_t recording,const DrawKey& key,uint32_t record){
    if(!current(command,recording))return false;
    auto i=rows_.find(key);if(i==rows_.end() || i->second.pre_count!=1)return false;
    i->second.record=record;return true;
  }
  bool unique_source(const DrawKey& key,uint32_t& record) const {
    auto i=rows_.find(key);if(i==rows_.end() || i->second.pre_count!=1 || i->second.record==UINT32_MAX)return false;
    record=i->second.record;return true;
  }
  bool unique(const DrawKey& key,uint32_t record) const {
    auto i=rows_.find(key);return i!=rows_.end() && i->second.record==record &&
      i->second.pre_count==1 && i->second.deferred_count==1;
  }
};
// Independent packets can be created in either CPU recording order. Local
// deferred snapshot indices never substitute for source AlphaRec indices.
inline bool join_packets(const DrawJoin& source,const DrawJoin& deferred,const DrawKey& key,
                         uint32_t deferredRecord,uint32_t& sourceRecord){
 return deferred.unique(key,deferredRecord) && source.unique_source(key,sourceRecord);
}
inline bool byte_range(uint64_t offset,uint64_t size,uint64_t total){return offset<=total && size<=total-offset;}
// One-time native recording ownership. Submission reservations pin the exact
// generation across the real driver call; failed submits never become complete.
struct RecordingOwner {
 uint64_t command{},recording{},submitted_frame{};uint32_t reservations{};
 bool submitted{},poisoned{};
 bool reserve(){if(reservations==UINT32_MAX){poisoned=true;return false;}++reservations;return true;}
 void finish(bool success,uint64_t frame){
  if(!reservations){poisoned=true;return;}--reservations;
  if(success){if(submitted)poisoned=true;submitted=true;submitted_frame=frame;}
 }
 bool retired(uint64_t currentRecording,uint64_t gpuDone) const {
  if(poisoned || reservations)return false;
  if(!submitted)return currentRecording!=recording; // known reset/free, never submitted
  return submitted_frame<=UINT64_MAX-2 && gpuDone>=submitted_frame+2;
 }
};
}
