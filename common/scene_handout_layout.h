// Scene handout increment 1 (docs/SCENE_HANDOUT_DESIGN.md 3.2): the update-after-bind (UAB) policy of the
// material set 1 (layer/alphatest.cpp alpha_device_created) and the empty dummy TLAS (layer/rtscene.cpp).
// Shared with the windowless fixture tools/scene_handout_gpu_fixture.cpp, so the fixture checks the very layout
// flags and empty-TLAS build the layer uses. Header-only, Vulkan types only.
#pragma once
#include <vulkan/vulkan.h>
#include <cstdint>

namespace bg3::scene_handout {

// The device features set 1 needs as UAB: storage buffers (records, soup, InstInfo, pool, stamp, OMM epochs),
// sampled images (alpha views; the same feature covers SAMPLER bindings) and the OMM reference TLAS (binding 10).
inline bool uab_supported(const VkPhysicalDeviceDescriptorIndexingFeatures& di,
                          const VkPhysicalDeviceAccelerationStructureFeaturesKHR& as) {
  return di.descriptorBindingStorageBufferUpdateAfterBind && di.descriptorBindingSampledImageUpdateAfterBind &&
         as.descriptorBindingAccelerationStructureUpdateAfterBind;
}

// UAB on every binding the layer writes per frame; bindings made only of immutable samplers are never written,
// so they keep 0 (no update-after-bind flag needed, none requested).
inline void uab_binding_flags(const VkDescriptorSetLayoutBinding* bindings, uint32_t count, VkDescriptorBindingFlags* out) {
  for (uint32_t i = 0; i < count; ++i)
    out[i] = bindings[i].pImmutableSamplers ? 0u : VkDescriptorBindingFlags(VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT);
}

// The empty TLAS bound where a TLAS handle is required but the shaders read the scene by address (increments 2-3):
// one instance slot (a valid input address), zero instances built. Fill the build info for size query and build.
struct EmptyTlas {
  VkAccelerationStructureGeometryKHR geometry{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR};
  VkAccelerationStructureBuildGeometryInfoKHR build{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR};
  VkAccelerationStructureBuildRangeInfoKHR range{};
  static constexpr uint32_t kMaxInstances = 1;   // size query: room for one (never used) instance
  static constexpr VkDeviceSize kInstanceBytes = sizeof(VkAccelerationStructureInstanceKHR);
  explicit EmptyTlas(VkDeviceAddress instances = 0) {
    geometry.geometryType = VK_GEOMETRY_TYPE_INSTANCES_KHR;
    geometry.geometry.instances.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR;
    geometry.geometry.instances.data.deviceAddress = instances;
    build.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
    build.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
    build.geometryCount = 1;
    build.pGeometries = &geometry;
    range.primitiveCount = 0;
  }
  EmptyTlas(const EmptyTlas&) = delete;
  EmptyTlas& operator=(const EmptyTlas&) = delete;
};

}  // namespace bg3::scene_handout
