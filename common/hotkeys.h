// Portable shortcut grammar and conflict policy. Key codes match Windows virtual keys.
#pragma once
#include <string>
#include <cctype>
namespace bg3::hotkeys {
struct Key { unsigned key=0, mods=0; bool valid=false; };
inline constexpr const char* defaults[2]={"Ctrl+Shift+F9","Ctrl+Shift+F8"};
inline constexpr const char* ids[2]={"hotkey_rt_toggle","hotkey_dlss5"};
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
inline std::string validate(const std::string& rt,const std::string& nr){
  auto a=parse(rt),b=parse(nr),dev=parse("Ctrl+Shift+F10");
  if(!a.valid||!b.valid)return "Use Ctrl, Shift or Alt with F1-F12, A-Z or 0-9, or None.";
  if(conflict(a,b))return "Choose different keyboard shortcuts.";
  if((a.key==dev.key&&(a.mods&3)==3)||(b.key==dev.key&&(b.mods&3)==3))return "Ctrl+Shift+F10 is reserved for the developer target toggle.";
  return {};
}
// Editing must allow recovery when the other saved binding is invalid.
// Save still validates the complete pair.
inline std::string validate_change(const std::string& value,const std::string& other){
  auto error=validate(value,"None");
  if(!error.empty())return error;
  if(conflict(parse(value),parse(other)))return "Choose different keyboard shortcuts.";
  return {};
}
}
