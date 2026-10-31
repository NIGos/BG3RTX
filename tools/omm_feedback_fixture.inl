// Optional existing-console-fixture path; no presentation or game.
static void feedback_fixture(DeviceData& dd,VkCommandPool pool,const char* path){
 std::ifstream f(path,std::ios::binary|std::ios::ate);check(bool(f),"feedback shader");auto bytes=f.tellg();
 check(bytes>0 && uint64_t(bytes)%4==0,"feedback shader bytes");std::vector<uint32_t> code(size_t(bytes)/4);f.seekg(0);f.read(reinterpret_cast<char*>(code.data()),bytes);
 VkShaderModuleCreateInfo sm{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};sm.codeSize=code.size()*4;sm.pCode=code.data();VkShaderModule module{};vkcheck(dd.vk.CreateShaderModule(dd.device,&sm,nullptr,&module),"feedback shader module");
 VkDescriptorSetLayoutCreateInfo dc{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};VkDescriptorSetLayout sets[2]{};vkcheck(dd.vk.CreateDescriptorSetLayout(dd.device,&dc,nullptr,&sets[0]),"empty feedback set");
 VkDescriptorSetLayoutBinding bindings[3]{};const uint32_t indexes[]={9,11,12};for(unsigned i=0;i<3;++i)bindings[i]={indexes[i],VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,1,VK_SHADER_STAGE_COMPUTE_BIT,nullptr};
 dc.flags=VK_DESCRIPTOR_SET_LAYOUT_CREATE_PUSH_DESCRIPTOR_BIT_KHR;dc.bindingCount=3;dc.pBindings=bindings;vkcheck(dd.vk.CreateDescriptorSetLayout(dd.device,&dc,nullptr,&sets[1]),"feedback set");
 VkPipelineLayoutCreateInfo lc{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};lc.setLayoutCount=2;lc.pSetLayouts=sets;VkPipelineLayout layout{};vkcheck(dd.vk.CreatePipelineLayout(dd.device,&lc,nullptr,&layout),"feedback layout");
 VkComputePipelineCreateInfo pi{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};pi.layout=layout;pi.stage={VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,nullptr,0,VK_SHADER_STAGE_COMPUTE_BIT,module,"main"};VkPipeline pipeline{};vkcheck(dd.vk.CreateComputePipelines(dd.device,VK_NULL_HANDLE,1,&pi,nullptr,&pipeline),"feedback pipeline");
 Buffer stamp,epochs,result;buffer(dd,stamp,512,VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);buffer(dd,epochs,64,VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);buffer(dd,result,256,VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
 VkCommandBufferAllocateInfo ca{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};ca.commandPool=pool;ca.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY;ca.commandBufferCount=1;VkCommandBuffer cmd{};vkcheck(dd.vk.AllocateCommandBuffers(dd.device,&ca,&cmd),"feedback command");omm::command_allocated(pool,cmd);
 const char* names[]={"current","stale_global","active_write","sticky_disabled","proof_zero","bad_generation","bad_magic","zero_materials"};
 for(unsigned test=0;test<std::size(names);++test){
  auto* s=static_cast<uint32_t*>(stamp.mapped);auto* e=static_cast<uint32_t*>(epochs.mapped);auto* r=static_cast<uint32_t*>(result.mapped);
  std::memset(s,0,512);std::memset(e,0,64);std::memset(r,0xcd,256);s[0]=7;s[32]=7;s[33]=3;s[34]=1;s[35]=0x4f4d4d32u;e[0]=3;
  if(test==1)e[0]=4;if(test==2)e[2]=1;if(test==3)e[1]=1;if(test==4)s[32]=0;if(test==5)s[32]=8;if(test==6)s[35]=0;if(test==7)s[34]=0;
  VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};vkcheck(dd.vk.BeginCommandBuffer(cmd,&bi),"feedback begin");omm::command_begin(cmd);check(omm::mark_feedback_use(cmd),"production feedback command marker");
  const VkDescriptorBufferInfo buffers[]={{stamp.handle,0,512},{epochs.handle,0,64},{result.handle,0,256}};VkWriteDescriptorSet writes[3]{};for(unsigned i=0;i<3;++i){writes[i]={VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};writes[i].dstBinding=indexes[i];writes[i].descriptorCount=1;writes[i].descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;writes[i].pBufferInfo=buffers+i;}
  dd.vk.CmdBindPipeline(cmd,VK_PIPELINE_BIND_POINT_COMPUTE,pipeline);dd.vk.CmdPushDescriptorSet(cmd,VK_PIPELINE_BIND_POINT_COMPUTE,layout,1,3,writes);dd.vk.CmdDispatch(cmd,1,1,1);
  omm::feedback_before_end(cmd,true);vkcheck(dd.vk.EndCommandBuffer(cmd),"feedback end");omm::command_end(cmd,true);
  const VkCommandBuffer commands[]={cmd};const auto pending=omm::before_submit(&dd,commands);VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};submit.commandBufferCount=1;submit.pCommandBuffers=commands;
  const auto sent=dd.vk.QueueSubmit(dd.graphics_queue,1,&submit,VK_NULL_HANDLE);omm::after_submit(&dd,dd.graphics_queue,pending,sent==VK_SUCCESS);vkcheck(sent,"feedback submit");vkcheck(dd.vk.QueueWaitIdle(dd.graphics_queue),"feedback actual completion");
  for(unsigned lane=0;lane<64;++lane)check(r[lane]==(test==0?1u:0u),names[test]);check(s[36]==(test>=1 && test<=3?1u:0u),"feedback only from a previously valid proof");
  std::printf("FEEDBACK case=%s flag=%u lanes=64\n",names[test],s[36]);vkcheck(dd.vk.ResetCommandBuffer(cmd,0),"feedback reset");omm::command_discard(cmd);
 }
 dd.vk.DestroyPipeline(dd.device,pipeline,nullptr);dd.vk.DestroyPipelineLayout(dd.device,layout,nullptr);for(auto set:sets)dd.vk.DestroyDescriptorSetLayout(dd.device,set,nullptr);dd.vk.DestroyShaderModule(dd.device,module,nullptr);
 std::puts("FEEDBACK cases=8 failures=0 production_guard=1 production_end_barrier=1 actual_completion=1");
}
