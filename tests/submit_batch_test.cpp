#include "../common/submit_batch.h"
#include <cstdio>
#include <cstdlib>
#include <array>
using namespace bg3::submit_batch;
static unsigned checks;
#define CHECK(x) do { ++checks; if (!(x)) { std::printf("FAIL %d: %s\n", __LINE__, #x); std::exit(1); } } while (0)
int main() {
  Scratch<std::vector<int>> scratch;
  {
    Scratch<std::vector<int>>::Lease outer(scratch,true);
    outer.get().reserve(64);outer.get().push_back(7);
    { Scratch<std::vector<int>>::Lease nested(scratch,true); CHECK(!nested.borrowed); nested.get().push_back(9); }
    { Scratch<std::vector<int>>::Lease off(scratch,false); CHECK(!off.borrowed);CHECK(off.get().empty()); }
    CHECK(outer.get()==std::vector<int>{7});CHECK(scratch.busy);
  }
  CHECK(!scratch.busy);
  { Scratch<std::vector<int>>::Lease reuse(scratch,true);CHECK(reuse.borrowed);reuse.get().clear();CHECK(reuse.get().capacity()>=64); }
  // Exhaustive final-content comparison: old full copy + overlay versus Vulkan's
  // writes-first order with only the complementary copy intervals.
  for (unsigned begin=0;begin<8;++begin) for(unsigned length=1;length<=8-begin;++length)
    for(unsigned first=0;first<10;++first) for(unsigned count=0;count<=10-first;++count) {
      std::array<int,16> old{},batched{},source{};
      for(unsigned i=0;i<16;++i)source[i]=100+i;
      VkCopyDescriptorSet copy{VK_STRUCTURE_TYPE_COPY_DESCRIPTOR_SET};
      copy.srcArrayElement=2;copy.dstArrayElement=begin;copy.descriptorCount=length;
      for(unsigned i=0;i<length;++i)old[begin+i]=source[2+i];
      for(unsigned i=first;i<first+count;++i)old[i]=batched[i]=-int(i)-1;
      std::vector<VkCopyDescriptorSet> compatible;copy_except(compatible,copy,first,count);
      for(auto c:compatible)for(unsigned i=0;i<c.descriptorCount;++i)batched[c.dstArrayElement+i]=source[c.srcArrayElement+i];
      CHECK(old==batched);
    }
  DescriptorWrites descriptors;
  VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};write.dstBinding=2;
  VkDescriptorImageInfo info{};info.imageLayout=VK_IMAGE_LAYOUT_GENERAL;
  descriptors.add_images(write,&info,1);auto first=descriptors.writes[0].pImageInfo;
  info.imageLayout=VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
  for(unsigned i=0;i<128;++i)descriptors.add_images(write,&info,1);
  CHECK(first->imageLayout==VK_IMAGE_LAYOUT_GENERAL);
  CHECK(descriptors.writes.back().pImageInfo->imageLayout==VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
  descriptors.add_images(write,nullptr,0);CHECK(descriptors.writes.size()==129);
  VkSubmitInfo legacy[2]={{VK_STRUCTURE_TYPE_SUBMIT_INFO},{VK_STRUCTURE_TYPE_SUBMIT_INFO}};
  CHECK(!appendable(legacy,0));CHECK(appendable(legacy,2));
  int extension=1;legacy[1].pNext=&extension;CHECK(!appendable(legacy,2));
  VkSubmitInfo2 modern[2]={{VK_STRUCTURE_TYPE_SUBMIT_INFO_2},{VK_STRUCTURE_TYPE_SUBMIT_INFO_2}};
  CHECK(appendable(modern,2));modern[0].flags=VK_SUBMIT_PROTECTED_BIT;CHECK(!appendable(modern,2));
  modern[0].flags=0;modern[1].pNext=&extension;CHECK(!appendable(modern,2));
  legacy[1].pNext=nullptr;modern[1].pNext=nullptr;
  VkSemaphore wait=reinterpret_cast<VkSemaphore>(uintptr_t(42));
  VkPipelineStageFlags stage=VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
  legacy[0].waitSemaphoreCount=1;legacy[0].pWaitSemaphores=&wait;legacy[0].pWaitDstStageMask=&stage;
  VkSemaphoreSubmitInfo appSignal{VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO};appSignal.value=88;
  modern[1].signalSemaphoreInfoCount=1;modern[1].pSignalSemaphoreInfos=&appSignal;
  PrivateSignal privateSignal(reinterpret_cast<VkSemaphore>(uintptr_t(17)),123);
  std::vector<VkSubmitInfo> oldSubmits;privateSignal.append(oldSubmits,legacy,2);
  std::vector<VkSubmitInfo2> newSubmits;privateSignal.append(newSubmits,modern,2);
  CHECK(oldSubmits.size()==3 && newSubmits.size()==3);
  CHECK(oldSubmits[0].pWaitSemaphores==&wait && oldSubmits[0].pWaitDstStageMask==&stage);
  CHECK(newSubmits[1].pSignalSemaphoreInfos==&appSignal && appSignal.value==88);
  CHECK(oldSubmits.back().commandBufferCount==0 && newSubmits.back().commandBufferInfoCount==0);
  CHECK(oldSubmits.back().pNext==&privateSignal.values);
  CHECK(*privateSignal.values.pSignalSemaphoreValues==123);
  CHECK(newSubmits.back().pSignalSemaphoreInfos->value==123);
  CHECK(newSubmits.back().pSignalSemaphoreInfos->stageMask==VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT);
  std::printf("submit_batch_test: %u checks passed\n",checks);
}
