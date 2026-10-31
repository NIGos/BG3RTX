// Included only by the opt-in synthetic console test, after its Vulkan helpers.
static void bc_fixture(DeviceData& dd,VkCommandPool pool,const char* shaderPath){
  std::ifstream file(shaderPath,std::ios::binary|std::ios::ate);check(bool(file),"BC shader file");
  auto length=file.tellg();check(length>0 && uint64_t(length)%4==0,"BC shader bytes");
  std::vector<uint32_t> code(size_t(length)/4);file.seekg(0);file.read(reinterpret_cast<char*>(code.data()),length);
  VkShaderModuleCreateInfo sci{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};sci.codeSize=code.size()*4;sci.pCode=code.data();VkShaderModule shader{};
  vkcheck(dd.vk.CreateShaderModule(dd.device,&sci,nullptr,&shader),"BC shader module");
  VkDescriptorSetLayoutBinding bindings[]={{1,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,1,VK_SHADER_STAGE_COMPUTE_BIT,nullptr},{3,VK_DESCRIPTOR_TYPE_SAMPLER,1,VK_SHADER_STAGE_COMPUTE_BIT,nullptr},{4,VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,1,VK_SHADER_STAGE_COMPUTE_BIT,nullptr}};
  VkDescriptorSetLayoutCreateInfo dci{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};dci.flags=VK_DESCRIPTOR_SET_LAYOUT_CREATE_PUSH_DESCRIPTOR_BIT_KHR;dci.bindingCount=3;dci.pBindings=bindings;VkDescriptorSetLayout descriptors{};
  vkcheck(dd.vk.CreateDescriptorSetLayout(dd.device,&dci,nullptr,&descriptors),"BC descriptors");
  VkPipelineLayoutCreateInfo lci{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};lci.setLayoutCount=1;lci.pSetLayouts=&descriptors;VkPipelineLayout layout{};vkcheck(dd.vk.CreatePipelineLayout(dd.device,&lci,nullptr,&layout),"BC pipeline layout");
  VkComputePipelineCreateInfo pci{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};pci.stage={VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,nullptr,0,VK_SHADER_STAGE_COMPUTE_BIT,shader,"bc_endpoints",nullptr};pci.layout=layout;VkPipeline pipeline{};vkcheck(dd.vk.CreateComputePipelines(dd.device,VK_NULL_HANDLE,1,&pci,nullptr,&pipeline),"BC pipeline");
  VkSamplerCreateInfo samplerInfo{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};samplerInfo.minFilter=samplerInfo.magFilter=VK_FILTER_LINEAR;samplerInfo.mipmapMode=VK_SAMPLER_MIPMAP_MODE_LINEAR;samplerInfo.addressModeU=samplerInfo.addressModeV=samplerInfo.addressModeW=VK_SAMPLER_ADDRESS_MODE_REPEAT;samplerInfo.maxLod=2;VkSampler sampler{};vkcheck(dd.vk.CreateSampler(dd.device,&samplerInfo,nullptr,&sampler),"BC sampler");
  auto properties=reinterpret_cast<PFN_vkGetPhysicalDeviceFormatProperties>(dd.inst->gipa(dd.inst->instance,"vkGetPhysicalDeviceFormatProperties"));
  const VkFormat formats[]={VK_FORMAT_BC3_UNORM_BLOCK,VK_FORMAT_BC3_SRGB_BLOCK,VK_FORMAT_BC7_UNORM_BLOCK,VK_FORMAT_BC7_SRGB_BLOCK};
  unsigned total=0,failures=0;
  for(auto format:formats)for(unsigned endpoint=0;endpoint<2;++endpoint){
    VkFormatProperties fp{};properties(dd.physical,format,&fp);check((fp.optimalTilingFeatures&VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT)!=0,"BC linear sampling supported");
    Buffer upload,output;buffer(dd,upload,256,VK_BUFFER_USAGE_TRANSFER_SRC_BIT);buffer(dd,output,32768,VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
    uint8_t block[16]{};
    const bool bc7=format==VK_FORMAT_BC7_UNORM_BLOCK || format==VK_FORMAT_BC7_SRGB_BLOCK;
    if(bc7){
      // Mode6 unary prefix bit6, seven-bit A endpoints at49..62, pbits63/64,
      // zero interpolation indices. This construction is VERIFIED by actual
      // Texture.Load and SampleLevel below; the bit layout is not a certificate.
      block[0]=0x40;if(endpoint){block[6]=0xfe;block[7]=0xff;block[8]=1;}
    }else block[0]=block[1]=endpoint?255:0; // BC3 selects equal alpha endpoint0.
    for(unsigned mip=0;mip<3;++mip)std::memcpy(static_cast<uint8_t*>(upload.mapped)+mip*16,block,16);
    VkImageCreateInfo ci{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};ci.imageType=VK_IMAGE_TYPE_2D;ci.format=format;ci.extent={4,4,1};ci.mipLevels=3;ci.arrayLayers=1;ci.samples=VK_SAMPLE_COUNT_1_BIT;ci.tiling=VK_IMAGE_TILING_OPTIMAL;ci.usage=VK_IMAGE_USAGE_SAMPLED_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    VkImage image{};vkcheck(dd.vk.CreateImage(dd.device,&ci,nullptr,&image),"BC image");omm::content_image_created(&dd,image,ci);
    VkMemoryRequirements req{};dd.vk.GetImageMemoryRequirements(dd.device,image,&req);VkPhysicalDeviceMemoryProperties mem{};dd.inst->GetPhysicalDeviceMemoryProperties(dd.physical,&mem);
    uint32_t type=UINT32_MAX;for(uint32_t i=0;i<mem.memoryTypeCount;++i)if((req.memoryTypeBits&(1u<<i)) && (mem.memoryTypes[i].propertyFlags&VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) && !(mem.memoryTypes[i].propertyFlags&VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT)){type=i;break;}check(type!=UINT32_MAX,"BC nonhost memory");
    VkMemoryAllocateInfo allocation{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};allocation.allocationSize=req.size;allocation.memoryTypeIndex=type;VkDeviceMemory memory{};vkcheck(dd.vk.AllocateMemory(dd.device,&allocation,nullptr,&memory),"BC memory");omm::content_memory_allocated(&dd,memory,allocation);vkcheck(dd.vk.BindImageMemory(dd.device,image,memory,0),"BC bind");omm::content_image_bound(&dd,image,memory,true);
    VkImageViewCreateInfo vi{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};vi.image=image;vi.viewType=VK_IMAGE_VIEW_TYPE_2D;vi.format=format;vi.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,3,0,1};VkImageView view{};vkcheck(dd.vk.CreateImageView(dd.device,&vi,nullptr,&view),"BC view");omm::content_view_created(&dd,view,vi);
    omm::ContentSource source{};check(omm::content_source_view(view,source),"BC view source metadata");check(source.requiredSamplingCertificate==2,"BC requires explicit uniform-endpoint sampling certificate");
    VkCommandBufferAllocateInfo ai{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};ai.commandPool=pool;ai.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY;ai.commandBufferCount=1;VkCommandBuffer cmd{};vkcheck(dd.vk.AllocateCommandBuffers(dd.device,&ai,&cmd),"BC command");omm::command_allocated(pool,cmd);VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};vkcheck(dd.vk.BeginCommandBuffer(cmd,&bi),"BC begin");omm::command_begin(cmd);
    VkImageMemoryBarrier transition{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};transition.dstAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;transition.oldLayout=VK_IMAGE_LAYOUT_UNDEFINED;transition.newLayout=VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;transition.srcQueueFamilyIndex=transition.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;transition.image=image;transition.subresourceRange=vi.subresourceRange;dd.vk.CmdPipelineBarrier(cmd,VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,0,nullptr,0,nullptr,1,&transition);
    VkBufferImageCopy copies[3]{};for(uint32_t mip=0;mip<3;++mip){copies[mip].bufferOffset=mip*16;copies[mip].imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,mip,0,1};copies[mip].imageExtent={4u>>mip,4u>>mip,1};}
    const bool began=omm::content_before_image_write(cmd,image);dd.vk.CmdCopyBufferToImage(cmd,upload.handle,image,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,3,copies);omm::content_after_image_write(cmd,image,began);
    transition.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;transition.dstAccessMask=VK_ACCESS_SHADER_READ_BIT;transition.oldLayout=VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;transition.newLayout=VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;dd.vk.CmdPipelineBarrier(cmd,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,0,0,nullptr,0,nullptr,1,&transition);
    VkDescriptorBufferInfo out{output.handle,0,32768};VkDescriptorImageInfo sample{sampler,VK_NULL_HANDLE,VK_IMAGE_LAYOUT_UNDEFINED},texture{VK_NULL_HANDLE,view,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};VkWriteDescriptorSet writes[3]{};
    for(unsigned j=0;j<3;++j){writes[j]={VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};writes[j].dstBinding=bindings[j].binding;writes[j].descriptorType=bindings[j].descriptorType;writes[j].descriptorCount=1;}writes[0].pBufferInfo=&out;writes[1].pImageInfo=&sample;writes[2].pImageInfo=&texture;
    check(omm::mark_content_dispatch(cmd),"BC sampled-read submit scope marker");dd.vk.CmdBindPipeline(cmd,VK_PIPELINE_BIND_POINT_COMPUTE,pipeline);dd.vk.CmdPushDescriptorSet(cmd,VK_PIPELINE_BIND_POINT_COMPUTE,layout,0,3,writes);dd.vk.CmdDispatch(cmd,17,1,1);barrier(dd,cmd,VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,VK_ACCESS_2_SHADER_WRITE_BIT,VK_PIPELINE_STAGE_2_HOST_BIT,VK_ACCESS_2_HOST_READ_BIT);
    vkcheck(dd.vk.EndCommandBuffer(cmd),"BC end");omm::command_end(cmd,true);const VkCommandBuffer commands[]={cmd};const auto pending=omm::before_submit(&dd,commands);VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};submit.commandBufferCount=1;submit.pCommandBuffers=commands;const auto sent=dd.vk.QueueSubmit(dd.graphics_queue,1,&submit,VK_NULL_HANDLE);omm::after_submit(&dd,dd.graphics_queue,pending,sent==VK_SUCCESS);vkcheck(sent,"BC submit");vkcheck(dd.vk.QueueWaitIdle(dd.graphics_queue),"BC idle");
    unsigned bad=0;const auto* values=static_cast<const uint32_t*>(output.mapped);const uint32_t expected=endpoint?0x3f800000u:0;
    for(unsigned i=0;i<1088;++i){const auto* v=values+i*4;bad+=v[0]!=expected || v[1]!=expected || v[2]!=i%17 || v[3]!=i/17;}
    failures+=bad;total+=1088;std::printf("BC_ENDPOINT format=%u alpha=%u samples=1088 load_and_linear_mip_exact_failures=%u certificate_required=%u\n",uint32_t(format),endpoint,bad,source.requiredSamplingCertificate);
    vkcheck(dd.vk.ResetCommandBuffer(cmd,0),"BC reset");omm::command_discard(cmd);dd.vk.FreeCommandBuffers(dd.device,pool,1,&cmd);omm::command_discard(cmd,true);omm::content_view_destroyed(&dd,view);dd.vk.DestroyImageView(dd.device,view,nullptr);omm::content_image_destroyed(&dd,image);dd.vk.DestroyImage(dd.device,image,nullptr);omm::content_memory_freed(&dd,memory);dd.vk.FreeMemory(dd.device,memory,nullptr);
  }
  std::printf("BC_ENDPOINT_COMPLETE samples=%u exact_failures=%u; finite BC3/BC7 alpha fixture, no mixed-value numeric certificate\n",total,failures);check(!failures,"BC decoded and filtered endpoints");
  dd.vk.DestroyPipeline(dd.device,pipeline,nullptr);dd.vk.DestroyPipelineLayout(dd.device,layout,nullptr);dd.vk.DestroyDescriptorSetLayout(dd.device,descriptors,nullptr);dd.vk.DestroyShaderModule(dd.device,shader,nullptr);dd.vk.DestroySampler(dd.device,sampler,nullptr);
}
