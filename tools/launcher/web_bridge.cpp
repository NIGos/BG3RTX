#include "web_bridge.h"
#include "version.h"
#include "ui_state.h"
#include "../../common/hotkeys.h"
#include <charconv>
#include <variant>
#include <climits>
#include <map>

namespace launcher::web {
namespace st = bg3::settings;
namespace {
using Atom = std::variant<std::string, int, bool>;
using Object = std::map<std::string, Atom>;
// Intents are a bounded flat JSON object, deliberately not a general document
// protocol. Reject duplicate fields, trailing data, nesting and fractional IDs.
struct Parser {
  const std::string& text; size_t at = 0;
  void ws() { while (at < text.size() && (text[at]==' '||text[at]=='\n'||text[at]=='\r'||text[at]=='\t')) ++at; }
  bool take(char c) { ws(); if(at==text.size()||text[at]!=c)return false; ++at;return true; }
  bool string(std::string& out) {
    if(!take('"'))return false;
    while(at<text.size()) {
      unsigned char c=text[at++]; if(c=='"')return true; if(c<32)return false;
      if(c!='\\'){out+=char(c);continue;} if(at==text.size())return false;
      c=text[at++];
      if(c=='"'||c=='\\'||c=='/')out+=char(c);
      else if(c=='n')out+='\n';else if(c=='r')out+='\r';else if(c=='t')out+='\t';
      else if(c=='b')out+='\b';else if(c=='f')out+='\f';
      else if(c=='u') {
        auto hex=[&](unsigned& n){n=0;for(int i=0;i<4;++i){if(at==text.size())return false;char h=text[at++];int v=h>='0'&&h<='9'?h-'0':h>='a'&&h<='f'?h-'a'+10:h>='A'&&h<='F'?h-'A'+10:-1;if(v<0)return false;n=n*16+v;}return true;};
        unsigned n;if(!hex(n))return false;
        if(n>=0xd800&&n<=0xdbff){if(at+2>text.size()||text.substr(at,2)!="\\u")return false;at+=2;unsigned lo;if(!hex(lo)||lo<0xdc00||lo>0xdfff)return false;n=0x10000+((n-0xd800)<<10)+lo-0xdc00;}
        else if(n>=0xdc00&&n<=0xdfff)return false;
        if(n<0x80)out+=char(n);else if(n<0x800){out+=char(0xc0|(n>>6));out+=char(0x80|(n&63));}
        else if(n<0x10000){out+=char(0xe0|(n>>12));out+=char(0x80|((n>>6)&63));out+=char(0x80|(n&63));}
        else{out+=char(0xf0|(n>>18));out+=char(0x80|((n>>12)&63));out+=char(0x80|((n>>6)&63));out+=char(0x80|(n&63));}
      } else return false;
    } return false;
  }
  bool parse(Object& out) {
    if(text.size()>65536||!take('{'))return false;if(take('}')){ws();return at==text.size();}
    do {std::string key;if(!string(key)||!take(':')||out.count(key))return false;ws();Atom val;
      if(at<text.size()&&text[at]=='"'){std::string s;if(!string(s))return false;val=s;}
      else if(text.compare(at,4,"true")==0){at+=4;val=true;}
      else if(text.compare(at,5,"false")==0){at+=5;val=false;}
      else{size_t start=at;if(at<text.size()&&text[at]=='-')++at;size_t digits=at;while(at<text.size()&&text[at]>='0'&&text[at]<='9')++at;if(at==digits||(at-digits>1&&text[digits]=='0'))return false;int n;auto r=std::from_chars(text.data()+start,text.data()+at,n);if(r.ec!=std::errc())return false;val=n;}
      out.emplace(key,val);if(out.size()>12)return false;
    }while(take(','));if(!take('}'))return false;ws();return at==text.size();
  }
};
template<class T> T get(const Object& o,const char* key,T fallback={}) {auto i=o.find(key);if(i==o.end())return fallback;auto p=std::get_if<T>(&i->second);return p?*p:fallback;}
std::string q(const std::string& s){std::string out="\"";constexpr char hex[]="0123456789abcdef";for(unsigned char c:s){if(c=='"'||c=='\\'){out+='\\';out+=char(c);}else if(c<32){out+="\\u00";out+=hex[c>>4];out+=hex[c&15];}else out+=char(c);}return out+'"';}
const char* b(bool value){return value?"true":"false";}
std::string strings(const std::vector<std::string>& values){std::string out="[";for(auto& x:values){if(out.size()>1)out+=",";out+=q(x);}return out+"]";}
std::string capability_requirements(unsigned mask, bool release = true) {
  const std::pair<unsigned,const char*> names[] = {
    {st::kCapRayQuery,"Hardware ray tracing"}, {st::kCapDlss,"Compatible NVIDIA RTX GPU with DLSS"},
    {st::kCapFg,"Frame Generation support"}, {st::kCapMfg,"Multi Frame Generation support"},
    {st::kCapOpacityMicromap,"opacity micromap support"},
    {st::kCapLinearSweptSpheres,"linear swept spheres support"},
    {st::kCapShaderInt64,"64-bit shader integers (shaderInt64)"},
    {st::kCapFragmentStores,"fragment shader stores (fragmentStoresAndAtomics)"},
    {st::kCapHairGrooms,"valid local hair groom files"}};
  std::string text;
  for (const auto& item:names) if(mask & item.first){if(!text.empty())text+="; ";text+=release?"Requires ":"needs ";
    if(!release&&item.first==st::kCapRayQuery)text+="hardware ray queries";
    else if(!release&&item.first==st::kCapDlss)text+="NVIDIA RTX DLSS";
    else if(!release&&item.first==st::kCapFg)text+="frame generation support";
    else if(!release&&item.first==st::kCapMfg)text+="multi-frame generation support";
    else text+=item.second;}
  return text;
}
// Release copy: short names, met hardware never shown, one entry per requirement.
const char* capability_name(unsigned cap){
  switch(cap){
    case st::kCapRayQuery:return "hardware ray tracing";
    case st::kCapDlss:return "an NVIDIA RTX GPU with DLSS";
    case st::kCapFg:return "Frame Generation support";
    case st::kCapMfg:return "Multi Frame Generation support";
    case st::kCapOpacityMicromap:return "opacity micromap support";
    case st::kCapLinearSweptSpheres:return "linear swept spheres support";
    case st::kCapShaderInt64:return "64-bit shader integers (shaderInt64)";
    case st::kCapFragmentStores:return "fragment shader stores (fragmentStoresAndAtomics)";
    case st::kCapHairGrooms:return "local hair groom files";
  }return "";
}
std::vector<std::string> capability_list(unsigned mask){
  std::vector<std::string> out;
  for(unsigned cap:{st::kCapRayQuery,st::kCapDlss,st::kCapFg,st::kCapMfg,st::kCapOpacityMicromap,st::kCapLinearSweptSpheres,st::kCapShaderInt64,st::kCapFragmentStores,st::kCapHairGrooms})
    if(mask&cap)out.push_back(capability_name(cap));
  return out;
}
std::string join(const std::vector<std::string>& items,const char* separator){std::string out;for(auto& x:items){if(!out.empty())out+=separator;out+=x;}return out;}
void add_unique(std::vector<std::string>& items,std::string item){if(!item.empty()&&std::find(items.begin(),items.end(),item)==items.end())items.push_back(std::move(item));}
std::string capitalised(std::string text){if(!text.empty()&&text[0]>='a'&&text[0]<='z')text[0]=char(text[0]-'a'+'A');return text;}
// Unmet requirement of one choice, or "". Startup keys are written by Save, so they never block.
std::string release_reason(const st::Knob& k,int c,unsigned caps,const std::vector<int>& values){
  if(!st::choice_supported(k,c,caps))return "Needs "+join(capability_list(k.choices[c].caps&~caps)," and ");
  for(auto& n:k.choices[c].need)if(n.knob){int j=st::knob_index(n.knob);
    if(j>=0&&values[j]>=0&&values[j]<n.min_choice)return "Needs "+std::string(st::kKnobs[j].label)+": "+st::kKnobs[j].choices[n.min_choice].label;}
  return {};
}
// Every requirement of one choice for Details, hardware included, without duplicates.
std::string release_requirements(const st::Knob& k,int c){
  std::vector<std::string> items;
  for(auto& name:capability_list(k.choices[c].caps))add_unique(items,capitalised(name));
  for(auto& n:k.choices[c].need)if(n.knob){int j=st::knob_index(n.knob);if(j>=0)add_unique(items,std::string(st::kKnobs[j].label)+": "+st::kKnobs[j].choices[n.min_choice].label);}
  for(auto& n:k.required_on)if(n.key&&c>=n.min_choice)add_unique(items,"Sets at startup: "+std::string(n.label));
  return join(items,"; ");
}
// Replaces one raw key of the user file text; render_user_ini keeps it as an unmanaged line.
std::string with_raw_key(const std::string& text,const std::string& key,const std::string& value){
  std::string out;size_t pos=0;
  while(pos<text.size()){size_t nl=text.find('\n',pos);std::string line=text.substr(pos,nl==std::string::npos?std::string::npos:nl-pos+1);pos=nl==std::string::npos?text.size():nl+1;
    std::string s=line;auto hash=s.find('#');if(hash!=std::string::npos)s.resize(hash);auto eq=s.find('=');
    if(eq!=std::string::npos&&st::trim(s.substr(0,eq))==key)continue;out+=line;}
  if(!value.empty()){if(!out.empty()&&out.back()!='\n')out+='\n';out+=key+"="+value+"\n";}
  return out;
}
std::string label(int i,int value){
  auto& k=st::kKnobs[i];if(value<0)return "Custom value";if(k.kind==st::Kind::Choice)return k.choices[value].label;
  if(k.slider.has_off&&value==0)return "Off";const std::string unit=k.slider.unit?k.slider.unit:"";
  return std::to_string(value)+(unit.empty()||unit=="%"?unit:" "+unit);
}
// The whole set of in-game shortcuts: each valid alone, no two the same (hotkeys::validate per pair).
template<class A> std::string shortcuts_error(const A& s){
  for(size_t i=0;i<s.size();++i)for(size_t j=i+1;j<s.size();++j){auto e=bg3::hotkeys::validate(s[i],s[j]);if(!e.empty())return e;}
  if(s.size()==1)return bg3::hotkeys::validate(s[0],"None");
  return {};
}
// One review line: "Name: from → to", marked when the change waits for the next game start.
std::string change_line(int i,int from,int to){
  return std::string(st::kKnobs[i].label)+": "+label(i,from)+" \xE2\x86\x92 "+label(i,to)+
    (st::transition_apply(st::kKnobs[i],from,to)==st::Apply::Restart?" (after a game restart)":"");
}
}
Bridge::Bridge(State& state,Hooks hooks):state_(state),hooks_(std::move(hooks)),saved_(state.values),saved_preset_(state.preset) {
  auto m=effective();for(size_t i=0;i<kShortcutCount;++i){auto v=st::lookup(m,bg3::hotkeys::ids[i]);if(!v.empty())state_.shortcuts[i]=state_.loaded_shortcuts[i]=v;}dev=st::same_value(st::lookup(m,"dev"),"1");
  experimental=saved_experimental_=!dev&&st::same_value(st::lookup(m,"experimental_options"),"1");
  for(int i=0;i<st::kKnobCount;++i){auto& k=st::kKnobs[i];if(!shown(k)||(k.kind==st::Kind::Choice&&!st::choice_visible(k,state_.values[i],dev,opt_in())))state_.values[i]=-1;}
  saved_=state_.values;
}
st::Map Bridge::effective() const {st::Map m;st::apply_layer(m,state_.base);st::apply_layer(m,state_.user);for(int i=0;i<st::kKnobCount;++i){st::KVList kv;st::knob_kvs(st::kKnobs[i],state_.values[i],kv);for(auto& v:kv)m[v.first]=v.second;}return m;}
int Bridge::dirty() const {return std::max(int(ab_selection_.pending),ui_state::dirty(state_.values,saved_,state_.preset,saved_preset_,L"",L"")+int(!dev&&experimental!=saved_experimental_)+int(state_.dlss5_runtime_path!=state_.loaded_dlss5_runtime_path)+changed_shortcuts());}
int Bridge::changed_shortcuts() const {int n=0;for(size_t i=0;i<kShortcutCount;++i)n+=int(state_.shortcuts[i]!=state_.loaded_shortcuts[i]);return n;}
bool Bridge::shown(const st::Knob& k) const {return st::knob_visible(k,dev,opt_in());}
bool Bridge::is_collection_intent(const std::string& message){Object o;return Parser{message}.parse(o)&&get<std::string>(o,"type")=="prepareReport";}
bool Bridge::is_export_intent(const std::string& message){Object o;return Parser{message}.parse(o)&&get<std::string>(o,"type")=="exportReport";}
bool Bridge::is_cancel_collection_intent(const std::string& message){Object o;if(!Parser{message}.parse(o))return false;auto type=get<std::string>(o,"type");return type=="closeReport"||type=="cancelReport";}
void Bridge::set_compatibility(GraphicsInspection p,int selected){pending_play_=false;preview_kind_.clear();inspection_=std::move(p);selected_=selected<0?default_adapter(inspection_):selected;compatibility_=assess_compatibility(inspection_,selected_);state_.caps=compatibility_.caps;state_.suggested=compatibility_.model_preset;state_.have_gpu=selected_>=0&&size_t(selected_)<inspection_.adapters.size();if(state_.have_gpu)state_.gpu=inspection_.adapters[selected_].gpu;}
bool Bridge::save_preview(bool enable_required){
  auto shortcut_error=shortcuts_error(state_.shortcuts);
  if(!shortcut_error.empty()){blocked_={shortcut_error};preview_kind_="save";return false;}
  auto p=st::preview_dependencies(state_.values,effective(),state_.caps,enable_required,dev,experimental,opt_in(),&saved_);
  preview_kind_="save";preview_values_=p.values;preview_preset_=state_.preset;preview_keys_=p.required_keys;ab_selection_.append(preview_keys_);blocked_=p.blocked;changes_.clear();
  for(int i:p.changed)changes_.push_back(change_line(i,state_.values[i],p.values[i]));
  for(auto& kv:p.required_keys){
    std::string name="Required startup setting";
    for(int i=0;i<st::kKnobCount;++i){auto& k=st::kKnobs[i];if(!shown(k))continue;
      for(auto& n:k.required_on)if(n.key&&kv.first==n.key&&preview_values_[i]>=n.min_choice){name=std::string(k.label)+": "+n.label;break;}
      if(name!="Required startup setting")break;
    }
    changes_.push_back(name+": set at the next game start");
  }
  return blocked_.empty();
}
std::string Bridge::handle(const std::string& message){
  Object o;if(!Parser{message}.parse(o)){status_="Launcher request failed. Reopen the launcher.";return state_json();}
  auto type=get<std::string>(o,"type");
  auto refresh_runtime=[&](){if(hooks_.dlss5RuntimeStatus)state_.dlss5_runtime_status=hooks_.dlss5RuntimeStatus(state_.dlss5_runtime_path);};
  if(type=="init"||type=="state")return state_json();
  if(type=="cancel"){pending_play_=false;preview_kind_.clear();changes_.clear();blocked_.clear();return state_json();}
  if(type=="confirm"){
    if(preview_kind_=="preset"){state_.values=preview_values_;state_.preset=preview_preset_;preview_kind_.clear();}
    else if(preview_kind_=="save"&&blocked_.empty()){
      State candidate=state_;candidate.values=preview_values_;std::string error;
      // The release opt-in travels with the settings it unlocks; Off removes the key.
      if(!dev)candidate.user_text=with_raw_key(candidate.user_text,"experimental_options",experimental?"1":"");
      if(hooks_.save&&hooks_.save(candidate,preview_keys_,error)){state_=std::move(candidate);state_.loaded_dlss5_runtime_path=state_.dlss5_runtime_path;state_.loaded_shortcuts=state_.shortcuts;ab_selection_.pending=false;saved_=state_.values;saved_preset_=state_.preset;saved_experimental_=experimental;preview_kind_.clear();status_="Saved.";}
      else {pending_play_=false;status_=error.empty()?"Settings not saved. Your edits are kept.":error;}
    }
    if(pending_play_&&preview_kind_.empty()){
      pending_play_=false;std::string error;
      if(!can_play_||!hooks_.play||!hooks_.play(error))status_=error.empty()?"Settings saved, but the game could not start. See Help & support.":error;
      else{status_=error.empty()?"Game started.":error;can_play_=false;}
    }return state_json();
  }
  // An edit invalidates the exact preview to prevent stale confirmations.
  if(type=="shortcut"||type=="resetShortcut"||type=="set"||type=="preset"||type=="discard"||type=="experimental"||type=="browseDlss5Runtime"||type=="clearDlss5Runtime"){preview_kind_.clear();pending_play_=false;}
  if(type=="shortcut"||type=="resetShortcut"){
    auto id=get<std::string>(o,"id");int index=-1;
    for(size_t i=0;i<kShortcutCount;++i)if(id==bg3::hotkeys::ids[i])index=int(i);
    if(index<0){status_="Unknown keyboard shortcut.";return state_json();}
    auto value=type=="resetShortcut"?std::string(bg3::hotkeys::defaults[index]):get<std::string>(o,"value");
    // A change is checked against every other binding; Save checks the whole set again.
    auto error=bg3::hotkeys::validate_change(value,"None");
    for(size_t i=0;i<kShortcutCount&&error.empty();++i)if(int(i)!=index)error=bg3::hotkeys::validate_change(value,state_.shortcuts[i]);
    if(!error.empty())status_=error;
    else {state_.shortcuts[index]=bg3::hotkeys::format(bg3::hotkeys::parse(value));status_="Keyboard shortcut changed. Save to apply.";}
  }
  else if(type=="set"){
    int i=st::knob_index(get<std::string>(o,"id"));int value=get<int>(o,"value",INT_MIN);
    if(i<0||!shown(st::kKnobs[i])){status_="This option is unavailable. Check its requirements in Help & support.";return state_json();}
    auto& k=st::kKnobs[i];bool valid=k.kind==st::Kind::Choice?st::choice_available(k,value,dev,experimental,state_.caps,opt_in()):((k.slider.has_off&&value==0)||(value>=k.slider.min&&value<=k.slider.max&&(value-k.slider.min)%k.slider.step==0));
    if(st::choice_requires_experimental(k,value)&&!experimental)valid=false;if(valid){ab_selection_.touch(k,value,opt_in());state_.values[i]=value;status_.clear();
      // One option turns on what it needs (all or nothing); an unmet need keeps showing its reason on the card.
      std::vector<int> raised;
      if(k.kind==st::Kind::Choice&&value>0&&st::raise_needs(state_.values,i,value,dev,experimental,state_.caps,opt_in(),&raised)&&!raised.empty()){
        std::vector<std::string> names;for(int j:raised)names.push_back(st::kKnobs[j].label);
        status_="Also turned on: "+join(names,", ")+". Save to apply.";
      }
    }else status_="This option is unavailable. Check its requirements in Help & support.";
  }else if(type=="preset"){
    int p=get<int>(o,"value",-1);if(p<0||p>=st::kPresetCount){status_="Preset unavailable. Choose another preset.";return state_json();}
    preview_kind_="preset";preview_preset_=p;preview_values_=st::preset_values_for(p,state_.caps);changes_.clear();blocked_.clear();
    for(int i=0;i<st::kKnobCount;++i){if(!shown(st::kKnobs[i])||std::string(st::kKnobs[i].category)=="Support")preview_values_[i]=state_.values[i];if(preview_values_[i]!=state_.values[i])changes_.push_back(change_line(i,state_.values[i],preview_values_[i]));}
  }else if(type=="experimental"){
    bool on=get<bool>(o,"value");
    if(dev)experimental=on;
    else if(on!=experimental){
      // Release opt-in: shown options start from their stored value; hiding restores Off (qualified).
      experimental=on;
      for(int i=0;i<st::kKnobCount;++i)if(st::opt_in_knob(st::kKnobs[i])){
        if(on){if(state_.values[i]<0)state_.values[i]=0;if(saved_[i]<0)saved_[i]=0;}
        else state_.values[i]=0;
      }
      status_=on?"Experimental options shown. Save to keep them.":"Experimental options hidden and reset to Off.";
    }
  }
  else if(type=="discard"){ab_selection_.pending=false;state_.values=saved_;state_.preset=saved_preset_;state_.shortcuts=state_.loaded_shortcuts;state_.dlss5_runtime_path=state_.loaded_dlss5_runtime_path;refresh_runtime();if(!dev)experimental=saved_experimental_;status_="Changes discarded.";}
  else if(type=="save"){pending_play_=false;save_preview(get<std::string>(o,"policy")=="enable");}
  else if(type=="play"){
    if(!can_play_)status_="Choose the game folder or close the running game to enable Play.";
    else{
      pending_play_=true;save_preview(true);
      if(blocked_.empty()&&changes_.empty())return handle("{\"type\":\"confirm\"}");
    }
  }else if(type=="report"){report_visible_=true;report_hint_=hooks_.reportHint?hooks_.reportHint():"";}
  else if(type=="closeReport"){report_visible_=false;}
  else if(type=="cancelReport"){status_="Report cancelled.";}
  else if(type=="browseGame"){if(hooks_.browseGame)game_path_=hooks_.browseGame();}
  else if(type=="browseDlss5Runtime"){
    if(hooks_.browseDlss5Runtime){state_.dlss5_runtime_path=hooks_.browseDlss5Runtime();refresh_runtime();status_="Runtime file selected. Save, then restart the game to use DLSS 5.";}
  }
  else if(type=="clearDlss5Runtime"){state_.dlss5_runtime_path.clear();refresh_runtime();status_="DLSS 5 will look in the game folder. Save to keep this change.";}
  else if(type=="recheck"){if(hooks_.recheck)set_compatibility(hooks_.recheck());}
  else if(type=="notices"){if(hooks_.notices)hooks_.notices();}
  else if(type=="prep"){
    // Preparation card: launcher-only settings, applied at once (no Save, not counted as unsaved).
    const auto action=get<std::string>(o,"action");const int value=get<int>(o,"value",0);
    if(!hooks_.prepare||!hooks_.prepare(action,value))status_="Preparation is unavailable. Reopen the launcher.";
  }
  else if(type=="adapter"){
    int selected=get<int>(o,"value",-1);if(selected>=0&&size_t(selected)<inspection_.adapters.size())set_compatibility(inspection_,selected);else status_="Choose an available GPU.";
  }
  else if(type=="prepareReport"){
    report_visible_=true;report_={};if(hooks_.prepareReport)report_=hooks_.prepareReport(get<std::string>(o,"issue"),get<bool>(o,"logs",true),get<bool>(o,"settings",true),get<bool>(o,"system",true),get<bool>(o,"save",false));else report_.message="Reports are unavailable. Reopen the launcher.";
  }else if(type=="exportReport"){
    if(report_.files.empty()||report_.status==diagnostics::Status::Cancelled||report_.status==diagnostics::Status::Failed)status_="Preview the report before saving.";
    else{std::string error;if(hooks_.exportReport&&hooks_.exportReport(report_,error))status_=error.empty()?"Report saved on your PC.":error;else status_=error.empty()?"Report not saved. Preview kept; retry or choose another folder.":error;}
  }else status_="Launcher request failed. Reopen the launcher.";
  return state_json();
}
std::string Bridge::state_json() const {
  auto preset_values=st::preset_values_for(state_.preset,state_.caps);bool custom=false;
  for(int i=0;i<st::kKnobCount;++i)if(shown(st::kKnobs[i])&&std::string(st::kKnobs[i].category)!="Support"&&state_.values[i]!=preset_values[i])custom=true;
  std::string out="{\"type\":\"state\",\"preset\":"+std::to_string(state_.preset)+",\"presetLabel\":"+q(st::kPresetLabel[state_.preset])+",\"recommendedPreset\":"+std::to_string(state_.suggested)+",\"dirty\":"+std::to_string(dirty())+",\"unsavedCount\":"+std::to_string(dirty())+",\"restartNeeded\":"+b(restart_count_>0)+",\"restartCount\":"+std::to_string(restart_count_)+",\"restartBanner\":"+q(ui_state::restart_banner(restart_count_))+",\"experimental\":"+b(experimental)+",\"developer\":"+b(dev)+",\"status\":"+q(status_)+",\"canPlay\":"+b(can_play_)+",\"settings\":[";
  bool first=true;for(int i=0;i<st::kKnobCount;++i){auto& k=st::kKnobs[i];if(!shown(k))continue;if(!first)out+=",";first=false;
    int value=state_.values[i];std::string maturity=k.experimental?"experimental":st::choice_maturity(k,value);
    // Opted-in release options use release copy and live on their own Experimental page.
    const bool opted=!dev&&st::opt_in_knob(k),developer=k.dev_only&&!opted;
    out+="{\"id\":"+q(k.id)+",\"title\":"+q(k.label)+",\"description\":"+q(k.desc)+",\"details\":"+q(k.details)+",\"category\":"+q(opted?"Experimental":k.category)+",\"maturity\":"+q(maturity)+",\"experimental\":"+b(k.experimental)+",\"developer\":"+b(developer)+",\"value\":"+std::to_string(value)+",\"custom\":"+b(value<0)+",\"cost\":"+q((std::string(k.cost.gpu)=="Unknown"||std::string(k.cost.gpu)=="Not measured")?"Not measured":std::string(k.cost.status)+": "+k.cost.gpu)+",\"vram\":"+q(std::string(k.cost.vram)=="Unknown"?"":k.cost.vram)+",\"provenance\":"+q(std::string(k.cost.provenance)=="No qualified measurement available."?"":k.cost.provenance)+",\"apply\":"+q(st::apply_label(st::transition_apply(k,saved_[i],value),!developer))+",\"applyNote\":"+q(developer?k.apply_note:"")+",\"kind\":"+q(k.kind==st::Kind::Choice?"choice":"slider")+",\"choices\":[";
    bool fc=true;for(int c=0;c<st::choice_count(k);++c){if(!st::choice_visible(k,c,dev,opt_in()))continue;if(!fc)out+=",";fc=false;bool available=st::choice_available(k,c,dev,experimental,state_.caps,opt_in());
      std::string reason,requirements;
      if(developer){
        // Developer cards keep their original wording.
        auto need=st::unmet_need(k,c,state_.values);if(need.empty())need=st::unmet_key_need(k,c,effective());
        reason=!st::choice_supported(k,c,state_.caps)?"Graphics compatibility unavailable: "+capability_requirements(k.choices[c].caps & ~state_.caps,false):!available?"Enable Experimental to select":need;
        requirements=capability_requirements(k.choices[c].caps,false);
        for(auto& n:k.choices[c].need)if(n.knob){int j=st::knob_index(n.knob);if(j>=0){if(!requirements.empty())requirements+="; ";requirements+=std::string("needs ")+st::kKnobs[j].label+" "+label(j,n.min_choice);}}
        for(auto& n:k.required_on)if(n.key&&c>=n.min_choice){if(!requirements.empty())requirements+="; ";requirements+=std::string("needs ")+n.label;}
      }else{reason=release_reason(k,c,state_.caps,state_.values);requirements=release_requirements(k,c);}
      out+="{\"value\":"+std::to_string(c)+",\"label\":"+q(k.choices[c].label)+",\"apply\":"+q(st::apply_label(st::transition_apply(k,saved_[i],c),!developer))+",\"enabled\":"+b(available)+",\"reason\":"+q(reason)+",\"requirements\":"+q(requirements)+",\"maturity\":"+q(st::choice_maturity(k,c))+"}";}
    out+="],\"min\":"+std::to_string(k.slider.min)+",\"max\":"+std::to_string(k.slider.max)+",\"step\":"+std::to_string(k.slider.step)+",\"hasOff\":"+b(k.slider.has_off)+",\"unit\":"+q(k.slider.unit?k.slider.unit:"")+"}";
  }
  std::string gpu=inspection_.adapters.size()>1?"GPU not chosen":"GPU not detected",vram="Not detected",driver="Driver not detected";if(selected_>=0&&size_t(selected_)<inspection_.adapters.size()){auto& a=inspection_.adapters[selected_];gpu=a.gpu.name;driver=a.driver_version.empty()?"Driver not detected":a.driver_version;if(a.dedicated_bytes)vram=std::to_string(a.dedicated_bytes>>30)+" GiB";}
  auto fg=st::lookup(effective(),"fg");std::string fg_label=fg=="0"?"Off":fg=="2"?"2x":fg=="3"?"3x":fg=="4"?"4x":"Unknown";
  std::vector<std::string> warnings;
  if(compatibility_.hardware!=CheckState::Supported&&!compatibility_.hardware_text.empty())warnings.push_back(compatibility_.hardware_text);
  if(compatibility_.driver!=CheckState::Supported&&!compatibility_.driver_text.empty())warnings.push_back(compatibility_.driver_text);
  out+="],\"gamePath\":"+q(game_path_)+",\"compatibility\":{\"gpu\":"+q(gpu)+",\"vram\":"+q(vram)+",\"memory\":"+q(compatibility_.memory_text)+",\"driver\":"+q(driver)+",\"warnings\":"+strings(warnings)+",\"details\":"+strings({compatibility_.hardware_text,compatibility_.driver_text,compatibility_.memory_text,compatibility_.adapter_text})+",\"selected\":"+std::to_string(selected_)+",\"adapters\":[";
  for(size_t i=0;i<inspection_.adapters.size();++i){if(i)out+=",";out+="{\"value\":"+std::to_string(i)+",\"label\":"+q(inspection_.adapters[i].gpu.name)+"}";}out+="]}";
  out+=",\"presetDescriptions\":[";for(int p=0;p<st::kPresetCount;++p){if(p)out+=",";out+=q(st::kPresetDesc[p]);}out+="]";
  out+=",\"version\":"+q(version_)+",\"modStatus\":"+q(mod_status_);
  out+=",\"shortcuts\":{";for(size_t i=0;i<kShortcutCount;++i){if(i)out+=",";out+=q(bg3::hotkeys::ids[i])+":"+q(state_.shortcuts[i]);}out+="}";
  // Defaults for the Hotkeys page's "Reset" line (the bridge resets; the page only shows them).
  out+=",\"shortcutDefaults\":{";for(size_t i=0;i<kShortcutCount;++i){if(i)out+=",";out+=q(bg3::hotkeys::ids[i])+":"+q(bg3::hotkeys::defaults[i]);}out+="}";
  out+=",\"dlss5RuntimePath\":"+q(state_.dlss5_runtime_path)+",\"dlss5RuntimeStatus\":"+q(state_.dlss5_runtime_status.empty()?"Runtime status is checked when the game folder or selected file changes. NVIDIA-signed runtimes are preferred.":state_.dlss5_runtime_status);
  out+=std::string(",\"custom\":")+b(custom)+",\"frameGeneration\":"+q(fg_label)+",\"frameGenerationNote\":\"Generated frames do not increase the rendered frame rate.\"";
  if(hooks_.preparation){const auto prep=hooks_.preparation();if(!prep.empty())out+=",\"preparation\":"+prep;}
  out+=",\"preview\":{\"kind\":"+q(preview_kind_)+",\"presetLabel\":"+q(preview_preset_>=0?st::kPresetLabel[preview_preset_]:st::kPresetLabel[state_.preset])+",\"changes\":"+strings(changes_)+",\"blocked\":"+strings(blocked_)+"}";
  out+=std::string(",\"report\":{\"visible\":")+b(report_visible_)+",\"privacy\":"+q(diagnostics::privacy_text)+",\"loggingHint\":"+q(report_hint_)+",\"bytes\":"+std::to_string(report_.bytes)+",\"status\":"+std::to_string(int(report_.status))+",\"message\":"+q(report_.message)+",\"files\":[";
  first=true;for(auto& f:report_.files){if(!first)out+=",";first=false;out+="{\"name\":"+q(f.name)+",\"text\":"+q(f.binary?std::string():f.text)+",\"note\":"+q(f.note)+",\"size\":"+std::to_string(f.text.size())+(f.binary?",\"binary\":true":"")+"}";}
  return out+"]}}";
}
}


