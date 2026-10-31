#pragma once
#include <cstdint>
namespace bg3::omm {
// Scoped to one verified Mesh generation. IDs describe actual resources, never
// raw recyclable Vulkan handles. The audited recipe fixes all scalar factors.
struct MaterialKey {
  uint64_t fs{},vs{},image{},view{},sampler{};
  bool operator==(const MaterialKey&) const = default;
};
// Native bvh records: 0 pending, 1 submitted, 2 dropped, -1 unknown/pruned.
// Pending builds are usable only later in their OWN command recording.
inline bool cached_bake_ready(uint64_t producer,uint64_t consumer,int state) {
  return producer && consumer && (state==1 || (state==0 && producer==consumer));
}
}
