#pragma once
#include <string>
#include <vector>
#include "../../common/settings.h"
namespace launcher::ui_state {
inline int dirty(const std::vector<int>& edited,const std::vector<int>& saved,int preset,int saved_preset,
                 const std::wstring& dir,const std::wstring& saved_dir) {
  int n=(preset!=saved_preset)+(dir!=saved_dir);
  for(size_t i=0;i<edited.size();++i)n+=i>=saved.size()||edited[i]!=saved[i];
  return n;
}
// Count controls, not their bundled raw keys. Unexposed startup prerequisites count once each.
// Compare the saved candidate to the original process baseline, never the last Save or draft.
inline int restart_count(const bg3::settings::Map& baseline,const bg3::settings::Map& candidate) {
  namespace st=bg3::settings;
  std::vector<std::string> changed;
  auto visit=[&](const st::Map& source) {
    for(auto& kv:source) {
      if(std::find(changed.begin(),changed.end(),kv.first)!=changed.end())continue;
      if(st::key_change_needs_restart(kv.first,st::lookup(baseline,kv.first),st::lookup(candidate,kv.first)))changed.push_back(kv.first);
    }
  };
  visit(candidate);visit(baseline);
  int n=0;
  for(auto& knob:st::kKnobs) {
    bool affected=false;
    auto consume=[&](const char* key) {
      auto it=std::find(changed.begin(),changed.end(),key);
      if(it!=changed.end()){affected=true;changed.erase(it);}
    };
    if(knob.kind==st::Kind::Slider){for(auto key:knob.slider.keys)if(key)consume(key);}
    else for(auto& choice:knob.choices)if(choice.id)for(auto& kv:choice.kv)if(kv.key)consume(kv.key);
    n+=affected;
  }
  return n+int(changed.size());
}
inline bool restart(const bg3::settings::Map& baseline,const bg3::settings::Map& candidate) {
  return restart_count(baseline,candidate)>0;
}
inline std::string restart_banner(int count) {
  return count>0?"Restart the game to apply "+std::to_string(count)+(count==1?" change.":" changes."):"";
}
// The same changes by name, for the page: the settings a player can see (release-visible or opt-in), in table order,
// and the number of changed startup keys that belong to none of them. names.size()+others == restart_count().
inline std::vector<std::string> restart_names(const bg3::settings::Map& baseline,const bg3::settings::Map& candidate,int* others=nullptr) {
  namespace st=bg3::settings;
  std::vector<std::string> changed,names;
  auto visit=[&](const st::Map& source) {
    for(auto& kv:source) {
      if(std::find(changed.begin(),changed.end(),kv.first)!=changed.end())continue;
      if(st::key_change_needs_restart(kv.first,st::lookup(baseline,kv.first),st::lookup(candidate,kv.first)))changed.push_back(kv.first);
    }
  };
  visit(candidate);visit(baseline);
  int hidden=0;
  for(auto& knob:st::kKnobs) {
    bool affected=false;
    auto consume=[&](const char* key) {
      auto it=std::find(changed.begin(),changed.end(),key);
      if(it!=changed.end()){affected=true;changed.erase(it);}
    };
    if(knob.kind==st::Kind::Slider){for(auto key:knob.slider.keys)if(key)consume(key);}
    else for(auto& choice:knob.choices)if(choice.id)for(auto& kv:choice.kv)if(kv.key)consume(kv.key);
    if(!affected)continue;
    if(st::knob_visible(knob,false)||st::opt_in_knob(knob))names.push_back(knob.label);else ++hidden;
  }
  if(others)*others=hidden+int(changed.size());
  return names;
}
// "Restart the game to apply: Water lighting, Hair shadows and 1 more setting."
inline std::string restart_banner(const std::vector<std::string>& names,int others) {
  if(names.empty())return restart_banner(others);
  std::string s="Restart the game to apply: ";
  for(size_t i=0;i<names.size();++i)s+=(i?", ":"")+names[i];
  if(others>0)s+=" and "+std::to_string(others)+(others==1?" more setting":" more settings");
  return s+".";
}
inline bool same_session(const std::string& stored,const std::string& current) { return !current.empty()&&stored==current; }
inline std::string serialize(int stage,bool pending,const std::string& session,const bg3::settings::Map& baseline) {
  std::string s="onboarding="+std::to_string(stage)+"\npending_restart="+(pending?"1":"0")+"\nsession="+session+"\n";
  for(auto& kv:baseline)s+="baseline_"+kv.first+"="+kv.second+"\n";
  return s;
}
}
