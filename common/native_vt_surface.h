#pragma once
// Exact post-sampling field decode of Base_AlphaTest_VT Deferred ST/STI.
// Native recipe evidence: tests/native_vt_surface_reference.json. This is NOT a
// VT sampler, coverage evaluator, BSDF or world-space normal reconstruction.
#include <array>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include "native_vt_fields.h"
namespace bg3::native_vt {
using V3=std::array<float,3>;using V4=std::array<float,4>;
struct Recipe {uint64_t fs,vs;uint32_t transform_offset,tileset_index_offset;};
inline constexpr Recipe recipes[]={
 {0x8995da1e4060f53bull,0x3812bc980438747full,64,100},
 {0x14678345c10aefbcull,0x2d2ac3c38992b7f0ull,0,32}};
inline const Recipe* recipe(uint64_t fs){for(const auto& r:recipes)if(r.fs==fs)return &r;return nullptr;}
// Both deferred variants: sampler 1:2, table 1:3, normal 1:4, albedo 1:5,
// physical 1:6; Reflectance is material UBO 1:1 byte 0. Engine UBO is 1:0.
struct Samples {
 V4 albedo{},normal{},physical{};
 uint32_t available_layers{}; // Bits 0/1/2: actual albedo/normal/physical samples.
 bool native_view_interpretation{}; // Includes linear albedo from the ACTUAL view.
 float tileset_magic{}; // Native tileset [0][2].w must equal 2202.
 bool reflectance_available{};float reflectance{};
};
enum class Status {SupportedFields,UnsupportedShader,MissingSamples,UnknownViewInterpretation,
                   MissingTileset,MissingReflectance,InvalidSample,InvalidNormal};
enum class Coverage {Unknown};
struct Surface {
 V3 albedo{},native_frame_normal{}; // Normal weights multiply native columns(T,N,B).
 float perceptual_roughness{},reflectance{},metalness{},albedo_aux{};
 Coverage coverage{Coverage::Unknown}; // DEF has no discard; depth-pass coverage is separate.
};
// Shared, proven post-sampler arithmetic for the audited VT and multi-texture
// recipes. This function does not validate a shader or certify a sampled view.
inline Status decode_sample_fields(const Samples& in,Surface& result){
 result={};
 if((in.available_layers&7u)!=7u)return Status::MissingSamples;
 if(!in.native_view_interpretation)return Status::UnknownViewInterpretation;
 if(!in.reflectance_available)return Status::MissingReflectance;
 for(const auto* layer:{&in.albedo,&in.normal,&in.physical})for(float x:*layer)if(!std::isfinite(x))return Status::InvalidSample;
 if(!std::isfinite(in.reflectance))return Status::InvalidSample;
 // Native FS: (_normal.wzy*2-1), then negate third component, normalize.
 const auto fields=transport::nv_fields({in.albedo[0],in.albedo[1],in.albedo[2]},
   {in.normal[3],in.normal[2],in.normal[1]},{in.physical[0],in.physical[1],in.physical[2]},in.reflectance);
 if(fields.status==2)return Status::InvalidNormal;
 if(fields.status)return Status::InvalidSample;
 Surface decoded{};decoded.native_frame_normal={fields.frame_normal.x,fields.frame_normal.y,fields.frame_normal.z};
 decoded.albedo={fields.albedo.x,fields.albedo.y,fields.albedo.z};
 decoded.perceptual_roughness=fields.roughness;decoded.reflectance=fields.reflectance;decoded.metalness=fields.metalness;
 decoded.albedo_aux=fields.aux; // Native albedo target alpha; no invented AO meaning.
 result=decoded;return Status::SupportedFields;
}
inline Status decode(uint64_t fs,const Samples& in,Surface& result){
 result={};
 if(!recipe(fs))return Status::UnsupportedShader;
 if((in.available_layers&7u)!=7u)return Status::MissingSamples;
 if(!in.native_view_interpretation)return Status::UnknownViewInterpretation;
 if(in.tileset_magic!=2202.f)return Status::MissingTileset;
 if(!in.reflectance_available)return Status::MissingReflectance;
 return decode_sample_fields(in,result);
}
}
