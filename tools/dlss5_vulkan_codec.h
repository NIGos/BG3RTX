// Vulkan resource/dispatch subset adapted from the user's DCS Control
// external/cheeky/src/vulkan_gpu.cpp and vulkan_nr.cpp. No ReShade/runtime hooks.
// Include after fixture Context and Buffer definitions, inside its anonymous namespace.
#pragma once
#include "dlss5_nr_vulkan_spv.h"

struct VulkanImage {
  Context& c;
  VkImage image{};
  VkDeviceMemory memory{};
  VkImageView view{};
  NVSDK_NGX_Resource_VK ngx{};
  VulkanImage(Context& ctx, uint32_t width, uint32_t height, VkFormat format) : c(ctx) {
    try {
      need(width && height, "private Vulkan image dimensions");
      VkFormatProperties properties{};
      vkGetPhysicalDeviceFormatProperties(c.physical, format, &properties);
      constexpr auto required = VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT | VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT |
        VK_FORMAT_FEATURE_TRANSFER_SRC_BIT | VK_FORMAT_FEATURE_TRANSFER_DST_BIT;
      need((properties.optimalTilingFeatures & required) == required, "private Vulkan codec image format support");
      VkImageCreateInfo ci{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
      ci.imageType = VK_IMAGE_TYPE_2D; ci.format = format; ci.extent = {width, height, 1};
      ci.mipLevels = ci.arrayLayers = 1; ci.samples = VK_SAMPLE_COUNT_1_BIT;
      ci.tiling = VK_IMAGE_TILING_OPTIMAL; ci.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
      ci.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT |
        VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
      vkcheck(vkCreateImage(c.device, &ci, nullptr, &image), "private Vulkan image");
      VkMemoryRequirements requirements{};
      vkGetImageMemoryRequirements(c.device, image, &requirements);
      VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
      ai.allocationSize = requirements.size;
      ai.memoryTypeIndex = c.memory_type(requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
      vkcheck(vkAllocateMemory(c.device, &ai, nullptr, &memory), "private Vulkan image allocation");
      vkcheck(vkBindImageMemory(c.device, image, memory, 0), "private Vulkan image memory bind");
      VkImageViewCreateInfo vi{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
      vi.image = image; vi.viewType = VK_IMAGE_VIEW_TYPE_2D; vi.format = format;
      vi.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
      vkcheck(vkCreateImageView(c.device, &vi, nullptr, &view), "private Vulkan image view");
      ngx.Type = NVSDK_NGX_RESOURCE_VK_TYPE_VK_IMAGEVIEW;
      ngx.ReadWrite = true;
      auto& im = ngx.Resource.ImageViewInfo;
      im.Image = image; im.ImageView = view; im.Format = format;
      im.Width = width; im.Height = height; im.SubresourceRange = vi.subresourceRange;
    } catch (...) { destroy(); throw; }
  }
  VulkanImage(const VulkanImage&) = delete;
  VulkanImage& operator=(const VulkanImage&) = delete;
  void destroy() noexcept {
    if (view) vkDestroyImageView(c.device, view, nullptr);
    if (image) vkDestroyImage(c.device, image, nullptr);
    if (memory) vkFreeMemory(c.device, memory, nullptr);
    view = VK_NULL_HANDLE; image = VK_NULL_HANDLE; memory = VK_NULL_HANDLE; ngx = {};
  }
  ~VulkanImage() { destroy(); }
};

struct VulkanCodec {
  Context& c;
  // Distinct immutable-in-flight encode/decode resources. Caller must finish the
  // preceding frame before record() rewrites the same direction's set/constants.
  Buffer encode_constants, decode_constants;
  VkDescriptorSetLayout descriptors{};
  VkPipelineLayout layout{};
  VkDescriptorPool pool{};
  VkDescriptorSet sets[2]{};
  VkPipeline pipelines[2]{};
  explicit VulkanCodec(Context& ctx) : c(ctx),
    encode_constants(ctx, sizeof(nr::CodecConstants), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT),
    decode_constants(ctx, sizeof(nr::CodecConstants), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT) {
    try {
      std::array<VkDescriptorSetLayoutBinding, 6> bindings{};
      for (uint32_t i = 0; i < bindings.size(); ++i) {
        bindings[i].binding = i; bindings[i].descriptorCount = 1;
        bindings[i].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
        bindings[i].descriptorType = i < 3 ? VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE :
          i < 5 ? VK_DESCRIPTOR_TYPE_STORAGE_IMAGE : VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
      }
      VkDescriptorSetLayoutCreateInfo d{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
      d.bindingCount = uint32_t(bindings.size()); d.pBindings = bindings.data();
      vkcheck(vkCreateDescriptorSetLayout(c.device, &d, nullptr, &descriptors), "Vulkan codec descriptors");
      VkPipelineLayoutCreateInfo l{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
      l.setLayoutCount = 1; l.pSetLayouts = &descriptors;
      vkcheck(vkCreatePipelineLayout(c.device, &l, nullptr, &layout), "Vulkan codec layout");
      const VkDescriptorPoolSize sizes[] = {{VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 6},
        {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 4}, {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 2}};
      VkDescriptorPoolCreateInfo p{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
      p.maxSets = 2; p.poolSizeCount = 3; p.pPoolSizes = sizes;
      vkcheck(vkCreateDescriptorPool(c.device, &p, nullptr, &pool), "Vulkan codec descriptor pool");
      const VkDescriptorSetLayout layouts[] = {descriptors, descriptors};
      VkDescriptorSetAllocateInfo a{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
      a.descriptorPool = pool; a.descriptorSetCount = 2; a.pSetLayouts = layouts;
      vkcheck(vkAllocateDescriptorSets(c.device, &a, sets), "Vulkan codec descriptor sets");
      make_pipeline(nr_vulkan_spv::nr_encode16, sizeof(nr_vulkan_spv::nr_encode16), "EncodeMain", pipelines[0]);
      make_pipeline(nr_vulkan_spv::nr_decode16, sizeof(nr_vulkan_spv::nr_decode16), "DecodeMain", pipelines[1]);
    } catch (...) { destroy(); throw; }
  }
  VulkanCodec(const VulkanCodec&) = delete;
  VulkanCodec& operator=(const VulkanCodec&) = delete;
  void make_pipeline(const uint32_t* code, size_t bytes, const char* entry, VkPipeline& pipeline) {
    VkShaderModuleCreateInfo m{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    m.codeSize = bytes; m.pCode = code;
    VkShaderModule module{};
    vkcheck(vkCreateShaderModule(c.device, &m, nullptr, &module), "DCS Vulkan codec shader module");
    VkComputePipelineCreateInfo p{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
    p.layout = layout; p.stage = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
    p.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT; p.stage.module = module; p.stage.pName = entry;
    const auto result = vkCreateComputePipelines(c.device, VK_NULL_HANDLE, 1, &p, nullptr, &pipeline);
    vkDestroyShaderModule(c.device, module, nullptr);
    vkcheck(result, "DCS Vulkan codec pipeline");
  }
  // Images must already be GENERAL, and writes/reads ordered by the caller.
  // Bindings match DCS VulkanCompute: t0..2 -> 0..2, u0..1 -> 3..4, b0 -> 5.
  void record(VkCommandBuffer cmd, bool encode, const std::array<VkImageView, 3>& inputs,
              const std::array<VkImageView, 2>& outputs, const nr::CodecConstants& constants) {
    const unsigned index = encode ? 0 : 1;
    auto& uniform = encode ? encode_constants : decode_constants;
    std::memcpy(uniform.mapped, &constants, sizeof constants);
    std::array<VkDescriptorImageInfo, 5> images{};
    std::array<VkWriteDescriptorSet, 6> writes{};
    for (uint32_t i = 0; i < images.size(); ++i) {
      images[i].imageView = i < 3 ? inputs[i] : outputs[i - 3];
      images[i].imageLayout = VK_IMAGE_LAYOUT_GENERAL;
      writes[i] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
      writes[i].dstSet = sets[index]; writes[i].dstBinding = i; writes[i].descriptorCount = 1;
      writes[i].descriptorType = i < 3 ? VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE : VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
      writes[i].pImageInfo = &images[i];
    }
    VkDescriptorBufferInfo buffer{uniform.buffer, 0, sizeof constants};
    auto& write = writes[5]; write = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    write.dstSet = sets[index]; write.dstBinding = 5; write.descriptorCount = 1;
    write.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER; write.pBufferInfo = &buffer;
    vkUpdateDescriptorSets(c.device, uint32_t(writes.size()), writes.data(), 0, nullptr);
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelines[index]);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, layout, 0, 1, &sets[index], 0, nullptr);
    const auto width = encode ? (std::max)(constants.size[0], constants.proxy_size[0]) : constants.size[0];
    const auto height = encode ? (std::max)(constants.size[1], constants.proxy_size[1]) : constants.size[1];
    vkCmdDispatch(cmd, (width + 15) / 16, (height + 15) / 16, 1);
  }
  void destroy() noexcept {
    for (auto& pipeline : pipelines) { if (pipeline) vkDestroyPipeline(c.device, pipeline, nullptr); pipeline = VK_NULL_HANDLE; }
    if (pool) vkDestroyDescriptorPool(c.device, pool, nullptr);
    if (layout) vkDestroyPipelineLayout(c.device, layout, nullptr);
    if (descriptors) vkDestroyDescriptorSetLayout(c.device, descriptors, nullptr);
    pool = VK_NULL_HANDLE; layout = VK_NULL_HANDLE; descriptors = VK_NULL_HANDLE;
  }
  ~VulkanCodec() { destroy(); }
};
