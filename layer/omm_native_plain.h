#pragma once
#include "host.h"
#include "omm_bake.h"
#include "omm_content.h"
#include "omm_sampler.h"
#include "../common/omm_native_plain_recipes.h"
namespace bg3::omm {
// Metadata captured at the real native draw, never reconstructed from cached FS.
struct NativePlainCapture {
  VkCommandBuffer command{}; uint64_t recording{},fs{},vs{};
  AlphaSnapshot alpha{}; ContentSource source{}; BoundSampler sampler{};
};
bool capture_native_plain(VkCommandBuffer,const AlphaFs&,uint64_t vs,const AlphaSnapshot&,NativePlainCapture*);
// Source must be verified post-VS triangle-list float4 position + float2 UV at16.
// The supplied owner retains that exact pool/range until backend retirement.
// This helper pins sampled native handles for this recording, not immutable bytes;
// GPU content epochs remain mandatory in record_plain and scene traversal.
bool make_native_plain_input(VkCommandBuffer,const NativePlainCapture&,VkBuffer vertices,
    VkDeviceSize bufferBytes,uint32_t offset,uint32_t stride,uint32_t triangles,
    std::shared_ptr<void> vertexOwner,PlainInput*);
}
