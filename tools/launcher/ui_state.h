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
inline bool restart(const bg3::settings::Map& baseline,const bg3::settings::Map& candidate) {
  for(auto& kv:candidate)if(bg3::settings::key_change_needs_restart(kv.first,bg3::settings::lookup(baseline,kv.first),kv.second))return true;
  return false;
}
inline bool same_session(const std::string& stored,const std::string& current) { return !current.empty()&&stored==current; }
inline std::string serialize(int stage,bool pending,const std::string& session,const bg3::settings::Map& baseline) {
  std::string s="onboarding="+std::to_string(stage)+"\npending_restart="+(pending?"1":"0")+"\nsession="+session+"\n";
  for(auto& kv:baseline)s+="baseline_"+kv.first+"="+kv.second+"\n";
  return s;
}
}
