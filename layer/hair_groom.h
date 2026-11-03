#pragma once
// rt_hair_groom (docs/RTX_HAIR_INTEGRATION.md): the strand groom of matched native hair draws, traced as
// linear swept spheres (VK_NV_ray_tracing_linear_swept_spheres) by the secondary ray queries.
#include "../common/hair_groom_asset.h"
#include <vulkan/vulkan.h>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>
namespace bg3 { struct DeviceData; struct InstanceData; }
namespace bg3::hair_groom {
// Chained by rtscene after the ordinary AS/RQ prerequisites pass. rt_hair_groom=1 (restart) only.
const void* device_features(InstanceData*, VkPhysicalDevice, const void* next, std::vector<const char*>& extensions);
bool device_requested();
void disable_device_request();   // optional-device retry: ordinary RT and the card path are retained
void device_created(DeviceData*, bool rt_enabled);
// The device has the feature and the key was on at creation (the card path stays otherwise).
bool lss_enabled();

// The groom files (data/assetdb/hair_grooms/*.hgrm), loaded once on first use.
struct Loaded {
  Asset asset;
  std::vector<uint8_t> bytes;   // owns the views in `asset`
  std::string path;
};
const Loaded* find(uint64_t index_id, uint32_t index_count);
// Index counts of the loaded grooms: a draw with another count never matches (no hashing).
bool count_known(uint32_t index_count);
// The loaded grooms with this index count (the UV join's candidates).
std::vector<const Loaded*> with_count(uint32_t index_count);
}  // namespace bg3::hair_groom
