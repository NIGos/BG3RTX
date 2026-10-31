#pragma once
// CPU evidence only. Recording IDs are exact generations, never handles/frame ages.
// The owner supplies successfully submitted producer evidence and a current batch
// serialized on the actual graphics queue. GPU barriers remain caller-owned.
#include <cstdint>
#include <cstddef>
#include <span>
#include <unordered_set>
namespace bg3::native_hit {
struct Evidence {uint64_t recording{};bool submitted{};uint64_t queue{};};
inline bool ordered_before_query(Evidence source,Evidence deferred,uint64_t queryRecording,
 uint64_t graphicsQueue,uint64_t currentQueue,std::span<const uint64_t> orderedRecordingIDs){
 if(!queryRecording || !source.recording || !deferred.recording || !graphicsQueue || currentQueue!=graphicsQueue ||
    source.recording==queryRecording || deferred.recording==queryRecording)return false;
 std::unordered_set<uint64_t> seen;size_t query=orderedRecordingIDs.size();
 for(size_t i=0;i<orderedRecordingIDs.size();++i){
  const auto id=orderedRecordingIDs[i];
  // Untracked unrelated CBs do not manufacture an owner or alter relative order.
  if(!id)continue;
  if(!seen.insert(id).second)return false;
  if(id==queryRecording)query=i;
 }
 if(query==orderedRecordingIDs.size())return false;
 auto before=[&](Evidence producer){
  if(producer.submitted)return producer.queue==graphicsQueue;
  for(size_t i=0;i<query;++i)if(orderedRecordingIDs[i]==producer.recording)return true;
  return false;
 };
 return before(source) && before(deferred);
}
// Called under the serialized submit lock. The query owns unique proof storage;
// do not reset this state or rewrite that storage until its actual retirement.
struct PendingPublication {
 bool sealed{},published{},pending{};
 uint32_t prepare(bool valid){
  if(sealed)return published?1u:0u; // A successfully submitted query word is immutable.
  if(pending){published=false;return 0u;} // Duplicate reservation cannot retain a proof.
  pending=true;published=valid;return published?1u:0u;
 }
 void finish(bool success){
  if(!pending)return;
  pending=false;
  if(success)sealed=true;
  else published=false; // A failed-good attempt cannot authorize a bad-order retry.
 }
};
}
