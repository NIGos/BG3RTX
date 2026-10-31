#pragma once
// Hit-material qualification POLICY: which native draws/pipelines may feed the
// RT hit-material capture. Pure decisions on plain facts gathered by the layer
// (layer/geocapture.cpp geo_pre_draw, layer/hit_material.cpp). No Vulkan types:
// the layer keeps every Vulkan object and only passes numbers and flags here.
// tests/native_hit_qualify_test.cpp pins the current decisions.
#include <cstdint>
namespace bg3::native_hit {
inline constexpr uint32_t kColorMaskRequired = 0xf73u;  // all audited colour targets written
inline constexpr uint32_t kTilesetMinStride = 80u, kStaticMinStride = 64u;
inline constexpr uint32_t kTilesetOutputs = 3u;
// Facts about one native graphics pipeline (PipeInfo in geocapture.cpp).
struct MaterialPipelineFacts {
  bool tileset = false;            // tileset_deferred(vs,fs)
  bool tileset_capture = false;    // tileset capture enabled at startup
  uint32_t stride = 0;             // XFB vertex stride
  uint32_t tileset_outputs = 0;
  bool tileset_inputs_valid = false;
  uint32_t vt_outputs = 0;
  bool xfb = false, material_state = false;
  int depth_test = -1;             // -1 missing or dynamic
  uint32_t blend_mask = 0, color_mask = 0;
  bool triangle_list = false;      // topology == TRIANGLE_LIST
  bool uv_ok = false;              // uv_match == UV_OK
};
inline bool material_pipeline_outputs(const MaterialPipelineFacts& p) {
  return p.tileset ? p.tileset_capture && p.stride >= kTilesetMinStride && p.tileset_outputs == kTilesetOutputs &&
                         p.tileset_inputs_valid
                   : p.vt_outputs != 0u;
}
// Exact-pipeline accept rule for the deferred material draw.
inline bool exact_material_pipeline(const MaterialPipelineFacts& p) {
  return p.xfb && material_pipeline_outputs(p) && p.material_state && p.depth_test == 1 && p.blend_mask == 0 &&
         (p.color_mask & kColorMaskRequired) == kColorMaskRequired && p.triangle_list && p.uv_ok;
}
// Deferred draw qualification (deferred_begin), in its original order.
enum class DeferredQualify { Accept, FamilyDeclined, PipelineDeclined };
inline DeferredQualify deferred_qualify(bool audited_family, bool tileset, bool exact_pipeline, uint32_t kind,
                                        uint32_t instances, uint32_t stride) {
  if (!audited_family) return DeferredQualify::FamilyDeclined;
  if (!exact_pipeline || kind > 1 || (!tileset && instances != 1) ||
      stride < (tileset ? kTilesetMinStride : kStaticMinStride))
    return DeferredQualify::PipelineDeclined;
  return DeferredQualify::Accept;
}
// Depth prepass draw geometry (note_prepass): vertices is the checked
// count*instances (tileset_vertex_count), triangles the captured count.
inline bool prepass_geometry_ok(bool tileset, uint32_t vertices, uint32_t instances, uint32_t triangles,
                                uint32_t stride) {
  return vertices == uint64_t(triangles) * 3 && (tileset || instances == 1) && (!tileset || stride >= kTilesetMinStride);
}
// Alpha record of a prepass draw (note_prepass): tileset kind 5 with zero
// p0.yz, otherwise the plain kind 1 on channel 0.
inline bool prepass_alpha_ok(bool tileset, uint32_t kind, uint32_t channel, float p0_y, float p0_z) {
  return tileset ? kind == 5 && p0_z == 0.f && p0_y == 0.f : kind == 1 && channel == 0;
}
// Opaque depth prepass draw (note_opaque_prepass), excluding enable/family/one-time checks.
inline bool opaque_prepass_geometry_ok(uint32_t kind, uint32_t instances, uint32_t count, uint32_t triangles,
                                       uint32_t stride) {
  return kind <= 1 && instances == 1 && count == uint64_t(triangles) * 3 && stride >= kStaticMinStride;
}
} // namespace bg3::native_hit
