#pragma once
// Owning specialization payload: worker closures copy values, never render-stack pointers.
#include <vulkan/vulkan.h>
#include <cassert>
#include <cstdint>
#include <string>
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
}  // namespace bg3::pipe_async
