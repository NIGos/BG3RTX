// Portable shortcut grammar and conflict policy. Key codes match Windows virtual keys.
#pragma once
#include <string>
#include <cctype>
namespace bg3::hotkeys {
struct Key { unsigned key=0, mods=0; bool valid=false; };
// 0: master RT toggle, 1: DLSS 5, 2: RTX Hair (rt_hair, layer/util.cpp). Ctrl+Shift+F10 is the developer target toggle.
inline constexpr int kCount=3;
inline constexpr const char* defaults[kCount]={"Ctrl+Shift+F9","Ctrl+Shift+F8","Ctrl+Shift+F7"};
inline constexpr const char* ids[kCount]={"hotkey_rt_toggle","hotkey_dlss5","hotkey_rtx_hair"};
inline int index_of(const std::string& id){for(int i=0;i<kCount;++i)if(id==ids[i])return i;return -1;}
inline Key parse(std::string text) {
  for(char& c:text)c=char(std::toupper(static_cast<unsigned char>(c)));
  if(text=="NONE")return {0,0,true};
  Key out; size_t at=0;
  while(true){auto end=text.find('+',at);auto part=text.substr(at,end==std::string::npos?end:end-at);
    if(end!=std::string::npos){unsigned mod=part=="CTRL"?1:part=="SHIFT"?2:part=="ALT"?4:0;
      if(!mod||(out.mods&mod))return {};out.mods|=mod;at=end+1;continue;}
    if(part.size()==1&&((part[0]>='A'&&part[0]<='Z')||(part[0]>='0'&&part[0]<='9')))out.key=part[0];
    else if(part.size()>=2&&part[0]=='F'){
      for(unsigned n=1;n<=12;++n)if(part=="F"+std::to_string(n))out.key=0x70+n-1;
    }
    out.valid=out.key!=0;return out;
  }
}
inline bool conflict(Key a,Key b){return a.valid&&b.valid&&a.key&&a.key==b.key&&a.mods==b.mods;}
inline std::string format(Key k){
  if(!k.key)return "None";
  return std::string(k.mods&1?"Ctrl+":"")+(k.mods&2?"Shift+":"")+(k.mods&4?"Alt+":"")+
    (k.key>=0x70?"F"+std::to_string(k.key-0x70+1):std::string(1,char(k.key)));
}
inline bool reserved(Key k){auto dev=parse("Ctrl+Shift+F10");return k.key==dev.key&&(k.mods&3)==3;}
// Every binding of a complete set (kCount values): valid, none reserved, no two equal.
inline std::string validate_all(const std::string* values,int n=kCount){
  Key k[kCount];
  for(int i=0;i<n&&i<kCount;++i){k[i]=parse(values[i]);if(!k[i].valid)return "Use Ctrl, Shift or Alt with F1-F12, A-Z or 0-9, or None.";}
  for(int i=0;i<n&&i<kCount;++i)for(int j=i+1;j<n&&j<kCount;++j)if(conflict(k[i],k[j]))return "Choose different keyboard shortcuts.";
  for(int i=0;i<n&&i<kCount;++i)if(reserved(k[i]))return "Ctrl+Shift+F10 is reserved for the developer target toggle.";
  return {};
}
inline std::string validate(const std::string& rt,const std::string& nr){const std::string v[2]={rt,nr};return validate_all(v,2);}
inline std::string validate(const std::string& rt,const std::string& nr,const std::string& hair){const std::string v[3]={rt,nr,hair};return validate_all(v,3);}
// Editing must allow recovery when the other saved binding is invalid.
// Save still validates the complete pair.
inline std::string validate_change(const std::string& value,const std::string& other){
  auto error=validate(value,"None");
  if(!error.empty())return error;
  if(conflict(parse(value),parse(other)))return "Choose different keyboard shortcuts.";
  return {};
}
// Binding `index` of `current` (kCount values) set to `value`: checked against every other binding.
inline std::string validate_change_at(const std::string* current,int index,const std::string& value){
  for(int i=0;i<kCount;++i){if(i==index)continue;auto error=validate_change(value,current[i]);if(!error.empty())return error;}
  return validate_change(value,"None");
}
}
