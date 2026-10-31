#pragma once
// Exact ENV_Tileset_MultipleTextures_AlphaTest Deferred ST/STI only.
// Selection uses the interpolated native color, never a guessed mesh-name role.
// This does not sample textures, prove sampled-view colors, or evaluate coverage.
#include "native_vt_surface.h"
namespace bg3::native_tileset {
enum class Channel : uint32_t {Green,Red,Base};
enum class Status {Supported,UnsupportedShader,MissingColor,InvalidInput};
struct Plan {
 Channel channel{Channel::Base};std::array<float,2> uv{};
 uint32_t normal_binding{},albedo_binding{},physical_binding{};
};
inline bool recipe(uint64_t fs,uint64_t vs){
 return (fs==0x7621aebd092c3889ull && vs==0xaf1d30a11685c516ull) ||
        (fs==0xcad41eff979970e4ull && vs==0xa618ddae98c2d1cfull);
}
inline Status plan(uint64_t fs,uint64_t vs,bool color_known,std::array<float,2> color_rg,
                   std::array<float,2> uv,std::array<float,3> tiling,Plan& out){
 out={};if(!recipe(fs,vs))return Status::UnsupportedShader;
 if(!color_known)return Status::MissingColor;
 for(float x:color_rg)if(!std::isfinite(x))return Status::InvalidInput;
 // Strict native branch ordering: ANY positive green wins, then positive red.
 const unsigned channel=color_rg[1]>0.f?0u:color_rg[0]>0.f?1u:2u;
 if(!std::isfinite(tiling[channel]))return Status::InvalidInput;
 Plan p{};p.channel=Channel(channel);p.normal_binding=2u+channel;
 p.albedo_binding=5u+channel;p.physical_binding=8u+channel;
 for(unsigned i=0;i<2;++i){p.uv[i]=uv[i]*tiling[channel];if(!std::isfinite(uv[i]) || !std::isfinite(p.uv[i]))return Status::InvalidInput;}
 out=p;return Status::Supported;
}
// Caller supplies the selected branch's REAL post-sampler values and actual
// view-interpretation proof. No VT magic is required for this non-VT recipe.
inline native_vt::Status decode_selected(const native_vt::Samples& selected,native_vt::Surface& out){
 return native_vt::decode_sample_fields(selected,out);
}
}
