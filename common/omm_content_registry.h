#pragma once
#include <cstdint>
#include <unordered_map>
namespace bg3::omm {
struct ContentRegistry {
  struct Entry {uint64_t imageId{};uint32_t slot{};bool boundSafe{};};
  uint32_t capacity{},nextSlot{};uint64_t nextId{1};
  std::unordered_map<uint64_t,Entry> images;
  explicit ContentRegistry(uint32_t slots=65536):capacity(slots){}
  bool create(uint64_t image,bool policy) {
    images.erase(image);
    if(!image || !policy || nextSlot>=capacity || nextId==UINT64_MAX)return false;
    images.emplace(image,Entry{nextId++,nextSlot++,false});return true;
  }
  void bind(uint64_t image,bool safe) {auto i=images.find(image);if(i!=images.end())i->second.boundSafe=safe;}
  void destroy(uint64_t image) {images.erase(image);}
  const Entry* lookup(uint64_t image) const {auto i=images.find(image);return i==images.end()?nullptr:&i->second;}
};
}
