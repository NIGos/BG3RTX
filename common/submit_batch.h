#pragma once
#include <algorithm>
#include <deque>
#include <vector>
#include <vulkan/vulkan_core.h>

namespace bg3::submit_batch {
// A lease is per invocation, including nested driver callbacks. Only the outer
// invocation borrows retained storage; recursion owns independent scratch.
template<class T> struct Scratch {
  T retained;
  bool busy = false;
  struct Lease {
    Scratch& owner;
    T local;
    bool borrowed;
    explicit Lease(Scratch& s, bool enabled) : owner(s), borrowed(enabled && !s.busy) {
      if (borrowed) owner.busy = true;
    }
    ~Lease() { if (borrowed) owner.busy = false; }
    T& get() { return borrowed ? owner.retained : local; }
    Lease(const Lease&) = delete;
  };
};

// Copies execute after writes. Exclude the overwritten destination range so
// that copy-then-overlay can be expressed as one Vulkan update without changing
// its final contents. The caller guarantees no use between these updates.
inline void copy_except(std::vector<VkCopyDescriptorSet>& out, VkCopyDescriptorSet copy,
                        uint32_t first, uint32_t count) {
  const uint64_t begin = copy.dstArrayElement, end = begin + copy.descriptorCount;
  const uint64_t lo = (std::max)(begin, uint64_t(first));
  const uint64_t hi = (std::min)(end, uint64_t(first) + count);
  if (!count || lo >= hi) { out.push_back(copy); return; }
  if (lo > begin) { auto prefix = copy; prefix.descriptorCount = uint32_t(lo - begin); out.push_back(prefix); }
  if (hi < end) {
    copy.srcArrayElement += uint32_t(hi - begin);
    copy.dstArrayElement = uint32_t(hi);
    copy.descriptorCount = uint32_t(end - hi); out.push_back(copy);
  }
}

// Own image arrays: pointers remain valid while further writes are appended.
struct DescriptorWrites {
  std::vector<VkWriteDescriptorSet> writes;
  std::deque<std::vector<VkDescriptorImageInfo>> images;
  void add_images(VkWriteDescriptorSet write, const VkDescriptorImageInfo* data, uint32_t count) {
    if (!count) return;
    images.emplace_back(data, data + count);
    write.descriptorCount = count; write.pImageInfo = images.back().data(); writes.push_back(write);
  }
};

// Only ordinary batches are eligible for appending our private signal. Unknown
// extensions/protected submits retain the existing independent signal path.
inline bool appendable(const VkSubmitInfo* submits, uint32_t count) {
  if (!count) return false;
  for (uint32_t i=0;i<count;++i) if (submits[i].pNext) return false;
  return true;
}
inline bool appendable(const VkSubmitInfo2* submits, uint32_t count) {
  if (!count) return false;
  for (uint32_t i=0;i<count;++i) if (submits[i].pNext || submits[i].flags) return false;
  return true;
}

// Stack-owned extension records, backed by the submit invocation's leased
// vectors. No source entry or source semaphore array is modified.
struct PrivateSignal {
  VkTimelineSemaphoreSubmitInfo values{VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO};
  VkSemaphoreSubmitInfo signal{VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO};
  PrivateSignal(VkSemaphore semaphore, uint64_t value) {
    signal.semaphore=semaphore;signal.value=value;signal.stageMask=VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
    values.signalSemaphoreValueCount=1;values.pSignalSemaphoreValues=&signal.value;
  }
  PrivateSignal(const PrivateSignal&) = delete;
  void append(std::vector<VkSubmitInfo>& out,const VkSubmitInfo* source,uint32_t count) {
    out.assign(source,source+count);
    VkSubmitInfo extra{VK_STRUCTURE_TYPE_SUBMIT_INFO,&values};
    extra.signalSemaphoreCount=1;extra.pSignalSemaphores=&signal.semaphore;out.push_back(extra);
  }
  void append(std::vector<VkSubmitInfo2>& out,const VkSubmitInfo2* source,uint32_t count) {
    out.assign(source,source+count);
    VkSubmitInfo2 extra{VK_STRUCTURE_TYPE_SUBMIT_INFO_2};
    extra.signalSemaphoreInfoCount=1;extra.pSignalSemaphoreInfos=&signal;out.push_back(extra);
  }
};
}
