// Native NR guide contract. DCS Control's normalized-motion/jitter convention,
// reduced to one full-frame post-upscale history. No Vulkan or NGX dependency.
#pragma once
#include <cmath>
#include <cstdint>
namespace bg3::dlss5_frame {
// CreateFeature records GPU initialization. A handle is usable by later
// recordings only after that initialization was actually submitted.
enum class Creation { Ready, AwaitSubmit, Discarded };
constexpr Creation creation(bool submitted,bool recording_retained) {
  return submitted?Creation::Ready:recording_retained?Creation::AwaitSubmit:Creation::Discarded;
}
enum class Boundary { Off, Bypass, RrFailed, PostFallback, InsidePass, Secondary, MissingGuides, Debug, Ready };
inline Boundary boundary(bool enabled,bool bypass,bool rr_success,bool use_post,bool in_pass,bool primary,bool guides,bool debug) {
  if(!enabled)return Boundary::Off;if(bypass)return Boundary::Bypass;if(!rr_success)return Boundary::RrFailed;
  if(!use_post)return Boundary::PostFallback;if(in_pass)return Boundary::InsidePass;if(!primary)return Boundary::Secondary;
  if(!guides)return Boundary::MissingGuides;if(debug)return Boundary::Debug;return Boundary::Ready;
}
// Vulkan 1.2 permits the EXT and core BDA FEATURE structs together (the
// extension names conflict instead). Keep existing nodes; append each missing
// kind once. NGX device-address allocations also need the core feature.
struct BdaPlan {bool valid,add_ext,add_core;};
constexpr BdaPlan bda_plan(bool ext_enabled,bool ext_chained,bool core_chained,bool vulkan12_chained){
  if(ext_enabled && vulkan12_chained)return {false,false,false};
  return {true,ext_enabled && !ext_chained,!core_chained && !vulkan12_chained};
}
// CPU recording is not GPU execution: only a successful submission containing
// the initial transition can commit the persistent image's known layout.
struct LayoutState {
  bool general{},transition_recorded{};
  void begin_recording(){transition_recorded=false;}
  void record_transition(){transition_recorded=true;}
  void submitted(bool success){general=general || (success && transition_recorded);}
};
struct Inputs {
  uint32_t render[2]{}, output[2]{}, depth_extent[2]{}, motion_extent[2]{};
  uint32_t depth_base[2]{}, motion_base[2]{}, motion_size[2]{};
  float scale[2]{}, jitter[2]{};
  bool jittered{}, reset{};
};
// Immutable feature/recording-image contract. Jitter and reset vary per frame;
// geometry, source formats and motion convention require fresh NGX history.
struct FeatureKey {
  Inputs guides{};
  uint32_t color_format{},depth_format{},motion_format{};
  bool hdr{},inverted{};
};
inline bool recreate(const FeatureKey& a,const FeatureKey& b) {
  if(a.color_format!=b.color_format || a.depth_format!=b.depth_format ||
     a.motion_format!=b.motion_format || a.hdr!=b.hdr || a.inverted!=b.inverted ||
     a.guides.jittered!=b.guides.jittered)return true;
  for(unsigned i=0;i<2;++i) {
    const auto& x=a.guides;const auto& y=b.guides;
    if(x.render[i]!=y.render[i] || x.output[i]!=y.output[i] ||
       x.depth_extent[i]!=y.depth_extent[i] || x.motion_extent[i]!=y.motion_extent[i] ||
       x.depth_base[i]!=y.depth_base[i] || x.motion_base[i]!=y.motion_base[i] ||
       x.motion_size[i]!=y.motion_size[i] || x.scale[i]!=y.scale[i])return true;
  }
  return false;
}
struct Guides {
  uint32_t output[2], region_base[2], region_size[2], processing[2];
  uint32_t motion_full[2], depth_full[2];
  float motion_origin[2], depth_origin[2], motion_scale[2], motion_offset[2];
};
static_assert(sizeof(Guides)==80);
inline bool prepare(const Inputs& in, const float previous_jitter_uv[2], bool history, Guides& g) {
  g = {};
  for(unsigned axis=0;axis<2;++axis) {
    if(!in.render[axis] || !in.output[axis] || !in.motion_size[axis] ||
       !std::isfinite(in.scale[axis]) || !std::isfinite(in.jitter[axis]) ||
       in.depth_base[axis]>in.depth_extent[axis] || in.render[axis]>in.depth_extent[axis]-in.depth_base[axis] ||
       in.motion_base[axis]>in.motion_extent[axis] || in.motion_size[axis]>in.motion_extent[axis]-in.motion_base[axis])return false;
    g.output[axis]=g.region_size[axis]=g.processing[axis]=in.output[axis];
    g.motion_full[axis]=in.motion_size[axis];g.depth_full[axis]=in.render[axis];
    g.motion_origin[axis]=float(in.motion_base[axis]);g.depth_origin[axis]=float(in.depth_base[axis]);
    g.motion_scale[axis]=in.scale[axis]/float(in.render[axis]);
    if(in.jittered && history && !in.reset) {
      if(!std::isfinite(previous_jitter_uv[axis]))return false;
      g.motion_offset[axis]=in.jitter[axis]/float(in.render[axis])-previous_jitter_uv[axis];
    }
  }
  return true;
}
} // namespace bg3::dlss5_frame
