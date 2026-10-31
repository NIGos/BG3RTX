#pragma once
#include <cstdint>
#include <array>
#include <string_view>

// A presentation on another thread must not relabel a partially recorded frame.
struct PanEpoch {
  uint64_t recording = 0;
  uint64_t frame = UINT64_MAX;
  int serial = 0;
  void clear() { *this = {}; }
  void bind(uint64_t rec, uint64_t index, int burst) {
    recording = rec; frame = index; serial = burst;
  }
  bool matches(uint64_t rec, int burst) const {
    return recording != 0 && recording == rec && serial == burst && frame != UINT64_MAX;
  }
};

// The first tonemap can already be recorded when depth arms a burst. Capture
// one explicit warmup plus 16 retained frames, not 15 final images labeled K16.
struct PanBurstSamples {
  static constexpr uint32_t kept_frames=16, raw_frames=17;
  uint64_t warmup;
  struct Frame {uint32_t mask=0;uint64_t lighting_recording=0;};
  std::array<Frame,kept_frames> frames{};
  bool invalid=false;
  bool retained(uint64_t frame) const {return frame>warmup && frame-warmup<=kept_frames;}
  void add(uint64_t frame,std::string_view name,uint64_t recording){
    if(!retained(frame))return;
    constexpr std::string_view names[]={"depth","normal","albedo","material","primary","sun","local","gi","scene_pose","final"};
    for(uint32_t i=0;i<10;++i)if(name==names[i]){
      auto& f=frames[frame-warmup-1];
      if(!recording || (f.mask&(1u<<i)))invalid=true;
      if(i!=9){if(f.lighting_recording && f.lighting_recording!=recording)invalid=true;f.lighting_recording=recording;}
      f.mask|=1u<<i;return;
    }
  }
  bool complete() const {if(invalid)return false;for(const auto& f:frames)if(f.mask!=1023)return false;return true;}
};
