#include "host.h"
#include "omm_sampler.h"
#include <memory>
namespace bg3::omm {
namespace {
std::mutex samplerMutex;std::atomic<bool> active{};DeviceData* owner{};
SamplerRegistry samplers;
uint64_t nextLayout{1},nextSet{1};
struct Binding {VkDescriptorType type{};uint32_t count{};VkDescriptorBindingFlags flags{};std::vector<SamplerMetadata> immutable;};
struct Layout {uint64_t id{};bool complete{};std::unordered_map<uint32_t,Binding> bindings;};
struct Set {uint64_t id{},version{};VkDescriptorPool pool{};std::shared_ptr<const Layout> layout;bool unsupported{};};
std::unordered_map<VkDescriptorSetLayout,std::shared_ptr<Layout>> layouts;
std::unordered_map<VkDescriptorSet,Set> sets;
uint64_t key(VkSampler sampler){return uint64_t(reinterpret_cast<uintptr_t>(sampler));}
bool sampler_type(VkDescriptorType type){return type==VK_DESCRIPTOR_TYPE_SAMPLER || type==VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;}
const Binding* binding(const Set& s,uint32_t b){if(!s.layout)return nullptr;auto i=s.layout->bindings.find(b);return i==s.layout->bindings.end()?nullptr:&i->second;}
bool range(const Binding* b,uint32_t first,uint32_t count){return b && first<=b->count && count<=b->count-first;}
void version(Set& s){if(s.version==UINT64_MAX)s.unsupported=true;else ++s.version;}
}
void sampler_device_created(DeviceData* dd){std::lock_guard lock(samplerMutex);if(owner)return;owner=dd;active.store(true,std::memory_order_release);}
void sampler_device_destroyed(DeviceData* dd){std::lock_guard lock(samplerMutex);if(owner!=dd)return;active.store(false,std::memory_order_release);sets.clear();layouts.clear();samplers.entries.clear();owner=nullptr;}
bool sampler_tracking_active(){return active.load(std::memory_order_acquire);}
uint64_t sampler_identity(VkSampler sampler){if(!active.load())return 0;std::lock_guard lock(samplerMutex);const auto* entry=samplers.lookup(key(sampler));return entry?entry->id:0;}
void sampler_created(DeviceData* dd,VkSampler sampler,const VkSamplerCreateInfo& info){if(!active.load() || dd!=owner)return;std::lock_guard lock(samplerMutex);samplers.create(key(sampler),sampler,info);}
void sampler_destroyed(DeviceData* dd,VkSampler sampler){if(!active.load() || dd!=owner)return;std::lock_guard lock(samplerMutex);samplers.destroy(key(sampler));}
void sampler_layout_created(DeviceData* dd,VkDescriptorSetLayout handle,const VkDescriptorSetLayoutCreateInfo& info){
  if(!active.load() || dd!=owner)return;std::lock_guard lock(samplerMutex);
  auto layout=std::make_shared<Layout>();layout->id=nextLayout==UINT64_MAX?0:nextLayout++;layout->complete=layout->id!=0;
  const VkDescriptorBindingFlags* flags=nullptr;
  for(auto* p=static_cast<const VkBaseInStructure*>(info.pNext);p;p=p->pNext){
    if(p->sType==VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO && !flags){
      const auto* f=reinterpret_cast<const VkDescriptorSetLayoutBindingFlagsCreateInfo*>(p);
      if(f->bindingCount!=info.bindingCount || (f->bindingCount && !f->pBindingFlags)){layout->complete=false;break;}flags=f->pBindingFlags;
    }else{layout->complete=false;break;}
  }
  for(uint32_t i=0;i<info.bindingCount;++i){const auto& b=info.pBindings[i];
    Binding out{b.descriptorType,b.descriptorCount,flags?flags[i]:0,{}};
    if(sampler_type(b.descriptorType) && b.pImmutableSamplers)for(uint32_t j=0;j<b.descriptorCount;++j){const auto* snapshot=samplers.lookup(key(b.pImmutableSamplers[j]));out.immutable.push_back(snapshot?*snapshot:SamplerMetadata{});}
    layout->bindings.emplace(b.binding,std::move(out));
  }
  layouts[handle]=std::move(layout);
}
void sampler_layout_destroyed(DeviceData* dd,VkDescriptorSetLayout layout){if(!active.load() || dd!=owner)return;std::lock_guard lock(samplerMutex);layouts.erase(layout);}
void sampler_set_allocated(DeviceData* dd,VkDescriptorSet set,VkDescriptorSetLayout layout,VkDescriptorPool pool,bool simpleAllocation){
  if(!active.load() || dd!=owner)return;std::lock_guard lock(samplerMutex);auto l=layouts.find(layout);
  Set record;record.id=nextSet==UINT64_MAX?0:nextSet++;record.pool=pool;record.unsupported=!simpleAllocation;if(l!=layouts.end())record.layout=l->second;sets[set]=record;
  if(!record.layout)return;
  // Seed immutable descriptors so the existing descriptor-copy decoder also
  // copies the actual sampler rather than leaving a stale destination entry.
  for(const auto& [b,def]:record.layout->bindings)for(uint32_t i=0;i<def.immutable.size();++i){
    DescEntry entry{b,i,def.type,VK_NULL_HANDLE,VK_NULL_HANDLE,0,0,VK_IMAGE_LAYOUT_UNDEFINED};entry.sampler=def.immutable[i].sampler;entry.sampler_id=def.immutable[i].id;
    tracker().sets[set][desc_key(b,i)]=entry;
  }
}
void sampler_sets_freed(DeviceData* dd,uint32_t count,const VkDescriptorSet* freed){if(!active.load() || dd!=owner)return;std::lock_guard lock(samplerMutex);for(uint32_t i=0;i<count;++i)sets.erase(freed[i]);}
void sampler_pool_reset(DeviceData* dd,VkDescriptorPool pool){if(!active.load() || dd!=owner)return;std::lock_guard lock(samplerMutex);for(auto i=sets.begin();i!=sets.end();)if(i->second.pool==pool)i=sets.erase(i);else ++i;}
bool immutable_sampler(VkDescriptorSet set,uint32_t b,uint32_t element,VkSampler& sampler,uint64_t& id){
  if(!active.load())return false;std::lock_guard lock(samplerMutex);auto s=sets.find(set);if(s==sets.end())return false;const auto* def=binding(s->second,b);
  if(!def || element>=def->immutable.size())return false;sampler=def->immutable[element].sampler;id=def->immutable[element].id;return true;
}
void sampler_template_write(VkDescriptorSet set,uint32_t b,uint32_t element,VkDescriptorType type){
  if(!active.load() || !sampler_type(type))return;std::lock_guard lock(samplerMutex);auto s=sets.find(set);if(s==sets.end())return;
  version(s->second);const auto* def=binding(s->second,b);if(!range(def,element,1) || def->type!=type)s->second.unsupported=true;
}
void sampler_descriptor_ranges(DeviceData* dd,uint32_t wc,const VkWriteDescriptorSet* writes,uint32_t cc,const VkCopyDescriptorSet* copies){
  if(!active.load() || dd!=owner)return;std::shared_lock tracked(tracker().mu);std::lock_guard lock(samplerMutex);
  for(uint32_t i=0;i<wc;++i){const auto& w=writes[i];if(!sampler_type(w.descriptorType))continue;auto s=sets.find(w.dstSet);if(s==sets.end())continue;version(s->second);const auto* b=binding(s->second,w.dstBinding);
    if(!range(b,w.dstArrayElement,w.descriptorCount) || !b || b->type!=w.descriptorType || !w.pImageInfo || w.pNext)s->second.unsupported=true;
  }
  for(uint32_t i=0;i<cc;++i){const auto& c=copies[i];auto dst=sets.find(c.dstSet);if(dst==sets.end())continue;const auto* db=binding(dst->second,c.dstBinding);if(!db)continue;
    version(dst->second);if(db->type==VK_DESCRIPTOR_TYPE_SAMPLER && !db->immutable.empty())continue;
    auto src=sets.find(c.srcSet);const auto* sb=src==sets.end()?nullptr:binding(src->second,c.srcBinding);
    if(!range(db,c.dstArrayElement,c.descriptorCount) || !range(sb,c.srcArrayElement,c.descriptorCount) || sb->type!=db->type || src->second.unsupported || !src->second.layout->complete){dst->second.unsupported=true;continue;}
    auto entries=tracker().sets.find(c.srcSet);if(entries==tracker().sets.end()){dst->second.unsupported=true;continue;}
    for(uint32_t k=0;k<c.descriptorCount;++k)if(!entries->second.contains(desc_key(c.srcBinding,c.srcArrayElement+k)))dst->second.unsupported=true;
  }
}
bool bound_sampler(VkCommandBuffer cmd,VkPipelineBindPoint bp,uint32_t set,uint32_t b,uint32_t element,BoundSampler& out){
  out={};if(!active.load() || device_of(cmd)!=owner || (bp!=VK_PIPELINE_BIND_POINT_GRAPHICS && bp!=VK_PIPELINE_BIND_POINT_COMPUTE))return false;
  CmdState* state=cmd_state(cmd);if(!state || set>=state->sets[0].size())return false;const unsigned bi=bp==VK_PIPELINE_BIND_POINT_COMPUTE?1:0;
  if(state->push_set[bi]==set){out.status=BoundSampler::Status::UnsupportedPush;return false;}
  const auto nativeSet=state->sets[bi][set];std::shared_lock tracked(tracker().mu);std::lock_guard lock(samplerMutex);
  auto s=sets.find(nativeSet);if(s==sets.end())return false;
  out.descriptorSetId=s->second.id;out.writeVersion=s->second.version;out.descriptorSet=nativeSet;out.binding=b;out.element=element;
  if(!s->second.id || !s->second.layout || !s->second.layout->complete){out.status=BoundSampler::Status::UnsupportedLayout;return false;}
  out.layoutId=s->second.layout->id;if(s->second.unsupported){out.status=BoundSampler::Status::UnsupportedRange;return false;}
  const auto* def=binding(s->second,b);if(!def || !sampler_type(def->type) || element>=def->count)return false;out.bindingFlags=def->flags;
  const SamplerMetadata* expected=nullptr;
  if(!def->immutable.empty()){expected=&def->immutable[element];out.immutable=true;}
  else{auto entries=tracker().sets.find(nativeSet);if(entries==tracker().sets.end())return false;auto e=entries->second.find(desc_key(b,element));if(e==entries->second.end())return false;expected=samplers.lookup(key(e->second.sampler));
    if(!expected || expected->id!=e->second.sampler_id){out.status=BoundSampler::Status::DeadSampler;return false;}}
  if(!expected)return false;out.sampler=*expected;const auto* live=samplers.lookup(key(expected->sampler));
  if(!live || live->id!=expected->id){out.status=BoundSampler::Status::DeadSampler;return false;}
  out.status=expected->complete?BoundSampler::Status::CompleteMetadata:BoundSampler::Status::UnsupportedSampler;
  return expected->complete;
}
bool immutable_sampler_current(const BoundSampler& saved){
  if(!active.load() || !saved.immutable)return false;std::lock_guard lock(samplerMutex);auto s=sets.find(saved.descriptorSet);
  if(s==sets.end() || s->second.id!=saved.descriptorSetId || s->second.unsupported || !s->second.layout || !s->second.layout->complete || s->second.layout->id!=saved.layoutId)return false;
  const auto* def=binding(s->second,saved.binding);if(!def || saved.element>=def->immutable.size())return false;
  const auto& expected=def->immutable[saved.element];const auto* live=samplers.lookup(key(expected.sampler));
  return live && live->complete && live->id==expected.id && live->id==saved.sampler.id;
}
bool ordinary_frozen_binding(VkCommandBuffer cmd,VkPipelineBindPoint bp,uint32_t set,uint32_t b,uint32_t element){
  if(!active.load() || device_of(cmd)!=owner || (bp!=VK_PIPELINE_BIND_POINT_GRAPHICS && bp!=VK_PIPELINE_BIND_POINT_COMPUTE))return false;
  CmdState* state=cmd_state(cmd);if(!state || set>=state->sets[0].size())return false;const unsigned bi=bp==VK_PIPELINE_BIND_POINT_COMPUTE?1:0;if(state->push_set[bi]==set)return false;
  std::lock_guard lock(samplerMutex);auto s=sets.find(state->sets[bi][set]);if(s==sets.end() || !s->second.id || s->second.unsupported || !s->second.layout || !s->second.layout->complete)return false;
  const auto* def=binding(s->second,b);return def && element<def->count && def->flags==0;
}
}
