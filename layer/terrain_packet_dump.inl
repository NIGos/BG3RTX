// Included only by dump.cpp. Exact-family opt-in post-pass readback, not a
// generic capture system or a proof that unobserved sampler/derivative inputs exist.
#include "terrain_packet_contract.h"
namespace bg3 {
namespace {
struct TerrainCopy {
 Bg3Binding source{};ViewRec view{};ImageRec image{};
 VkBuffer buffer{};VkDeviceMemory memory{};void* mapped{};VkDeviceSize bytes{};
 std::string slot,metadata;
};
struct TerrainPacket {
 uint64_t recording{},frame=UINT64_MAX;VkCommandBuffer cmd{};uint32_t ordinal{},draw{},id{};
 std::array<TerrainCopy,4> copies{};std::string reason,metadata;bool recorded{};
};
std::mutex terrain_mu;std::vector<TerrainPacket> terrain_packets;
std::string tn(uint64_t v){return std::to_string(v);}
std::string tf(float v){char b[32];snprintf(b,sizeof b,"%.9g",double(v));return b;}
std::string terrain_sampler(VkCommandBuffer cmd,uint32_t set,uint32_t binding){
 omm::BoundSampler s;const bool known=omm::bound_sampler(cmd,VK_PIPELINE_BIND_POINT_GRAPHICS,set,binding,0,s);
 const auto& c=s.sampler.info;
 std::string j="{\"set\":"+tn(set)+",\"binding\":"+tn(binding)+",\"known\":"+(known?"true":"false")+
  ",\"status\":"+tn(uint32_t(s.status))+",\"sampler_id\":"+tn(s.sampler.id)+",\"descriptor_set_id\":"+tn(s.descriptorSetId)+",\"write_version\":"+tn(s.writeVersion);
 if(known&&s.sampler.complete)j+=",\"mag\":"+tn(c.magFilter)+",\"min\":"+tn(c.minFilter)+",\"mipmap\":"+tn(c.mipmapMode)+
  ",\"address\":["+tn(c.addressModeU)+","+tn(c.addressModeV)+","+tn(c.addressModeW)+"],\"bias\":"+tf(c.mipLodBias)+
  ",\"min_lod\":"+tf(c.minLod)+",\"max_lod\":"+tf(c.maxLod)+",\"anisotropy\":"+tn(c.anisotropyEnable)+
  ",\"max_anisotropy\":"+tf(c.maxAnisotropy)+",\"compare\":"+tn(c.compareEnable)+",\"compare_op\":"+tn(c.compareOp)+
  ",\"border\":"+tn(c.borderColor)+",\"unnormalized\":"+tn(c.unnormalizedCoordinates)+",\"reduction\":"+tn(s.sampler.reduction);
 return j+"}";
}
bool terrain_binding(VkCommandBuffer cmd,CmdState* s,uint32_t set,uint32_t binding,TerrainCopy& out){
 if(!omm::ordinary_frozen_binding(cmd,VK_PIPELINE_BIND_POINT_GRAPHICS,set,binding,0) ||
    !query_binding(cmd,VK_PIPELINE_BIND_POINT_GRAPHICS,set,binding,0,&out.source))return false;
 std::shared_lock lock(tracker().mu);
 auto ds=tracker().sets.find(s->sets[0][set]);if(ds==tracker().sets.end())return false;
 auto it=ds->second.find(desc_key(binding,0));if(it==ds->second.end())return false;
 const auto& e=it->second;
 out.metadata="\"descriptor_offset\":"+tn(e.offset)+",\"descriptor_range\":"+tn(e.range)+",\"effective_offset\":"+tn(out.source.offset)+",\"validated_range\":"+tn(out.source.range);
 if(out.source.image){
  auto v=tracker().views.find(out.source.view);auto i=tracker().images.find(out.source.image);
  if(v==tracker().views.end()||i==tracker().images.end())return false;
  out.view=v->second;out.image=i->second;
  out.metadata= "\"view\":"+trace_view::json(v->second.format,v->second.range,v->second.creation,i->second.id,&i->second.ci,e.type,e.layout)+",\"width\":"+tn(i->second.ci.extent.width)+",\"height\":"+tn(i->second.ci.extent.height)+",\"row_pitch\":"+tn(uint64_t(i->second.ci.extent.width)*(i->second.ci.format==VK_FORMAT_R16_UNORM?2:4));
 }
 return true;
}
void terrain_release(TerrainCopy& c,bool deferred){
 auto* dd=main_device();if(c.mapped)dd->vk.UnmapMemory(dd->device,c.memory);c.mapped=nullptr;
 if(deferred){if(c.buffer)defer_destroy(BG3_DEFER_BUFFER,uint64_t(c.buffer));if(c.memory)defer_destroy(BG3_DEFER_MEMORY,uint64_t(c.memory));}
 else{if(c.buffer)dd->vk.DestroyBuffer(dd->device,c.buffer,nullptr);if(c.memory)dd->vk.FreeMemory(dd->device,c.memory,nullptr);}
 c.buffer=VK_NULL_HANDLE;c.memory=VK_NULL_HANDLE;
}
}
std::string terrain_capture_note(VkCommandBuffer cmd,CmdState* s){
 if(!s||!s->in_pass||!trace_recording())return {};
 std::lock_guard lock(terrain_mu);
 const bool family=s->vs==0x64e2568916ede3d6ull&&s->fs==0x6650c75f3d3e4968ull;
 // Later draws cannot silently mutate an earlier packet through an ordinary
 // storage descriptor alias. Known target shaders use these inputs read-only.
 if(!family)for(auto& p:terrain_packets)if(p.cmd==cmd&&p.recording==s->recording&&p.ordinal==s->pass.ordinal&&p.reason.empty()){
  std::shared_lock trackerLock(tracker().mu);
  for(const auto& e:s->push[0])if(e.type==VK_DESCRIPTOR_TYPE_STORAGE_BUFFER||e.type==VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC||e.type==VK_DESCRIPTOR_TYPE_STORAGE_IMAGE)p.reason="later_push_storage_cannot_prove_no_alias";
  for(auto set:s->sets[0]){auto it=tracker().sets.find(set);if(it==tracker().sets.end())continue;
   for(const auto& item:it->second){const auto& e=item.second;
    if(e.type==VK_DESCRIPTOR_TYPE_STORAGE_BUFFER||e.type==VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC||e.type==VK_DESCRIPTOR_TYPE_STORAGE_IMAGE)
     for(const auto& c:p.copies){
      VkDeviceSize effective=e.offset;
      if(is_dynamic_type(e.type)){
       const auto index=set_dynamic_index_locked(set,e.binding,e.element);
       size_t setIndex=0;for(;setIndex<s->sets[0].size()&&s->sets[0][setIndex]!=set;++setIndex){}
       if(setIndex<8&&index<s->dyn[0][setIndex].size())effective+=s->dyn[0][setIndex][index];
       else if(e.buffer==c.source.buffer)p.reason="unknown_later_dynamic_alias";
      }
      if(e.buffer&&e.buffer==c.source.buffer&&terrain_packet::overlap(effective,e.range,c.source.offset,c.bytes))p.reason="possible_later_storage_alias";
      if(e.view&&c.source.image){auto view=tracker().views.find(e.view);if(view!=tracker().views.end()&&view->second.image==c.source.image)p.reason="possible_later_storage_image_alias";}
     }
   }
  }
 }
 if(!family || terrain_packets.size()>=8 || !file_mtime(root_dir()+"\\trace_terrain_packet.request"))return {};
 TerrainPacket p;p.cmd=cmd;p.recording=s->recording;p.ordinal=s->pass.ordinal;p.draw=s->pass.draw_count;p.id=uint32_t(terrain_packets.size()+1);
 const uint32_t sets[]={4,1,1,1},bindings[]={0,4,2,3};const char* slots[]={"s4b0","s1b4","s1b2","s1b3"};
 for(size_t k=0;k<4;++k){auto& c=p.copies[k];c.slot=slots[k];
  if(!terrain_binding(cmd,s,sets[k],bindings[k],c)){p.reason="missing_or_mutable_binding";break;}
  if(k<2){
   const bool typeOk=k==0?(c.source.type==VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER||c.source.type==VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC):(c.source.type==VK_DESCRIPTOR_TYPE_STORAGE_BUFFER||c.source.type==VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC);
   if(!typeOk){p.reason="unsupported_buffer_descriptor_type";break;}
   const auto usage=geo_buffer_usage(c.source.buffer);c.metadata+=",\"buffer_usage\":"+tn(usage);
   if(!(usage&VK_BUFFER_USAGE_TRANSFER_SRC_BIT)){p.reason="buffer_missing_transfer_src_"+c.slot+"_usage"+tn(usage);break;}
   c.bytes=terrain_packet::buffer_prefix(geo_buffer_size(c.source.buffer),c.source.offset,c.source.range,k?448:80,k?7168:80,k?112:80);
   if(!c.bytes){p.reason="unsupported_buffer_range";break;}
  }else{
   const auto& ci=c.image.ci;const auto& r=c.view.range;
   const uint32_t fmt=k==2?37:70,w=k==2?49:256,h=k==2?84:4;
   if(uint32_t(ci.format)!=fmt||uint32_t(c.view.format)!=fmt||ci.extent.width!=w||ci.extent.height!=h||ci.extent.depth!=1||ci.mipLevels!=1||ci.arrayLayers!=1||
      ci.imageType!=VK_IMAGE_TYPE_2D||ci.samples!=VK_SAMPLE_COUNT_1_BIT||!(ci.usage&VK_IMAGE_USAGE_TRANSFER_SRC_BIT)||
      ci.flags!=0||r.aspectMask!=VK_IMAGE_ASPECT_COLOR_BIT||r.baseMipLevel||r.baseArrayLayer||
      (r.levelCount!=1&&r.levelCount!=VK_REMAINING_MIP_LEVELS)||(r.layerCount!=1&&r.layerCount!=VK_REMAINING_ARRAY_LAYERS)||
      c.source.layout!=VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL){p.reason="unsupported_exact_image_profile";break;}
   for(uint32_t a=0;a<s->pass.color_count;++a)if(c.source.image==s->pass.color_images[a])p.reason="input_aliases_attachment";
   if(c.source.image==s->pass.depth_image)p.reason="input_aliases_attachment";
   c.bytes=uint64_t(w)*h*(k==2?4:2);
  }
 }
 p.metadata="\"sampler_scope\":\"metadata_not_sampling_certificate\",\"samplers\":["+terrain_sampler(cmd,4,4)+","+terrain_sampler(cmd,1,1)+","+terrain_sampler(cmd,0,7)+"]";
 {
  std::shared_lock trackerLock(tracker().mu);
  const auto pipe=tracker().pipelines.find(s->pipe[0]);
  if(pipe==tracker().pipelines.end())p.reason="unknown_pipeline";
  else{const auto pass=tracker().render_passes.find(pipe->second.render_pass);
   if(pass==tracker().render_passes.end()||pass->second.subpasses.size()!=1||pipe->second.subpass!=0)p.reason="unsupported_render_pass_profile";
  }
 }
 if(!(s->begin_flags&VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT)||(s->begin_flags&VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT))p.reason="requires_one_time_non_simultaneous_recording";
 if(s->begin_flags&VK_COMMAND_BUFFER_USAGE_RENDER_PASS_CONTINUE_BIT)p.reason="secondary_target_unsupported";
 if(!p.reason.empty())log("[terrain_packet] candidate %u declined: %s",p.id,p.reason.c_str());
 const auto id=p.id;terrain_packets.push_back(std::move(p));return tn(id);
}
void terrain_capture_discard(VkCommandBuffer cmd){
 // Existing native successful reset/free hooks prove the old recording can no
 // longer execute. Submitted snapshots stay alive until the tracer saves them.
 std::lock_guard lock(terrain_mu);
 for(auto it=terrain_packets.begin();it!=terrain_packets.end();){
  if(it->cmd==cmd&&it->frame==UINT64_MAX){if(it->recorded)log("[terrain_packet] discarded unsubmitted recording %llu",(unsigned long long)it->recording);for(auto& c:it->copies)terrain_release(c,true);it=terrain_packets.erase(it);}
  else ++it;
 }
}
void terrain_capture_secondary(VkCommandBuffer cmd,CmdState* s){
 if(!s)return;std::lock_guard lock(terrain_mu);
 for(auto& p:terrain_packets)if(p.cmd==cmd&&p.recording==s->recording&&p.ordinal==s->pass.ordinal&&!p.recorded&&p.reason.empty())p.reason="secondary_execution_after_target";
}
void terrain_capture_pass(VkCommandBuffer cmd,CmdState* s){
 std::lock_guard lock(terrain_mu);auto* dd=device_of(cmd);
 for(auto& p:terrain_packets)if(p.cmd==cmd&&p.recording==s->recording&&p.ordinal==s->pass.ordinal&&!p.recorded&&p.reason.empty()){
  bool allocated=true;for(auto& c:p.copies)if(!alloc_host_buffer(c.bytes,VK_BUFFER_USAGE_TRANSFER_DST_BIT,&c.buffer,&c.memory,&c.mapped)){allocated=false;break;}
  if(!allocated){p.reason="staging_allocation_failed";for(auto& c:p.copies)terrain_release(c,false);continue;}
  // Legal only after the native render pass. No native command can intervene
  // between its end and these copies. Source identities/ranges were frozen.
  VkMemoryBarrier mb{VK_STRUCTURE_TYPE_MEMORY_BARRIER};mb.srcAccessMask=VK_ACCESS_MEMORY_WRITE_BIT;mb.dstAccessMask=VK_ACCESS_TRANSFER_READ_BIT;
  dd->vk.CmdPipelineBarrier(cmd,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,1,&mb,0,nullptr,0,nullptr);
  for(auto& c:p.copies){if(c.source.buffer){VkBufferCopy copy{c.source.offset,0,c.bytes};dd->vk.CmdCopyBuffer(cmd,c.source.buffer,c.buffer,1,&copy);}
   else{
    VkImageMemoryBarrier b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};b.srcAccessMask=VK_ACCESS_MEMORY_READ_BIT|VK_ACCESS_MEMORY_WRITE_BIT;b.dstAccessMask=VK_ACCESS_TRANSFER_READ_BIT;
    b.oldLayout=c.source.layout;b.newLayout=VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;b.srcQueueFamilyIndex=b.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;b.image=c.source.image;b.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};
    dd->vk.CmdPipelineBarrier(cmd,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,0,nullptr,0,nullptr,1,&b);
    VkBufferImageCopy copy{};copy.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1};copy.imageExtent=c.image.ci.extent;
    dd->vk.CmdCopyImageToBuffer(cmd,c.source.image,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,c.buffer,1,&copy);
    b.srcAccessMask=VK_ACCESS_TRANSFER_READ_BIT;b.dstAccessMask=VK_ACCESS_MEMORY_READ_BIT|VK_ACCESS_MEMORY_WRITE_BIT;b.oldLayout=VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;b.newLayout=c.source.layout;
    dd->vk.CmdPipelineBarrier(cmd,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,0,0,nullptr,0,nullptr,1,&b);
   }
  }
  mb.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;mb.dstAccessMask=VK_ACCESS_HOST_READ_BIT;
  dd->vk.CmdPipelineBarrier(cmd,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_HOST_BIT,0,1,&mb,0,nullptr,0,nullptr);p.recorded=true;
 }
}
void terrain_capture_submit(const std::vector<VkCommandBuffer>& cmds,uint64_t frame){
 std::lock_guard lock(terrain_mu);for(auto cmd:cmds)for(auto& p:terrain_packets)if(p.cmd==cmd&&p.recording==cmd_recording(cmd)&&p.frame==UINT64_MAX)p.frame=frame;
}
void terrain_capture_finish(uint64_t frame,const std::string& dir,VkQueue queue){
 std::vector<TerrainPacket> packets;{std::lock_guard lock(terrain_mu);packets.swap(terrain_packets);}
 if(packets.empty()){
  if(file_mtime(root_dir()+"\\trace_terrain_packet.request")){
   const std::string missing="{\"status\":\"no_exact_family_candidate\"}\n";write_file(dir+"\\terrain_packet_missing.json",missing.data(),missing.size());
   log("[terrain_packet] frame %llu: no exact-family candidate",(unsigned long long)frame);
  }return;
 }
 auto* dd=main_device();bool idle=dd->vk.QueueWaitIdle(queue)==VK_SUCCESS;
 if(dd->graphics_queue&&dd->graphics_queue!=queue)idle=(dd->vk.QueueWaitIdle(dd->graphics_queue)==VK_SUCCESS)&&idle;
 if(!idle)for(auto& p:packets){p.reason="queue_idle_failed";p.recorded=false;}
 std::string folder=dir+"\\terrain_packet";make_dirs(folder);std::string json="{\"schema\":1,\"frame\":"+tn(frame)+",\"packets\":[";bool first=true;uint32_t copied=0,declined=0;
 for(auto& p:packets){if(p.frame==frame){
  if(!p.recorded&&p.reason.empty())p.reason="pass_not_completed";
  if(p.recorded&&p.copies[0].mapped){const auto* data=static_cast<const uint32_t*>(p.copies[0].mapped);for(unsigned offset:{16u,17u})if(data[offset]<64 && uint64_t(data[offset]+1)*112>p.copies[1].bytes)p.reason="base_brush_outside_copied_range";}
  if(p.recorded&&p.reason.empty())++copied;else ++declined;
  json+=(first?"":",")+std::string("{\"id\":")+tn(p.id)+",\"recording\":"+tn(p.recording)+",\"pass\":"+tn(p.ordinal)+",\"draw\":"+tn(p.draw)+",\"status\":\""+(p.reason.empty()?"copied":p.reason)+"\",\"capture_point\":\"immediately_after_native_pass\",\"vs\":\"64e2568916ede3d6\",\"fs\":\"6650c75f3d3e4968\","+p.metadata+",\"resources\":[";first=false;
  for(size_t k=0;k<p.copies.size();++k){auto& c=p.copies[k];const auto name=tn(p.id)+"_"+c.slot+".bin";
   if(p.recorded&&c.mapped)write_file(folder+"\\"+name,c.mapped,size_t(c.bytes));
   json+=(k?",":"")+std::string("{\"slot\":\"")+c.slot+"\",\"file\":"+(p.recorded?"\""+name+"\"":"null")+",\"bytes\":"+tn(c.bytes)+(c.metadata.empty()?"":","+c.metadata)+"}";
  }json+="]}";
 }
  if(p.recorded&&p.frame==UINT64_MAX&&cmd_recording(p.cmd)==p.recording){
   // Still executable but not yet submitted: frame-count retirement is unsafe.
   std::lock_guard lock(terrain_mu);terrain_packets.push_back(std::move(p));
  }else for(auto& c:p.copies)terrain_release(c,true);
 }
 json+="]}\n";write_file(folder+"\\manifest.json",json.data(),json.size());log("[terrain_packet] frame %llu: copied %u, declined %u -> %s",(unsigned long long)frame,copied,declined,folder.c_str());
}
} // namespace bg3
