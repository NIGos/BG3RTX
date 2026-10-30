#pragma once
#include <cstdint>
#include <unordered_map>
#include <array>
namespace bg3::omm {
struct ContentRegistry {
  struct Entry {uint64_t imageId{};uint32_t slot{};bool boundSafe{};uint32_t mipLevels{1},format{};};
  struct View {uint64_t image{},imageId{},viewId{};uint32_t baseMip{},levelCount{},format{};std::array<uint32_t,4> components{};};
  uint32_t capacity{},nextSlot{};uint64_t nextId{1};
  std::unordered_map<uint64_t,Entry> images;
  std::unordered_map<uint64_t,View> views;
  uint64_t nextViewId{1};
  explicit ContentRegistry(uint32_t slots=65536):capacity(slots){}
  bool create(uint64_t image,bool policy,uint32_t mipLevels=1,uint32_t format=0) {
    images.erase(image);
    if(!image || !policy || !mipLevels || nextSlot>=capacity || nextId==UINT64_MAX)return false;
    images.emplace(image,Entry{nextId++,nextSlot++,false,mipLevels,format});return true;
  }
  void bind(uint64_t image,bool safe) {auto i=images.find(image);if(i!=images.end())i->second.boundSafe=safe;}
  void destroy(uint64_t image) {images.erase(image);}
  const Entry* lookup(uint64_t image) const {auto i=images.find(image);return i==images.end()?nullptr:&i->second;}
  bool create_view(uint64_t view,uint64_t image,bool policy,uint32_t baseMip,uint32_t levels,
                   uint32_t format,std::array<uint32_t,4> components={}) {
    views.erase(view);const auto* parent=lookup(image);
    if(!view || !policy || !parent || format!=parent->format || nextViewId==UINT64_MAX || baseMip>=parent->mipLevels)return false;
    if(levels==UINT32_MAX)levels=parent->mipLevels-baseMip;
    if(!levels || levels>parent->mipLevels-baseMip)return false;
    views.emplace(view,View{image,parent->imageId,nextViewId++,baseMip,levels,format,components});return true;
  }
  const View* lookup_view(uint64_t view) const {
    auto i=views.find(view);if(i==views.end())return nullptr;
    const auto* parent=lookup(i->second.image);
    return parent && parent->imageId==i->second.imageId?&i->second:nullptr;
  }
  void destroy_view(uint64_t view){views.erase(view);}
};
}
