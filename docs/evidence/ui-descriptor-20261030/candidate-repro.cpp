
#include <cstdint>
#include <iostream>
#include <vector>
#include <unordered_map>
#include <shared_mutex>
#include <mutex>
#define VKAPI_ATTR
#define VKAPI_CALL
using VkDevice=uint64_t;using VkDescriptorSet=uint64_t;using VkDescriptorSetLayout=uint64_t;
using VkDescriptorPool=uint64_t;using VkImageView=uint64_t;using VkBuffer=uint64_t;
using VkDeviceSize=uint64_t;using VkResult=int;using VkImageLayout=int;using VkDescriptorType=int;
constexpr int VK_SUCCESS=0,VK_NULL_HANDLE=0,VK_IMAGE_LAYOUT_UNDEFINED=0;
constexpr int VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER=1,VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE=2,
VK_DESCRIPTOR_TYPE_STORAGE_IMAGE=3,VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT=10,
VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER=6,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER=7,
VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC=8,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC=9;
struct VkDescriptorImageInfo{uint64_t sampler;VkImageView imageView;VkImageLayout imageLayout;};
struct VkDescriptorBufferInfo{VkBuffer buffer;VkDeviceSize offset,range;};
struct VkWriteDescriptorSet{VkDescriptorSet dstSet;uint32_t dstBinding,dstArrayElement,descriptorCount;VkDescriptorType descriptorType;const VkDescriptorImageInfo* pImageInfo;const VkDescriptorBufferInfo* pBufferInfo;};
struct VkDescriptorSetAllocateInfo{VkDescriptorPool descriptorPool;uint32_t descriptorSetCount;const VkDescriptorSetLayout* pSetLayouts;};
struct DescEntry{uint32_t binding,element;VkDescriptorType type;VkImageView view;VkBuffer buffer;VkDeviceSize offset,range;VkImageLayout layout;};
uint64_t desc_key(uint32_t binding,uint32_t element){return (uint64_t(binding)<<32)|element;}
bool is_dynamic_type(VkDescriptorType t){return t==8||t==9;}
struct Tracker{std::shared_mutex mu;std::unordered_map<VkDescriptorSet,std::unordered_map<uint64_t,DescEntry>> sets;std::unordered_map<VkDescriptorSet,uint32_t> dynamic_counts;std::unordered_map<VkDescriptorSet,VkDescriptorSetLayout> set_layouts;};
Tracker state;Tracker& tracker(){return state;}
VkResult allocation_result=0;
VkResult driver_allocate(VkDevice,const VkDescriptorSetAllocateInfo*,VkDescriptorSet*){return allocation_result;}
struct Device{struct Functions{decltype(&driver_allocate) AllocateDescriptorSets=driver_allocate;}vk;} device;
Device* device_of(VkDevice){return &device;}
static bool is_image_type(VkDescriptorType t) {
  return t == VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER || t == VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE ||
         t == VK_DESCRIPTOR_TYPE_STORAGE_IMAGE || t == VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT;
}

static bool is_buffer_type(VkDescriptorType t) {
  return t == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER || t == VK_DESCRIPTOR_TYPE_STORAGE_BUFFER ||
         t == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC || t == VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC;
}

static void decode_write(const VkWriteDescriptorSet& w, std::vector<DescEntry>& out) {
  for (uint32_t i = 0; i < w.descriptorCount; ++i) {
    DescEntry e{w.dstBinding, w.dstArrayElement + i, w.descriptorType, VK_NULL_HANDLE, VK_NULL_HANDLE, 0, 0,
                VK_IMAGE_LAYOUT_UNDEFINED};
    if (is_image_type(w.descriptorType) && w.pImageInfo) {
      e.view = w.pImageInfo[i].imageView;
      e.layout = w.pImageInfo[i].imageLayout;
    }
    else if (is_buffer_type(w.descriptorType) && w.pBufferInfo) {
      e.buffer = w.pBufferInfo[i].buffer;
      e.offset = w.pBufferInfo[i].offset;
      e.range = w.pBufferInfo[i].range;
    } else continue;
    out.push_back(e);
  }
}

static void merge(VkDescriptorSet set, const std::vector<DescEntry>& src) {
  auto& dst = tracker().sets[set];
  for (const auto& e : src) {
    auto [it, inserted] = dst.insert_or_assign(desc_key(e.binding, e.element), e);
    if (inserted && is_dynamic_type(e.type)) ++tracker().dynamic_counts[set];
  }
}

void track_set_write(const VkWriteDescriptorSet& w) {
  std::vector<DescEntry> entries;
  decode_write(w, entries);
  if (entries.empty()) return;
  std::unique_lock<std::shared_mutex> lock(tracker().mu);
  merge(w.dstSet, entries);
}

static VKAPI_ATTR VkResult VKAPI_CALL AllocateDescriptorSets(VkDevice device, const VkDescriptorSetAllocateInfo* ai,
                                                             VkDescriptorSet* out) {
  VkResult r = device_of(device)->vk.AllocateDescriptorSets(device, ai, out);
  if (r == VK_SUCCESS) {
    std::unique_lock<std::shared_mutex> lock(tracker().mu);
    for (uint32_t i = 0; i < ai->descriptorSetCount; ++i) { tracker().sets.erase(out[i]); tracker().dynamic_counts.erase(out[i]); tracker().set_layouts[out[i]] = ai->pSetLayouts[i]; }
  }
  return r;
}
int failures=0,cases=0;
void check(bool value,const char* name){++cases;if(!value){++failures;std::cout<<"FAIL "<<name<<"\n";}}
void fresh(){state.sets.clear();state.dynamic_counts.clear();state.set_layouts.clear();allocation_result=0;}
void seed(VkDescriptorSet h){
 VkDescriptorBufferInfo buf{900,0,144};VkDescriptorImageInfo atlas{0,700,5};
 VkWriteDescriptorSet a{h,28,0,1,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,nullptr,&buf};track_set_write(a);
 VkWriteDescriptorSet b{h,42,0,1,VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,&atlas,nullptr};track_set_write(b);
 VkWriteDescriptorSet c{h,2,0,1,VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC,nullptr,&buf};track_set_write(c);
}
void alloc(VkDescriptorSet h,VkDescriptorSetLayout l){VkDescriptorSetAllocateInfo a{1,1,&l};AllocateDescriptorSets(1,&a,&h);}
bool empty(VkDescriptorSet h){return state.sets[h].empty() && state.dynamic_counts[h]==0;}
int main(){
 fresh();alloc(42,7);check(empty(42)&&state.set_layouts[42]==7,"fresh allocation");
 seed(42);check(state.sets[42].size()==3&&state.dynamic_counts[42]==1,"ordinary partial writes retained in one lifetime");
 alloc(42,8);check(empty(42)&&state.set_layouts[42]==8,"recycled handle changed layout drops old buffer/image/dynamic metadata");
 fresh();alloc(42,7);seed(42);alloc(42,7);check(empty(42),"same-layout handle recycling also starts new lifetime");
 fresh();alloc(42,7);seed(42);alloc(99,8);check(state.sets[42].size()==3,"another set allocation preserves live set");
 allocation_result=-1;alloc(42,9);check(state.sets[42].size()==3&&state.set_layouts[42]==7,"failed allocation preserves prior tracker state");
 allocation_result=0;VkDescriptorSet handles[2]={42,99};VkDescriptorSetLayout layouts[2]={8,9};VkDescriptorSetAllocateInfo batch{2,2,layouts};AllocateDescriptorSets(1,&batch,handles);
 check(empty(42)&&empty(99)&&state.set_layouts[42]==8&&state.set_layouts[99]==9,"batch allocation handles every new lifetime");
 VkDescriptorBufferInfo required{901,0,16384};VkWriteDescriptorSet w{42,13,0,1,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,nullptr,&required};track_set_write(w);
 check(state.sets[42].at(desc_key(13,0)).buffer==901,"new required binding is tracked");
 check(!state.sets[42].count(desc_key(28,0))&&!state.sets[42].count(desc_key(42,0)),"optional material queries cannot see old profile or UI atlas");
 std::cout<<"cases="<<cases<<" failures="<<failures<<"\n";return failures?1:0;
}
