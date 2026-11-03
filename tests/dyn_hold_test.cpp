#include "../common/dyn_hold.h"
#include "../common/as_chunk_alloc.h"
#include "../common/hair_cost_epoch.h"
#include <deque>
#include <array>
#include <cstring>
int main() {
  for (uint64_t age = 1; age <= 4; ++age)
    if (!dyn_hold_alive(100 + age, 100, 4, false, true, true)) return 1;
  if (dyn_hold_alive(105, 100, 4, false, true, true)) return 2;
  if (dyn_hold_alive(101, 100, 0, false, true, true)) return 3;
  if (dyn_hold_alive(101, 100, 4, true, true, true)) return 4;
  if (dyn_hold_alive(101, 100, 4, false, false, true)) return 5;
  if (dyn_hold_alive(101, 100, 4, false, true, false)) return 6;
  if (dyn_hold_alive(100, 100, 4, false, true, true) ||
      dyn_hold_alive(99, 100, 4, false, true, true)) return 7;
  // Reappearing draws refresh age; holds themselves do not.
  if (!dyn_hold_alive(107, 103, 4, false, true, true)) return 8;
  // Expiry must not remove the last eligible absent frame, or underflow when
  // a scene is recorded at/before the producer's frame.
  if (dyn_hold_expired(100, 100, 4) || dyn_hold_expired(99, 100, 4) ||
      dyn_hold_expired(104, 100, 4) || !dyn_hold_expired(105, 100, 4)) return 9;
  if (!dyn_hold_expired(101, 100, 0)) return 10;
  for (bool hair : {false, true}) {
    if (dyn_slot_expired(220, 100, 4, false, hair) ||
        !dyn_slot_expired(221, 100, 4, false, hair)) return 11;
  }
  if (dyn_slot_expired(105, 100, 4, true, false) ||
      !dyn_slot_expired(105, 100, 4, true, true)) return 12;
  // Model upload-ring identity churn: one new 88-byte/vertex snapshot per
  // frame. Old policy pins 120 snapshots; trim pins only the four-frame hold
  // horizon plus one producer frame. Delayed GPU retirement stays separate.
  const auto peak = [](uint32_t horizon) {
    bg3::as_chunks::RangeAlloc ranges;
    constexpr uint64_t bytes = 3 * 88 * 64;
    ranges.init(bytes * 140);
    struct Live { uint64_t frame, off; };
    std::deque<Live> live, pending;
    uint64_t maximum = 0;
    for (uint64_t frame = 1; frame <= 1000; ++frame) {
      while (!live.empty() && dyn_hold_expired(frame, live.front().frame, horizon)) {
        pending.push_back({frame, live.front().off}); live.pop_front();
      }
      while (!pending.empty() && bg3::as_chunks::retired(pending.front().frame,
               frame, frame > 2 ? frame - 2 : 0, frame > 3 ? frame - 3 : 0)) {
        ranges.release(pending.front().off, bytes); pending.pop_front();
      }
      uint64_t off = 0;
      if (!ranges.alloc(bytes, 16, &off)) return uint64_t(-1);
      live.push_back({frame, off});
      if (ranges.used > maximum) maximum = ranges.used;
    }
    return maximum / bytes;
  };
  if (peak(120) != 127 || peak(4) != 11) return 13;
  for (uint32_t kind=0;kind<9;++kind) {
    const bool compact=kind>=1u && kind<=3u;
    if (dyn_hold_stride(88,true,true,kind)!=(compact?24u:88u)) return 14;
    if (dyn_hold_stride(88,false,true,kind)!=88u) return 15;
  }
  if (dyn_hold_stride(88,true,false,0)!=24u || dyn_hold_stride(64,true,false,0)!=64u) return 16;
  // Exact last vertex, truncation, zero vertices and overflow-safe rejection.
  if (!dyn_hold_pack_range(16,3,88,280) || dyn_hold_pack_range(16,3,88,279) ||
      !dyn_hold_pack_range(280,0,88,280) || dyn_hold_pack_range(281,0,88,280) ||
      dyn_hold_pack_range(UINT64_MAX,2,88,280) || dyn_hold_pack_range(0,UINT64_MAX,88,280) ||
      !dyn_hold_pack_range(0,3,16,48)) return 17;
  std::array<uint32_t,66> source{};  // three 88-byte vertices
  std::array<uint32_t,18> packed{};  // three 24-byte vertices
  for (uint32_t i=0;i<source.size();++i) source[i]=0x80000000u+i;
  const DynHoldPackJob job{0,0,3,88};
  for (uint32_t v=0;v<job.vertices;++v)
    std::memcpy(packed.data()+6u*v,source.data()+v*job.stride/4u,24);
  for (uint32_t v=0;v<3;++v) for (uint32_t c=0;c<6;++c)
    if (packed[v*6u+c]!=source[v*22u+c]) return 18;
  if (packed.size()*4u!=job.vertices*24u || source.size()*4u!=job.vertices*job.stride) return 19;
  HairCostEpoch epoch;
  if (epoch.update(0) || epoch.generation!=0) return 20;
  uint32_t samples=299;
  double sum=299.0*10.0;
  const uint64_t queued_off=epoch.generation;
  if (epoch.update(1u<<4)) { samples=0; sum=0; }
  // Delayed OFF results must not complete a mixed 300-frame average.
  if (epoch.accepts(queued_off)) { ++samples; sum+=10.0; }
  if (samples!=0 || sum!=0) return 21;
  for (uint32_t i=0;i<300;++i) if (epoch.accepts(epoch.generation)) { ++samples; sum+=2.0; }
  if (samples!=300 || sum/samples!=2.0) return 22;
  // OFF->ON->OFF still rejects an old OFF result (mask equality alone fails).
  if (!epoch.update(0) || epoch.accepts(queued_off)) return 23;
  const uint32_t mask=hair_cost_mask([](const char* key,int) {
    return std::strcmp(key,"rt_hair_pool_trim")==0;
  });
  if (mask!=(1u<<3)) return 24;
  return 0;
}
