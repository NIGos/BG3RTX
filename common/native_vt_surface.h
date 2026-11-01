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
// Engine UBO (1:0) byte offsets: UV transform p0 (p1 = mip clamp follows at +16), tile-set
// index, light channel word (&7). 8995: v[4]/v[5]/words.y/words.z; STI 14678345: _m0[0]/_m0[1]/_m1/_m2.
// reflectance: nv_reflectance mode (native_vt_fields.h). The variants differ from their base FS
// (8995 or 14678345) only in that RT2.y term; the rest of the dumped GLSL is identical after
// renaming (audit 2026-10-31, captures/alpha_cache, PLAN_M4 "next families").
enum Reflectance : uint32_t {ReflectanceUbo=0,ReflectanceConstant=1,ReflectanceNormalMix=2};
// capture: whether the layer has a geometry path for the pair. CaptureNone: the FS decode is
// audited but its VS is neither the per-draw VS 3812bc98 nor class A (spirv_xfb.h
// analyze_vs_instancing), so its meshes are never pooled and its instanced draws have no
// per-draw row: counted in "[hit material families]", never qualified (PLAN_M4 "next families").
enum Capture : uint32_t {CapturePool=0,CaptureNone=1};
// layout: LayoutBase = the 8995 sampling (sampler 1:2, table 1:3, normal/albedo/physical 1:4..6);
// LayoutDetail = ENV_DetailMap_VT (native_vt_detail below): the same VT layers at 1:4..7 plus a
// 2D detail normal 1:3 and a 2D detail mask 1:8 sampled with sampler 0:7.
enum Layout : uint32_t {LayoutBase=0,LayoutDetail=1};
// reflectance_offset: byte of the reflectance float in the material UBO 1:1 (FS 8e49cde8: 16).
// two_sided: the FS multiplies the N column by (gl_FrontFacing ? 1 : -1) (nv_two_sided_sign).
struct Recipe {uint64_t fs,vs;uint32_t transform_offset,tileset_index_offset,channel_offset;uint32_t reflectance=ReflectanceUbo;
 uint32_t reflectance_offset=0;uint32_t capture=CapturePool;uint32_t layout=LayoutBase;bool two_sided=false;};
inline constexpr Recipe recipes[]={
 {0x8995da1e4060f53bull,0x3812bc980438747full,64,100,104},
 {0x14678345c10aefbcull,0x2d2ac3c38992b7f0ull,0,32,36},
 {0x3da860bebbe9133bull,0x3812bc980438747full,64,100,104,ReflectanceConstant},   // per-draw, 8995 with 0.5
 {0x3df678c09f13ea60ull,0x2d2ac3c38992b7f0ull,0,32,36,ReflectanceConstant},      // pool, 14678345 with 0.5
 {0x8165785442a20e57ull,0x2d2ac3c38992b7f0ull,0,32,36,ReflectanceNormalMix},     // pool, mix(0.5, n.x, p)
 // ATM_Wind_Vertex_RG_VT STI: 14678345 with RT2.y = clamp(1:1 _m4) (byte 16 of a 32 B UBO). VS
 // 50fbfceb: same UV/N/B/T outputs as 2d2ac3c3 (inputs moved: rows 0..2, P 4, UV 5, quaternion
 // 6), but W = M P + wind(time, vertex color RG, distance fade) before the clip transform: not
 // class A, so no pool mesh (and no static mesh to check the raster positions against).
 {0x8e49cde81b680054ull,0x50fbfceb06192473ull,0,32,36,ReflectanceUbo,16,CaptureNone},
 // Base_AlphaTest_2s_VT STI: 14678345 with the engine UBO 1:0 {vec4 p[2]; vec3 offset @32; uint
 // tile-set index @44, flag @48 (bit 3), channel @52} and mat3(T, N * (gl_FrontFacing ? 1 : -1), B)
 // (two_sided). Classifier: _m1@32->_m2@44, _m2@36->_m3@48, _m3@40->_m4@52; the FS reads
 // `(_m3 & 7) << 4` (channel @48) and `_m4 != 0` (flag @52). VS 1e9145e4: UV/N/B/T as 2d2ac3c3,
 // but each matrix row's translation gets the per-draw engine offset (1:0 @32) added before
 // W = M P: not class A (the matrix is not the plain instance rows), never pooled.
 {0x923131c7039f33daull,0x1e9145e4b621a5e0ull,0,44,48,ReflectanceUbo,0,CaptureNone,LayoutBase,true}};
inline const Recipe* recipe(uint64_t fs){for(const auto& r:recipes)if(r.fs==fs)return &r;return nullptr;}
// Material UBO 1:1 bytes the recipe reads (0: no 1:1 binding).
inline uint32_t material_bytes(const Recipe& r){return r.reflectance==ReflectanceConstant?0u:(r.layout==LayoutDetail?20u:r.reflectance_offset+4u);}
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
