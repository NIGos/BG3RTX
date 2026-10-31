// Pure CPU Vulkan structure ownership/stage checks. No Vulkan calls or device.
#include "../common/material_submit_stages.h"
#include <cstdio>
#include <cstdlib>
using bg3::native_hit::SubmitStages;
static unsigned checks;
#define CHECK(x) do{++checks;if(!(x)){std::printf("FAIL line %d: %s\n",__LINE__,#x);std::exit(1);}}while(0)
int main(){
 int extension=7;
 VkSemaphoreSubmitInfo wait{VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO};wait.pNext=&extension;wait.value=31;wait.deviceIndex=3;wait.stageMask=VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
 VkSemaphoreSubmitInfo signal=wait;signal.value=93;signal.deviceIndex=2;signal.stageMask=VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT;
 VkCommandBufferSubmitInfo cmd{VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO};cmd.deviceMask=5;
 VkSubmitInfo2 source[3]{};
 for(auto& s:source){s.sType=VK_STRUCTURE_TYPE_SUBMIT_INFO_2;s.pNext=&extension;s.flags=VK_SUBMIT_PROTECTED_BIT;s.waitSemaphoreInfoCount=1;s.pWaitSemaphoreInfos=&wait;s.signalSemaphoreInfoCount=1;s.pSignalSemaphoreInfos=&signal;s.commandBufferInfoCount=1;s.pCommandBufferInfos=&cmd;}
 SubmitStages off;off.promote(source,3,0,false,false);CHECK(off.data_or(source)==source);CHECK(off.submits.empty());
 SubmitStages changes;
 changes.promote(source,3,1,false,true); // Existing OMM-only policy remains signal-only.
 auto first=changes.data_or(source);CHECK(first!=source);CHECK(first[0].pWaitSemaphoreInfos==&wait);CHECK(first[0].pSignalSemaphoreInfos==&signal);
 CHECK(first[1].pWaitSemaphoreInfos==&wait);CHECK(first[1].pSignalSemaphoreInfos!=&signal);CHECK(first[1].pSignalSemaphoreInfos[0].stageMask==VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT);
 changes.promote(source,3,2,true,true);auto result=changes.data_or(source);
 CHECK(result[2].pWaitSemaphoreInfos!=&wait);CHECK(result[2].pWaitSemaphoreInfos[0].stageMask==VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT);
 CHECK(result[2].pSignalSemaphoreInfos!=&signal);CHECK(result[2].pSignalSemaphoreInfos[0].stageMask==VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT);
 CHECK(result[1].pSignalSemaphoreInfos[0].stageMask==VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT); // Later entry cannot invalidate earlier ownership.
 for(unsigned i=0;i<3;++i){CHECK(result[i].sType==source[i].sType);CHECK(result[i].pNext==source[i].pNext);CHECK(result[i].flags==source[i].flags);CHECK(result[i].pCommandBufferInfos==&cmd);CHECK(result[i].commandBufferInfoCount==1);
  CHECK(result[i].waitSemaphoreInfoCount==1);CHECK(result[i].signalSemaphoreInfoCount==1);
  CHECK(result[i].pWaitSemaphoreInfos[0].value==31);CHECK(result[i].pWaitSemaphoreInfos[0].deviceIndex==3);CHECK(result[i].pWaitSemaphoreInfos[0].pNext==&extension);
  CHECK(result[i].pSignalSemaphoreInfos[0].value==93);CHECK(result[i].pSignalSemaphoreInfos[0].deviceIndex==2);CHECK(result[i].pSignalSemaphoreInfos[0].pNext==&extension);}
 CHECK(wait.stageMask==VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT);CHECK(signal.stageMask==VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT);
 VkSubmitInfo2 one=source[0];one.signalSemaphoreInfoCount=0;one.pSignalSemaphoreInfos=nullptr;
 SubmitStages waitsOnly;waitsOnly.promote(&one,1,0,true,true);CHECK(waitsOnly.data_or(&one)!=&one);CHECK(waitsOnly.submits[0].pWaitSemaphoreInfos[0].stageMask==VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT);CHECK(waitsOnly.submits[0].pSignalSemaphoreInfos==nullptr);
 one.waitSemaphoreInfoCount=0;one.pWaitSemaphoreInfos=nullptr;SubmitStages empty;empty.promote(&one,1,0,true,true);CHECK(empty.data_or(&one)==&one);
 std::printf("%u material submit-stage CPU checks passed\n",checks);
}
