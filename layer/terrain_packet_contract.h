#pragma once
#include <cstdint>
#include "../common/native_hit_join.h"
namespace bg3::terrain_packet {
// One exact opt-in packet, using the same reservation contract as native-hit.
// GPU completion for submitted packets is established separately by queue idle.
struct Lifetime {
 native_hit::RecordingOwner owner{};
 bool discarded{};
 bool reserve(uint64_t currentRecording){
  return !discarded && currentRecording==owner.recording && owner.recording && owner.reserve();
 }
 void finish(bool success,uint64_t frame){owner.finish(success,frame);}
 void discard(){discarded=true;}
 bool pending()const{return owner.reservations!=0;}
 bool release_unsubmitted()const{return discarded && !owner.submitted && !pending() && !owner.poisoned;}
 bool finishable()const{return owner.submitted && !pending() && !owner.poisoned;}
};
inline uint64_t buffer_prefix(uint64_t size,uint64_t offset,uint64_t range,uint64_t minimum,uint64_t maximum,uint64_t stride){
 if(!stride || !minimum || minimum>maximum || offset%4 || offset>=size)return 0;
 const uint64_t available=size-offset;
 if(range==UINT64_MAX)range=available;
 if(range>available || range<minimum)return 0;
 uint64_t bytes=range<maximum?range:maximum;bytes-=bytes%stride;
 return bytes>=minimum && bytes%4==0?bytes:0;
}
inline bool overlap(uint64_t a,uint64_t n,uint64_t b,uint64_t m){
 if(!n||!m)return false;
 return a<=b ? b-a<n : a-b<m; // no offset+length overflow
}
}
