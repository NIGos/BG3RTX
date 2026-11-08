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
// Chained by rtscene after the ordinary AS/RQ prerequisites pass. Optional supported feature is reserved even with rt_hair_groom=0 for live enable.
const void* device_features(InstanceData*, VkPhysicalDevice, const void* next, std::vector<const char*>& extensions);
bool device_requested();
void disable_device_request();   // optional-device retry: ordinary RT and the card path are retained
void device_created(DeviceData*, bool rt_enabled);
// The device has the feature and the key was on at creation (the card path stays otherwise).
bool lss_enabled();

// The groom files (data/assetdb/hair_grooms/*.hgrm), indexed once on first use: header and UV samples only
// (`asset.lods` empty). The arrays are read from disk when a LOD is uploaded (read_full), then dropped.
struct Loaded {
  Asset asset;
  std::string path;
  uint64_t size = 0;           // the file's size at indexing
  uint64_t lod_at[9] = {};     // byte offset of each LOD's header; lod_at[lod_count] = the end (== size)
};
// One LOD of a groom (round 3 M6): its bytes only, validated (parse_lod); `lod` views into `bytes`. False when the
// file changed since indexing (size, header) or the LOD is malformed.
bool read_lod(const Loaded& l, uint32_t index, std::vector<uint8_t>& bytes, Lod& lod);
const Loaded* find(uint64_t index_id, uint32_t index_count);
// Index counts of the loaded grooms: a draw with another count never matches (no hashing).
bool count_known(uint32_t index_count);
// The loaded grooms with this index count (the UV join's candidates).
std::vector<const Loaded*> with_count(uint32_t index_count);
// Hair drawn without a groom (docs/RTX_HAIR_ROADMAP.md 2b): the converter does these styles first. note_seen is a
// set insert (called on the miss paths, which already log once per index count); flush_seen merges them into
// <assetdb dir>\hair_seen.txt ("<index count> <index id hex>", id 0 when unknown) at a level load and at device
// destroy, so the file is written at most once per level. tools/hair_groom/prepare.py parse_seen reads it.
void note_seen(uint32_t index_count, uint64_t index_id);
// At a level load (bvhsplit): index groom files written since (the converter may run while the game plays). New files
// only; returns how many were added.
uint32_t rescan();
void flush_seen();
std::string seen_path();
}  // namespace bg3::hair_groom
