#include "web_bridge.h"
#include "version.h"
#include "ui_state.h"
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
std::string label(int i,int value){auto& k=st::kKnobs[i];return value<0?"Custom value preserved":k.kind==st::Kind::Choice?std::string(k.choices[value].label):std::to_string(value);}
}
Bridge::Bridge(State& state,Hooks hooks):state_(state),hooks_(std::move(hooks)),saved_(state.values),saved_preset_(state.preset) {
  auto m=effective();dev=st::same_value(st::lookup(m,"dev"),"1");
  for(int i=0;i<st::kKnobCount;++i){auto& k=st::kKnobs[i];if(!st::knob_visible(k,dev)||(k.kind==st::Kind::Choice&&!st::choice_visible(k,state_.values[i],dev)))state_.values[i]=-1;}
  saved_=state_.values;
}
st::Map Bridge::effective() const {st::Map m;st::apply_layer(m,state_.base);st::apply_layer(m,state_.user);for(int i=0;i<st::kKnobCount;++i){st::KVList kv;st::knob_kvs(st::kKnobs[i],state_.values[i],kv);for(auto& v:kv)m[v.first]=v.second;}return m;}
int Bridge::dirty() const {return ui_state::dirty(state_.values,saved_,state_.preset,saved_preset_,L"",L"");}
bool Bridge::is_collection_intent(const std::string& message){Object o;return Parser{message}.parse(o)&&get<std::string>(o,"type")=="prepareReport";}
bool Bridge::is_export_intent(const std::string& message){Object o;return Parser{message}.parse(o)&&get<std::string>(o,"type")=="exportReport";}
bool Bridge::is_cancel_collection_intent(const std::string& message){Object o;if(!Parser{message}.parse(o))return false;auto type=get<std::string>(o,"type");return type=="closeReport"||type=="cancelReport";}
void Bridge::set_compatibility(GraphicsInspection p,int selected){preview_kind_.clear();inspection_=std::move(p);selected_=selected;if(selected_<0&&inspection_.adapters.size()==1)selected_=0;compatibility_=assess_compatibility(inspection_,selected_);state_.caps=compatibility_.caps;state_.suggested=compatibility_.model_preset;state_.have_gpu=selected_>=0&&size_t(selected_)<inspection_.adapters.size();if(state_.have_gpu)state_.gpu=inspection_.adapters[selected_].gpu;}
bool Bridge::save_preview(bool enable_required){
  auto p=st::preview_dependencies(state_.values,effective(),state_.caps,enable_required,dev,experimental);
  preview_kind_="save";preview_values_=p.values;preview_preset_=state_.preset;preview_keys_=p.required_keys;blocked_=p.blocked;changes_.clear();
  for(int i:p.changed)changes_.push_back(std::string(st::kKnobs[i].label)+": "+label(i,state_.values[i])+" -> "+label(i,p.values[i]));
  for(auto& kv:p.required_keys){std::string name="Startup prerequisite";for(auto& k:st::kKnobs)for(auto& n:k.required_on)if(n.key&&kv.first==n.key)name=n.label;changes_.push_back(name+": enable at next launch");}
  return blocked_.empty();
}
std::string Bridge::handle(const std::string& message){
  Object o;if(!Parser{message}.parse(o)){status_="Invalid launcher message.";return state_json();}
  auto type=get<std::string>(o,"type");
  if(type=="init"||type=="state")return state_json();
  if(type=="cancel"){preview_kind_.clear();changes_.clear();blocked_.clear();return state_json();}
  if(type=="confirm"){
    if(preview_kind_=="preset"){state_.values=preview_values_;state_.preset=preview_preset_;preview_kind_.clear();}
    else if(preview_kind_=="save"&&blocked_.empty()){
      State candidate=state_;candidate.values=preview_values_;std::string error;
      if(hooks_.save&&hooks_.save(candidate,preview_keys_,error)){state_=std::move(candidate);saved_=state_.values;saved_preset_=state_.preset;preview_kind_.clear();status_=restart_count_?"Saved; restart required.":"Saved for next launch. Live changes have no runtime acknowledgement.";}
      else status_=error.empty()?"Settings could not be saved. Your edits are kept.":error;
    }return state_json();
  }
  // An edit invalidates the exact preview to prevent stale confirmations.
  if(type=="set"||type=="preset"||type=="discard"||type=="experimental")preview_kind_.clear();
  if(type=="set"){
    int i=st::knob_index(get<std::string>(o,"id"));int value=get<int>(o,"value",INT_MIN);
    if(i<0||!st::knob_visible(st::kKnobs[i],dev)){status_="Setting is unavailable.";return state_json();}
    auto& k=st::kKnobs[i];bool valid=k.kind==st::Kind::Choice?st::choice_available(k,value,dev,experimental,state_.caps):((k.slider.has_off&&value==0)||(value>=k.slider.min&&value<=k.slider.max&&(value-k.slider.min)%k.slider.step==0));
    if(k.experimental&&!experimental)valid=false;if(valid){state_.values[i]=value;status_.clear();}else status_="This choice is unavailable. Review compatibility or Experimental.";
  }else if(type=="preset"){
    int p=get<int>(o,"value",-1);if(p<0||p>=st::kPresetCount){status_="Unknown preset.";return state_json();}
    preview_kind_="preset";preview_preset_=p;preview_values_=st::preset_values_for(p,state_.caps);changes_.clear();blocked_.clear();
    for(int i=0;i<st::kKnobCount;++i){if(state_.values[i]<0||!st::knob_visible(st::kKnobs[i],dev))preview_values_[i]=state_.values[i];if(preview_values_[i]!=state_.values[i])changes_.push_back(std::string(st::kKnobs[i].label)+": "+label(i,state_.values[i])+" -> "+label(i,preview_values_[i]));}
  }else if(type=="experimental")experimental=get<bool>(o,"value");
  else if(type=="discard"){state_.values=saved_;state_.preset=saved_preset_;status_="Edits discarded.";}
  else if(type=="save")save_preview(get<std::string>(o,"policy")=="enable");
  else if(type=="play"){
    if(!can_play_)status_="Play is unavailable. Check game location and whether the game is running.";
    else if(dirty())status_="Save or discard your edits before playing.";
    else{std::string error;if(!hooks_.play||!hooks_.play(error))status_=error.empty()?"Game could not start.":error;else{status_="Game started.";can_play_=false;}}
  }else if(type=="report"){report_visible_=true;}
  else if(type=="closeReport"){report_visible_=false;}
  else if(type=="cancelReport"){status_="Report collection cancelled. Nothing was uploaded.";}
  else if(type=="browseGame"){if(hooks_.browseGame)game_path_=hooks_.browseGame();}
  else if(type=="recheck"){if(hooks_.recheck)set_compatibility(hooks_.recheck());}
  else if(type=="adapter"){
    int selected=get<int>(o,"value",-1);if(selected>=0&&size_t(selected)<inspection_.adapters.size())set_compatibility(inspection_,selected);else status_="Choose an available adapter.";
  }
  else if(type=="prepareReport"){
    report_visible_=true;report_={};if(hooks_.prepareReport)report_=hooks_.prepareReport(get<std::string>(o,"issue"),get<bool>(o,"logs",true),get<bool>(o,"settings",true),get<bool>(o,"system",true));else report_.message="Report collector unavailable.";
  }else if(type=="exportReport"){
    if(report_.files.empty()||report_.status==diagnostics::Status::Cancelled||report_.status==diagnostics::Status::Failed)status_="Prepare and review a report first.";
    else{std::string error;if(hooks_.exportReport&&hooks_.exportReport(report_,error))status_=error.empty()?"Report saved locally. Nothing was uploaded.":error;else status_=error.empty()?"Report was not saved. Preview is retained; choose another folder or retry.":error;}
  }else status_="Unknown launcher intent.";
  return state_json();
}
std::string Bridge::state_json() const {
  auto preset_values=st::preset_values_for(state_.preset,state_.caps);bool custom=false;
  for(int i=0;i<st::kKnobCount;++i)if(st::knob_visible(st::kKnobs[i],dev)&&state_.values[i]!=preset_values[i])custom=true;
  std::string out="{\"type\":\"state\",\"preset\":"+std::to_string(state_.preset)+",\"presetLabel\":"+q(st::kPresetLabel[state_.preset])+",\"recommendedPreset\":"+std::to_string(state_.suggested)+",\"dirty\":"+std::to_string(dirty())+",\"unsavedCount\":"+std::to_string(dirty())+",\"restartNeeded\":"+b(restart_count_>0)+",\"restartCount\":"+std::to_string(restart_count_)+",\"restartBanner\":"+q(ui_state::restart_banner(restart_count_))+",\"experimental\":"+b(experimental)+",\"developer\":"+b(dev)+",\"status\":"+q(status_)+",\"canPlay\":"+b(can_play_)+",\"settings\":[";
  bool first=true;for(int i=0;i<st::kKnobCount;++i){auto& k=st::kKnobs[i];if(!st::knob_visible(k,dev))continue;if(!first)out+=",";first=false;
    int value=state_.values[i];std::string maturity=k.experimental?"experimental":st::choice_maturity(k,value);
    out+="{\"id\":"+q(k.id)+",\"title\":"+q(k.label)+",\"description\":"+q(k.desc)+",\"category\":"+q(k.category)+",\"maturity\":"+q(maturity)+",\"experimental\":"+b(k.experimental)+",\"developer\":"+b(k.dev_only)+",\"value\":"+std::to_string(value)+",\"custom\":"+b(value<0)+",\"cost\":"+q(std::string(k.cost.gpu)=="Unknown"?"":std::string(k.cost.status)+": "+k.cost.gpu)+",\"vram\":"+q(std::string(k.cost.vram)=="Unknown"?"":k.cost.vram)+",\"provenance\":"+q(k.cost.provenance)+",\"apply\":"+q(st::apply_label(k.apply))+",\"applyNote\":"+q(k.apply_note)+",\"kind\":"+q(k.kind==st::Kind::Choice?"choice":"slider")+",\"choices\":[";
    bool fc=true;for(int c=0;c<st::choice_count(k);++c){if(!st::choice_visible(k,c,dev))continue;if(!fc)out+=",";fc=false;bool available=st::choice_available(k,c,dev,experimental,state_.caps);auto need=st::unmet_need(k,c,state_.values);if(need.empty())need=st::unmet_key_need(k,c,effective());std::string reason=!st::choice_supported(k,c,state_.caps)?"Graphics compatibility unavailable":!available?"Enable Experimental to select":need;
      out+="{\"value\":"+std::to_string(c)+",\"label\":"+q(k.choices[c].label)+",\"apply\":"+q(st::apply_label(k.choices[c].apply))+",\"enabled\":"+b(available)+",\"reason\":"+q(reason)+",\"maturity\":"+q(st::choice_maturity(k,c))+"}";}
    out+="],\"min\":"+std::to_string(k.slider.min)+",\"max\":"+std::to_string(k.slider.max)+",\"step\":"+std::to_string(k.slider.step)+",\"hasOff\":"+b(k.slider.has_off)+",\"unit\":"+q(k.slider.unit?k.slider.unit:"")+"}";
  }
  std::string gpu="Unknown adapter",vram="Unknown",driver="Unknown";if(selected_>=0&&size_t(selected_)<inspection_.adapters.size()){auto& a=inspection_.adapters[selected_];gpu=a.gpu.name;driver=a.driver_version.empty()?"Unknown":a.driver_version;if(a.dedicated_bytes)vram=std::to_string(a.dedicated_bytes>>30)+" GiB";}
  auto fg=st::lookup(effective(),"fg");std::string fg_label=fg=="0"?"Off":fg=="2"?"2x":fg=="3"?"3x":fg=="4"?"4x":"Unknown";
  std::vector<std::string> warnings;
  if(compatibility_.hardware!=CheckState::Supported&&!compatibility_.hardware_text.empty())warnings.push_back(compatibility_.hardware_text);
  if(compatibility_.driver!=CheckState::Supported&&!compatibility_.driver_text.empty())warnings.push_back(compatibility_.driver_text);
  out+="],\"gamePath\":"+q(game_path_)+",\"compatibility\":{\"gpu\":"+q(gpu)+",\"vram\":"+q(vram)+",\"memory\":"+q(compatibility_.memory_text)+",\"driver\":"+q(driver)+",\"warnings\":"+strings(warnings)+",\"details\":"+strings({compatibility_.hardware_text,compatibility_.driver_text,compatibility_.memory_text,compatibility_.adapter_text})+",\"selected\":"+std::to_string(selected_)+",\"adapters\":[";
  for(size_t i=0;i<inspection_.adapters.size();++i){if(i)out+=",";out+="{\"value\":"+std::to_string(i)+",\"label\":"+q(inspection_.adapters[i].gpu.name)+"}";}out+="]}";
  out+=",\"version\":"+q(BG3RTX_PRODUCT_VERSION)+",\"modStatus\":"+q(mod_status_);
  out+=std::string(",\"custom\":")+b(custom)+",\"frameGeneration\":"+q(fg_label)+",\"frameGenerationNote\":\"Displayed frames include generated frames; this is not measured rendering performance.\"";
  out+=",\"preview\":{\"kind\":"+q(preview_kind_)+",\"changes\":"+strings(changes_)+",\"blocked\":"+strings(blocked_)+"}";
  out+=std::string(",\"report\":{\"visible\":")+b(report_visible_)+",\"privacy\":"+q(diagnostics::privacy_text)+",\"bytes\":"+std::to_string(report_.bytes)+",\"status\":"+std::to_string(int(report_.status))+",\"message\":"+q(report_.message)+",\"files\":[";
  first=true;for(auto& f:report_.files){if(!first)out+=",";first=false;out+="{\"name\":"+q(f.name)+",\"text\":"+q(f.text)+",\"note\":"+q(f.note)+",\"size\":"+std::to_string(f.text.size())+"}";}
  return out+"]}}";
}
}


