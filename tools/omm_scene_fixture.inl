// Existing console probe: exercises the production scene validator on the GPU.
static void scene_fixture(DeviceData& dd,VkCommandPool pool,const char* shaderPath){
  std::ifstream f(shaderPath,std::ios::binary|std::ios::ate);check(bool(f),"scene validator shader");
  auto bytes=f.tellg();check(bytes>0 && uint64_t(bytes)%4==0,"scene shader size");
  std::vector<uint32_t> code(size_t(bytes)/4);f.seekg(0);f.read(reinterpret_cast<char*>(code.data()),bytes);
  VkShaderModuleCreateInfo sm{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};sm.codeSize=code.size()*4;sm.pCode=code.data();VkShaderModule module{};
  vkcheck(dd.vk.CreateShaderModule(dd.device,&sm,nullptr,&module),"scene shader module");
  VkDescriptorSetLayoutBinding bindings[3]{};for(uint32_t i=0;i<3;++i)bindings[i]={i,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,1,VK_SHADER_STAGE_COMPUTE_BIT,nullptr};
  VkDescriptorSetLayoutCreateInfo dc{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};dc.flags=VK_DESCRIPTOR_SET_LAYOUT_CREATE_PUSH_DESCRIPTOR_BIT_KHR;dc.bindingCount=3;dc.pBindings=bindings;VkDescriptorSetLayout descriptors{};
  vkcheck(dd.vk.CreateDescriptorSetLayout(dd.device,&dc,nullptr,&descriptors),"scene descriptors");
  VkPushConstantRange pc{VK_SHADER_STAGE_COMPUTE_BIT,0,16};VkPipelineLayoutCreateInfo lc{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};lc.setLayoutCount=1;lc.pSetLayouts=&descriptors;lc.pushConstantRangeCount=1;lc.pPushConstantRanges=&pc;VkPipelineLayout layout{};
  vkcheck(dd.vk.CreatePipelineLayout(dd.device,&lc,nullptr,&layout),"scene layout");
  VkComputePipelineCreateInfo pi{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};pi.layout=layout;pi.stage={VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,nullptr,0,VK_SHADER_STAGE_COMPUTE_BIT,module,"main"};VkPipeline pipeline{};
  vkcheck(dd.vk.CreateComputePipelines(dd.device,VK_NULL_HANDLE,1,&pi,nullptr,&pipeline),"scene pipeline");
  Buffer rows,epochs,stamp;buffer(dd,rows,64,VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);buffer(dd,epochs,64,VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);buffer(dd,stamp,512,VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
  VkCommandBufferAllocateInfo ca{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};ca.commandPool=pool;ca.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY;ca.commandBufferCount=1;VkCommandBuffer cmd{};
  vkcheck(dd.vk.AllocateCommandBuffers(dd.device,&ca,&cmd),"scene command");omm::command_allocated(pool,cmd);
  {
    std::string path=shaderPath;const auto slash=path.find_last_of("/\\");
    path=(slash==std::string::npos?std::string():path.substr(0,slash+1))+"scene-clone.spv";
    std::ifstream file(path,std::ios::binary|std::ios::ate);check(bool(file),"production scene clone shader");
    const auto length=file.tellg();check(length>0 && uint64_t(length)%4==0,"clone shader size");
    std::vector<uint32_t> words(size_t(length)/4);file.seekg(0);file.read(reinterpret_cast<char*>(words.data()),length);
    sm.codeSize=words.size()*4;sm.pCode=words.data();VkShaderModule cloneModule{};
    vkcheck(dd.vk.CreateShaderModule(dd.device,&sm,nullptr,&cloneModule),"clone module");
    pi.stage.module=cloneModule;VkPipeline clonePipeline{};vkcheck(dd.vk.CreateComputePipelines(dd.device,VK_NULL_HANDLE,1,&pi,nullptr,&clonePipeline),"clone pipeline");
    Buffer original,copy,overrides;buffer(dd,original,128,VK_BUFFER_USAGE_TRANSFER_SRC_BIT);
    buffer(dd,copy,128,VK_BUFFER_USAGE_STORAGE_BUFFER_BIT|VK_BUFFER_USAGE_TRANSFER_DST_BIT);
    buffer(dd,overrides,16,VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
    auto* native=static_cast<VkAccelerationStructureInstanceKHR*>(original.mapped);
    std::memset(native,0,128);for(unsigned i=0;i<2;++i){native[i].transform.matrix[0][0]=native[i].transform.matrix[1][1]=native[i].transform.matrix[2][2]=1;native[i].instanceCustomIndex=17+i;native[i].mask=255-i;native[i].flags=VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR;native[i].accelerationStructureReference=0x1000ull*(i+1);}
    const uint64_t addresses[]={0,0x3000};std::memcpy(overrides.mapped,addresses,sizeof addresses);
    VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};vkcheck(dd.vk.BeginCommandBuffer(cmd,&begin),"clone begin");omm::command_begin(cmd);
    const VkBufferCopy region{0,0,128};dd.vk.CmdCopyBuffer(cmd,original.handle,copy.handle,1,&region);
    barrier(dd,cmd,VK_PIPELINE_STAGE_2_TRANSFER_BIT,VK_ACCESS_2_TRANSFER_WRITE_BIT,VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,VK_ACCESS_2_SHADER_READ_BIT|VK_ACCESS_2_SHADER_WRITE_BIT);
    const VkDescriptorBufferInfo infos[]={{copy.handle,0,128},{overrides.handle,0,16},{stamp.handle,0,512}};VkWriteDescriptorSet writes[3]{};
    for(unsigned i=0;i<3;++i){writes[i]={VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};writes[i].dstBinding=i;writes[i].descriptorCount=1;writes[i].descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;writes[i].pBufferInfo=infos+i;}
    const uint32_t push[]={2,0,0,0};dd.vk.CmdBindPipeline(cmd,VK_PIPELINE_BIND_POINT_COMPUTE,clonePipeline);dd.vk.CmdPushDescriptorSet(cmd,VK_PIPELINE_BIND_POINT_COMPUTE,layout,0,3,writes);dd.vk.CmdPushConstants(cmd,layout,VK_SHADER_STAGE_COMPUTE_BIT,0,16,push);dd.vk.CmdDispatch(cmd,1,1,1);
    barrier(dd,cmd,VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,VK_ACCESS_2_SHADER_WRITE_BIT,VK_PIPELINE_STAGE_2_HOST_BIT,VK_ACCESS_2_HOST_READ_BIT);
    vkcheck(dd.vk.EndCommandBuffer(cmd),"clone end");omm::command_end(cmd,true);
    // Mimic native matrix fixups AFTER recording, BEFORE the actual submission.
    native[0].transform.matrix[0][3]=3.25f;native[1].transform.matrix[1][3]=-2.5f;
    VkAccelerationStructureInstanceKHR expected[2];std::memcpy(expected,native,128);expected[1].accelerationStructureReference=addresses[1];
    const VkCommandBuffer commands[]={cmd};const auto pending=omm::before_submit(&dd,commands);VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};submit.commandBufferCount=1;submit.pCommandBuffers=commands;
    const auto sent=dd.vk.QueueSubmit(dd.graphics_queue,1,&submit,VK_NULL_HANDLE);omm::after_submit(&dd,dd.graphics_queue,pending,sent==VK_SUCCESS);vkcheck(sent,"clone submit");vkcheck(dd.vk.QueueWaitIdle(dd.graphics_queue),"clone actual completion");
    check(std::memcmp(expected,copy.mapped,128)==0,"exact native late matrices/order/masks/IDs and only selected AS replacement");
    vkcheck(dd.vk.ResetCommandBuffer(cmd,0),"clone reset");omm::command_discard(cmd);
    dd.vk.DestroyPipeline(dd.device,clonePipeline,nullptr);dd.vk.DestroyShaderModule(dd.device,cloneModule,nullptr);
    std::puts("SCENE_CLONE instances=2 late_matrix_words=2 AS_replacements=1 preserved_bytes=120 failures=0; production shader, no game");
  }
  const char* names[]={"current","stale_image","unrelated_write","disabled","invalid_bake","source_out_of_range","global_saturated","uninitialized_image","missing_rows","bad_generation","multiple_materials","second_material_stale","write_in_progress","global_advanced_before_image"};
  unsigned passed=0;
  for(unsigned test=0;test<std::size(names);++test){
    auto* r=static_cast<uint32_t*>(rows.mapped);auto* e=static_cast<uint32_t*>(epochs.mapped);auto* s=static_cast<uint32_t*>(stamp.mapped);
    std::memset(r,0,64);std::memset(e,0,64);std::memset(s,0xa5,512);
    r[0]=1;r[1]=3;r[3]=0x4f4d4d31u;r[4]=16;e[0]=3;e[4]=1;s[0]=7;
    uint32_t push[]={1,7,0,0};bool expected=test==0 || test==2 || test==10;
    if(test==1)e[4]=2;if(test==2)e[0]=4;if(test==3)e[1]=1;if(test==4)r[2]=1;
    if(test==5)r[4]=64;if(test==6)e[0]=UINT32_MAX;if(test==7)r[0]=e[4]=0;
    if(test==8)push[0]=0;if(test==9)push[1]=0;
    if(test==10 || test==11){push[0]=2;std::memcpy(r+8,r,32);r[12]=20;e[5]=test==11?2:1;}
    if(test>=12)e[2]=1;if(test==13)e[0]=4;
    VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};vkcheck(dd.vk.BeginCommandBuffer(cmd,&begin),"scene begin");omm::command_begin(cmd);
    VkDescriptorBufferInfo infos[]={{rows.handle,0,64},{epochs.handle,0,64},{stamp.handle,0,512}};VkWriteDescriptorSet writes[3]{};
    for(unsigned j=0;j<3;++j){writes[j]={VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};writes[j].dstBinding=j;writes[j].descriptorCount=1;writes[j].descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;writes[j].pBufferInfo=&infos[j];}
    dd.vk.CmdBindPipeline(cmd,VK_PIPELINE_BIND_POINT_COMPUTE,pipeline);dd.vk.CmdPushDescriptorSet(cmd,VK_PIPELINE_BIND_POINT_COMPUTE,layout,0,3,writes);dd.vk.CmdPushConstants(cmd,layout,VK_SHADER_STAGE_COMPUTE_BIT,0,16,push);dd.vk.CmdDispatch(cmd,1,1,1);
    barrier(dd,cmd,VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,VK_ACCESS_2_SHADER_WRITE_BIT,VK_PIPELINE_STAGE_2_HOST_BIT,VK_ACCESS_2_HOST_READ_BIT);
    vkcheck(dd.vk.EndCommandBuffer(cmd),"scene end");omm::command_end(cmd,true);const VkCommandBuffer commands[]={cmd};auto pending=omm::before_submit(&dd,commands);
    VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};submit.commandBufferCount=1;submit.pCommandBuffers=commands;auto sent=dd.vk.QueueSubmit(dd.graphics_queue,1,&submit,VK_NULL_HANDLE);omm::after_submit(&dd,dd.graphics_queue,pending,sent==VK_SUCCESS);vkcheck(sent,"scene submit");vkcheck(dd.vk.QueueWaitIdle(dd.graphics_queue),"scene actual completion");
    const bool valid=s[32]==7 && s[33]==e[0] && s[34]==push[0] && s[35]==0x4f4d4d32u;
    check(valid==expected,names[test]);if(!expected)check(s[32]==0 && s[33]==0 && s[34]==0 && s[35]==0,"invalid scene clears previous stamp");
    for(unsigned i=1;i<128;++i)if(i<32 || i>=36)check(s[i]==0xa5a5a5a5u,"preserve Hair/Sun/camera stamp regions");
    ++passed;vkcheck(dd.vk.ResetCommandBuffer(cmd,0),"scene reset");omm::command_discard(cmd);
  }
  std::printf("SCENE_VALIDATOR cases=%u failures=0; current/stale/unrelated/disabled/bounds/multiple images; not yet native scene wiring\n",passed);
  dd.vk.DestroyPipeline(dd.device,pipeline,nullptr);dd.vk.DestroyPipelineLayout(dd.device,layout,nullptr);dd.vk.DestroyDescriptorSetLayout(dd.device,descriptors,nullptr);dd.vk.DestroyShaderModule(dd.device,module,nullptr);
}
