#pragma once
// Owning specialization payload: worker closures copy values, never render-stack pointers.
#include <vulkan/vulkan.h>
#include <cassert>
#include <cstdint>
#include <string>
#include "settings.h"
#include "shader_variant.h"
namespace bg3::pipe_async {
struct PipelineSpecs {
  uint32_t data[16]{};
  VkSpecializationMapEntry entries[16]{};
  uint32_t count = 0;
  void add(uint32_t id, uint32_t value) {
    assert(count < 16);
    entries[count] = {id, count * uint32_t(sizeof(uint32_t)), sizeof(uint32_t)};
    data[count++] = value;
  }
  std::string key() const {
    std::string out;
    for (uint32_t i = 0; i < count; ++i) out += "#" + std::to_string(entries[i].constantID) + "=" + std::to_string(data[i]);
    return out;
  }
};
// Startup alternatives for the actual visible Experimental choices, not a Cartesian product.
// Controls such as RR order, visibility and pool trim change data/dispatch, not specialization.
// opt_prewarm is an explicit job set: bit 0 roof reject, bit 1 lazy origin normal.
// Ordinary release startup (dev/detailed logging/mask all off) adds neither candidate.
inline std::vector<PipelineSpecs> experimental_specs(const PipelineSpecs& base, bool dev,
                                                    bool detailed_logging = false, uint32_t opt_prewarm = 0) {
  using namespace shader_variant;
  struct Binding { const char* key; uint32_t id; };
  static constexpr Binding bindings[] = {
      {"rt_native_scalp", kSpecIdScalpHair}, {"rt_native_hair", kSpecIdScalpHair},
      {"rt_native_tileset", kSpecIdTileset}, {"rt_water_compose_shift", kSpecIdWaterComposeShift},
      {"rt_hair_groom", kSpecIdHairGroom},
      {"rt_hair_groom_shadow_t", kSpecIdHairGroomShadowT}};
  std::vector<PipelineSpecs> out;
  for (const auto& knob : settings::kKnobs) {
    if (!knob.experimental || !settings::knob_visible(knob, dev)) continue;
    for (int c = 0; c < settings::choice_count(knob); ++c) {
      const auto& choice = knob.choices[c];
      if (!settings::choice_visible(knob, c, dev) || choice.apply == settings::Apply::Restart ||
          choice.apply == settings::Apply::Unspecified) continue;
      auto alt = base;
      for (const auto& kv : choice.kv) {
        if (!kv.key) break;
        for (const auto& b : bindings) if (std::string(kv.key) == b.key)
          for (uint32_t i = 0; i < alt.count; ++i)
            if (alt.entries[i].constantID == b.id) alt.data[i] = uint32_t(std::atoi(kv.value));
      }
      if (alt.key() == base.key()) continue;
      bool seen = false;
      for (const auto& prior : out) if (prior.key() == alt.key()) seen = true;
      if (!seen) out.push_back(alt);
    }
  }
  static constexpr Binding candidates[] = {
      {"gi_roof_reject", kSpecIdGiRoofReject}, {"rt_origin_lazy_normal", kSpecIdOriginLazyNormal}};
  for (uint32_t c = 0; c < 2; ++c) {
    if (!dev && !detailed_logging && !(opt_prewarm & (1u << c))) continue;
    for (uint32_t i = 0; i < base.count; ++i) {
      if (base.entries[i].constantID != candidates[c].id) continue;
      auto alt = base;
      alt.data[i] = base.data[i] ? 0u : 1u;
      bool seen = false;
      for (const auto& prior : out) if (prior.key() == alt.key()) seen = true;
      if (!seen) out.push_back(alt);
    }
  }
  return out;
}
}  // namespace bg3::pipe_async
