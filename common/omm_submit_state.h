#pragma once
#include <cstdint>
#include <map>
#include <set>
#include <span>
#include <vector>
namespace bg3::omm {
using Id=uint64_t;
inline bool queue_allowed(bool sameQueue,bool references) {return sameQueue || !references;}
// CPU recording graph. Generation, not handle equality, identifies a recording.
struct CommandGraph {
  struct Edge { Id command,generation; std::set<Id> tickets;bool content{},feedback{}; };
  struct Command { Id pool{},generation{}; uint32_t family{}; bool recording{},executable{}; std::set<Id> tickets; std::vector<Edge> children;bool content{},feedback{},hostBarrierAllowed{true}; };
  std::map<Id,Command> commands;
  Id generation{};
  bool resolve(Id id,std::set<Id>& out,std::set<Id>& visited) const {
    auto i=commands.find(id); if(i==commands.end() || !i->second.executable || !visited.insert(id).second)return false;
    const auto& c=i->second;out.insert(c.tickets.begin(),c.tickets.end());
    bool valid=true;
    for(const auto& e:c.children) {
      auto child=commands.find(e.command);
      // Preserve possible references even when the child recording changed.
      out.insert(e.tickets.begin(),e.tickets.end());
      if(child==commands.end() || child->second.generation!=e.generation) valid=false;
      else valid=resolve(e.command,out,visited)&&valid;
    }
    visited.erase(id);return valid;
  }
  std::set<Id> references(const Command& c) const {
    auto refs=c.tickets;for(const auto& e:c.children)refs.insert(e.tickets.begin(),e.tickets.end());return refs;
  }
  bool referenced(Id ticket) const {
    for(const auto& [id,c]:commands)if(references(c).contains(ticket))return true;
    return false;
  }
  bool has_content(Id id) const {auto i=commands.find(id);if(i==commands.end())return false;if(i->second.content)return true;for(const auto& e:i->second.children)if(e.content)return true;return false;}
  bool mark_feedback(Id id) {auto i=commands.find(id);if(i==commands.end() || !i->second.recording)return false;i->second.feedback=true;return true;}
  bool has_feedback(Id id) const {auto i=commands.find(id);if(i==commands.end())return false;if(i->second.feedback)return true;for(const auto& e:i->second.children)if(e.feedback)return true;return false;}
  bool feedback_before_end(Id id,uint32_t family,bool outsidePass) const {
    auto i=commands.find(id);return outsidePass && i!=commands.end() && i->second.recording &&
      i->second.family==family && i->second.hostBarrierAllowed &&
      (!references(i->second).empty() || has_feedback(id));
  }
  bool mark_content(Id id) {auto i=commands.find(id);if(i==commands.end() || !i->second.recording)return false;i->second.content=true;return true;}
  std::set<Id> reset(Id id,bool erase=false) {
    auto i=commands.find(id);if(i==commands.end())return {};
    auto orphan=references(i->second);
    if(erase)commands.erase(i);
    else {auto pool=i->second.pool;auto family=i->second.family;i->second={pool,++generation,family};}
    for(const auto& [key,c]:commands)for(auto t:references(c))orphan.erase(t);
    return orphan;
  }
  std::set<Id> allocate(Id id,Id pool,uint32_t family) {
    auto old=reset(id,true);commands[id]={pool,++generation,family};return old;
  }
  bool begin(Id id,bool hostBarrierAllowed=true) {auto i=commands.find(id);if(i==commands.end())return false;i->second.recording=true;i->second.hostBarrierAllowed=hostBarrierAllowed;return true;}
  bool end(Id id,bool success) {auto i=commands.find(id);if(i==commands.end())return false;i->second.recording=false;i->second.executable=success;return success;}
  bool add(Id id,Id ticket,uint32_t family) {
    auto i=commands.find(id);if(!ticket || i==commands.end() || !i->second.recording || i->second.family!=family)return false;
    i->second.tickets.insert(ticket);return true;
  }
  bool tracked_recording(Id id,uint32_t family) const {
    auto i=commands.find(id);return i!=commands.end() && i->second.recording && i->second.family==family;
  }
  bool execute(Id primary,Id secondary) {
    auto p=commands.find(primary),s=commands.find(secondary);
    if(p==commands.end() || s==commands.end() || !p->second.recording)return false;
    std::set<Id> refs,visited;bool valid=resolve(secondary,refs,visited);
    p->second.children.push_back({secondary,s->second.generation,std::move(refs),has_content(secondary),has_feedback(secondary)});return valid;
  }
  bool submitted(std::span<const Id> ids,std::set<Id>& out) const {
    out.clear();bool valid=true;
    for(auto id:ids) {
      // Untracked unrelated commands contain no successfully registered OMM use.
      if(!commands.contains(id))continue;
      std::set<Id> visited;valid=resolve(id,out,visited)&&valid;
    }
    return valid;
  }
};
struct SubmitSerial {
  uint64_t lastSignal{},lastCompleted{}; bool poisoned{};
  uint64_t candidate() const {return poisoned || lastSignal==UINT64_MAX?0:lastSignal+1;}
  bool signal(uint64_t value,bool success) {
    if(!success || !value || value!=candidate()) {poisoned=true;return false;}
    lastSignal=value;return true;
  }
  bool completed(uint64_t value,bool success) {
    if(poisoned || !success || value<lastCompleted || value>lastSignal) {poisoned=true;return false;}
    lastCompleted=value;return true;
  }
};
}
