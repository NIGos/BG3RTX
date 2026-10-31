#pragma once
#include <vector>
#include <vulkan/vulkan_core.h>
namespace bg3::native_hit {
// Own only changed submit/sem-info arrays until the driver call returns. Other
// fields (including pNext, values, device indices and command arrays) stay exact.
struct SubmitStages {
 std::vector<VkSubmitInfo2> submits;
 std::vector<std::vector<VkSemaphoreSubmitInfo>> waits,signals;
 void promote(const VkSubmitInfo2* source,uint32_t count,uint32_t index,bool read_before_wait,bool finish_before_signal){
  const auto& s=source[index];
  const bool w=read_before_wait && s.waitSemaphoreInfoCount;
  const bool r=finish_before_signal && s.signalSemaphoreInfoCount;
  if(!w && !r)return;
  if(submits.empty()){submits.assign(source,source+count);waits.resize(count);signals.resize(count);}
  if(w){waits[index].assign(s.pWaitSemaphoreInfos,s.pWaitSemaphoreInfos+s.waitSemaphoreInfoCount);
   for(auto& sem:waits[index])sem.stageMask=VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
   submits[index].pWaitSemaphoreInfos=waits[index].data();}
  if(r){signals[index].assign(s.pSignalSemaphoreInfos,s.pSignalSemaphoreInfos+s.signalSemaphoreInfoCount);
   for(auto& sem:signals[index])sem.stageMask=VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
   submits[index].pSignalSemaphoreInfos=signals[index].data();}
 }
 const VkSubmitInfo2* data_or(const VkSubmitInfo2* original)const{return submits.empty()?original:submits.data();}
};
}
