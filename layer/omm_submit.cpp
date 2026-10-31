#include "host.h"
#include "omm_backend.h"
#include "omm_submit.h"
#include "../common/omm_submit_state.h"
#include <set>
namespace bg3::omm {
namespace {
std::mutex graphMutex,serialMutex;
CommandGraph graph;
std::map<Id,uint32_t> pools;
std::set<Id> untrackedContent;
DeviceData* owner{};
VkSemaphore timeline{};
PFN_vkGetSemaphoreCounterValue getCounter{};
SubmitSerial serial;
bool poisonLogged{};
std::atomic<bool> active{};
template<class T> Id id(T handle) {return uint64_t(reinterpret_cast<uintptr_t>(handle));}
void discard(const std::set<Id>& tickets) {for(auto t:tickets)discard_unsubmitted(t);}
void poison(const char* why) {
  serial.poisoned=true;retain_until_idle();
  if(!poisonLogged)log("[omm] completion proof lost (%s); retain micromaps until device idle",why);
  poisonLogged=true;
}
}
bool submit_device_created(DeviceData* dd) {
  std::lock_guard lock(serialMutex);
  owner=dd;serial={};poisonLogged=false;
  getCounter=reinterpret_cast<PFN_vkGetSemaphoreCounterValue>(dd->gdpa(dd->device,"vkGetSemaphoreCounterValue"));
  if(!getCounter)getCounter=reinterpret_cast<PFN_vkGetSemaphoreCounterValue>(dd->gdpa(dd->device,"vkGetSemaphoreCounterValueKHR"));
  if(!getCounter)return false;
  VkSemaphoreTypeCreateInfo type{VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO};type.semaphoreType=VK_SEMAPHORE_TYPE_TIMELINE;
  VkSemaphoreCreateInfo ci{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,&type};
  if(dd->vk.CreateSemaphore(dd->device,&ci,nullptr,&timeline)!=VK_SUCCESS)return false;
  active.store(true,std::memory_order_release);return true;
}
void submit_device_destroyed(DeviceData* dd) {
  std::lock_guard lock(serialMutex);if(owner!=dd)return;
  active.store(false,std::memory_order_release);
  // Backend teardown already waited idle when any ticket remained alive.
  if(timeline) {
    if(serial.lastCompleted<serial.lastSignal)dd->vk.DeviceWaitIdle(dd->device);
    dd->vk.DestroySemaphore(dd->device,timeline,nullptr);
  }
  timeline=VK_NULL_HANDLE;owner=nullptr;getCounter=nullptr;
  std::lock_guard graphLock(graphMutex);graph={};pools.clear();untrackedContent.clear();
}
bool tracked_recording(VkCommandBuffer cmd) {
  if(!active.load(std::memory_order_acquire))return false;
  std::lock_guard lock(graphMutex);return owner && graph.tracked_recording(id(cmd),owner->graphics_family);
}
bool command_queue_family(VkCommandBuffer cmd,uint32_t& family) {
  if(!active.load())return false;std::lock_guard lock(graphMutex);auto i=graph.commands.find(id(cmd));
  if(i==graph.commands.end() || !i->second.recording)return false;family=i->second.family;return true;
}
bool mark_content_dispatch(VkCommandBuffer cmd) {if(!active.load())return false;std::lock_guard lock(graphMutex);if(graph.mark_content(id(cmd)))return true;untrackedContent.insert(id(cmd));return false;}
bool content_dispatch_in_submit(std::span<const VkCommandBuffer> commands) {
  if(!active.load())return false;std::lock_guard lock(graphMutex);
  for(auto cmd:commands){
    if(graph.has_content(id(cmd)) || graph.has_feedback(id(cmd)) || untrackedContent.contains(id(cmd)))return true;
    auto i=graph.commands.find(id(cmd));if(i!=graph.commands.end() && !graph.references(i->second).empty())return true;
  }
  return false;
}
bool register_record_ticket(VkCommandBuffer cmd,uint64_t ticket) {
  if(!active.load(std::memory_order_acquire))return false;
  std::lock_guard lock(graphMutex);return owner && graph.add(id(cmd),ticket,owner->graphics_family);
}
bool recorded_reference(uint64_t ticket) {std::lock_guard lock(graphMutex);return graph.referenced(ticket);}
void pool_created(VkCommandPool pool,uint32_t family) {if(active.load()){std::lock_guard lock(graphMutex);pools[id(pool)]=family;}}
void pool_discard(VkCommandPool pool,bool destroyed) {
  if(!active.load())return;std::set<Id> orphan;
  {std::lock_guard lock(graphMutex);std::vector<Id> ids;
   for(const auto& [key,c]:graph.commands)if(c.pool==id(pool))ids.push_back(key);
   for(auto key:ids){auto old=graph.reset(key,destroyed);orphan.insert(old.begin(),old.end());}
   if(destroyed)pools.erase(id(pool));}
  discard(orphan);
}
void command_allocated(VkCommandPool pool,VkCommandBuffer cmd) {
  if(!active.load())return;std::set<Id> orphan;
  {std::lock_guard lock(graphMutex);auto p=pools.find(id(pool));if(p==pools.end())return;orphan=graph.allocate(id(cmd),id(pool),p->second);}
  discard(orphan);
}
void command_discard(VkCommandBuffer cmd,bool freed) {
  if(!active.load())return;std::set<Id> orphan;
  {std::lock_guard lock(graphMutex);orphan=graph.reset(id(cmd),freed);untrackedContent.erase(id(cmd));}discard(orphan);
}
void command_begin(VkCommandBuffer cmd,bool hostBarrierAllowed) {if(active.load()){std::lock_guard lock(graphMutex);graph.begin(id(cmd),hostBarrierAllowed);}}
bool mark_feedback_use(VkCommandBuffer cmd) {
  if(!active.load())return false;std::lock_guard lock(graphMutex);return graph.mark_feedback(id(cmd));
}
void feedback_before_end(VkCommandBuffer cmd,bool outsideRenderPass) {
  if(!active.load(std::memory_order_acquire) || !outsideRenderPass)return;
  bool needed=false;
  {std::lock_guard lock(graphMutex);needed=owner && graph.feedback_before_end(id(cmd),owner->graphics_family,outsideRenderPass);}
  if(!needed)return;
  VkMemoryBarrier2 barrier{VK_STRUCTURE_TYPE_MEMORY_BARRIER_2};
  barrier.srcStageMask=VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;barrier.srcAccessMask=VK_ACCESS_2_SHADER_WRITE_BIT;
  barrier.dstStageMask=VK_PIPELINE_STAGE_2_HOST_BIT;barrier.dstAccessMask=VK_ACCESS_2_HOST_READ_BIT;
  VkDependencyInfo dependency{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};dependency.memoryBarrierCount=1;dependency.pMemoryBarriers=&barrier;
  owner->vk.CmdPipelineBarrier2(cmd,&dependency);
}
void command_end(VkCommandBuffer cmd,bool success) {if(active.load()){std::lock_guard lock(graphMutex);graph.end(id(cmd),success);}}
void command_execute(VkCommandBuffer cmd,std::span<const VkCommandBuffer> children) {
  if(!active.load())return;
  std::lock_guard lock(graphMutex);for(auto child:children){graph.execute(id(cmd),id(child));if(untrackedContent.contains(id(child))){if(!graph.mark_content(id(cmd)))untrackedContent.insert(id(cmd));}}
}
PreparedSubmit before_submit(DeviceData* dd,std::span<const VkCommandBuffer> commands) {
  PreparedSubmit prepared;
  if(!active.load(std::memory_order_acquire) || dd!=owner)return prepared;
  std::set<Id> tickets;bool valid;
  {std::lock_guard lock(graphMutex);std::vector<Id> ids;for(auto cmd:commands)ids.push_back(id(cmd));valid=graph.submitted(ids,tickets);}
  prepared.tickets.assign(tickets.begin(),tickets.end());prepared.valid=valid;
  reserve_submissions(prepared.tickets);return prepared;
}
void after_submit(DeviceData* dd,VkQueue queue,const PreparedSubmit& prepared,bool success) {
  if(!active.load(std::memory_order_acquire) || dd!=owner)return;
  struct Unpin {const PreparedSubmit& p;~Unpin(){release_submissions(p.tickets);}} unpin{prepared};
  if(!success)return;
  const auto& tickets=prepared.tickets;
  // Foreign unrelated queues do not affect OMM. A known referencing submission
  // there lacks the required same-queue completion/ownership proof.
  if(!queue_allowed(queue==dd->graphics_queue,!tickets.empty())) {std::lock_guard lock(serialMutex);poison("referencing foreign queue");return;}
  if(queue!=dd->graphics_queue)return;
  if(tickets.empty() && !has_resources())return; // Fresh default-off baseline.
  std::lock_guard lock(serialMutex);
  if(serial.poisoned)return;
  if(!prepared.valid && !tickets.empty()){poison("stale command generation");return;}
  if(!tickets.empty()) {
    const uint64_t value=serial.candidate();
    if(!value){poison("serial overflow");return;}
    VkTimelineSemaphoreSubmitInfo values{VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO};
    values.signalSemaphoreValueCount=1;values.pSignalSemaphoreValues=&value;
    VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO,&values};submit.signalSemaphoreCount=1;submit.pSignalSemaphores=&timeline;
    const auto result=dd->vk.QueueSubmit(queue,1,&submit,VK_NULL_HANDLE);
    if(!serial.signal(value,result==VK_SUCCESS)){poison("timeline signal submit failed");return;}
    for(auto ticket:tickets)if(!mark_submitted(ticket,value)){poison("ticket use after retirement or absent ticket");return;}
  }
  uint64_t completed{};const auto result=getCounter(dd->device,timeline,&completed);
  if(!serial.completed(completed,result==VK_SUCCESS)){poison("timeline counter invalid/failed");return;}
  collect(completed);
}
}
