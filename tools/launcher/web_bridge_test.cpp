// CPU-only bridge fixture: injected effects never launch or write real settings.
#include "web_bridge.h"
#include "ui_state.h"
#include "session_logging.h"
#include <cstdio>
namespace st=bg3::settings;
int main(int argc,char** argv){
  // --json [experimental|needs]: renderer fixtures (release opt-in shown; unmet requirements).
  if((argc==2||argc==3)&&std::string(argv[1])=="--json"){
    const std::string view=argc==3?argv[2]:"";
    launcher::State sample;sample.values=st::preset_values(st::kHigh);sample.preset=st::kHigh;
    if(view=="needs")sample.values[st::knob_index("rt_geometry")]=0;
    launcher::web::Bridge bridge(sample);bridge.set_status("Line\n\"quoted\"\tcontrol\r");
    if(view=="experimental")bridge.handle("{\"type\":\"experimental\",\"value\":true}");
    if(!view.empty())bridge.set_status("");
    std::printf("%s",bridge.state_json().c_str());return 0;
  }
  int checks=0,failed=0,saves=0,plays=0,prepares=0,exports=0;
  auto check_line=[&](bool ok,int line){++checks;if(!ok){++failed;std::printf("FAIL check %d at line %d\n",checks,line);}};
#define check(ok) check_line((ok),__LINE__)
  // Play persists the candidate before launch; failed saves and cancelled
  // dependency reviews must never reach the launch callback.
  {
    launcher::State ps; ps.caps=~0u; ps.base=st::code_defaults();
    ps.base["dev"]="0"; ps.values=st::preset_values(st::kLow);
    bool fail_save=false; std::string effects;
    launcher::web::Hooks ph;
    ph.save=[&](launcher::State&,const st::KVList&,std::string&){effects+='S';return !fail_save;};
    ph.play=[&](std::string&){effects+='P';return true;};
    launcher::web::Bridge pb(ps,ph); pb.set_can_play(true);
    pb.handle("{\"type\":\"set\",\"id\":\"rt_intensity\",\"value\":90}");
    pb.handle("{\"type\":\"play\"}");
    check(effects=="SP"&&pb.dirty()==0);
    pb.set_can_play(true); effects.clear(); fail_save=true;
    pb.handle("{\"type\":\"set\",\"id\":\"rt_intensity\",\"value\":80}");
    pb.handle("{\"type\":\"play\"}");
    check(effects=="S"&&pb.dirty()>0);
    check(pb.state_json().find("Settings not saved. Your edits are kept.")!=std::string::npos);
    effects.clear(); fail_save=false;
    pb.handle("{\"type\":\"set\",\"id\":\"gi\",\"value\":2}");
    pb.handle("{\"type\":\"play\"}"); check(effects.empty());
    pb.handle("{\"type\":\"cancel\"}"); pb.handle("{\"type\":\"confirm\"}");
    check(effects.empty()&&pb.dirty()>0);
    pb.handle("{\"type\":\"play\"}"); pb.handle("{\"type\":\"confirm\"}");
    check(effects=="SP"&&pb.dirty()==0);
  }
  {
    launcher::State runtime;runtime.base={{"dev","0"}};runtime.values=st::preset_values(st::kHigh);
    launcher::web::Hooks rh;rh.browseDlss5Runtime=[](){return std::string("C:/User runtime/nvngx_dlssnr.dll");};
    rh.save=[](launcher::State&,const st::KVList&,std::string&){return true;};
    launcher::web::Bridge rb(runtime,rh);
    rb.handle("{\"type\":\"browseDlss5Runtime\"}");
    check(runtime.dlss5_runtime_path=="C:/User runtime/nvngx_dlssnr.dll" && rb.dirty()==1);
    check(rb.state_json().find("C:/User runtime/nvngx_dlssnr.dll")!=std::string::npos);
    rb.handle("{\"type\":\"discard\"}");check(runtime.dlss5_runtime_path.empty()&&rb.dirty()==0);
    rb.handle("{\"type\":\"browseDlss5Runtime\"}");rb.handle("{\"type\":\"save\"}");rb.handle("{\"type\":\"confirm\"}");
    check(runtime.loaded_dlss5_runtime_path==runtime.dlss5_runtime_path&&rb.dirty()==0);
    rb.handle("{\"type\":\"clearDlss5Runtime\"}");check(runtime.dlss5_runtime_path.empty()&&rb.dirty()==1);
    rb.handle("{\"type\":\"discard\"}");check(!runtime.dlss5_runtime_path.empty()&&rb.dirty()==0);
  }
  {
    launcher::State ks;ks.base={{"dev","0"}};ks.values=st::preset_values(st::kHigh);
    launcher::web::Hooks kh;bool saved=false;
    kh.save=[&](launcher::State& c,const st::KVList&,std::string&){saved=c.shortcuts[1]=="Alt+N";return true;};
    launcher::web::Bridge kb(ks,kh);
    check(kb.state_json().find("\"category\":\"DLSS 5\"")!=std::string::npos);
    check(st::choice_available(*st::find_knob("dlss5_nr"),1,false,false,~0u));
    kb.handle(R"({"type":"set","id":"dlss5_nr","value":1})");
    check(ks.values[st::knob_index("dlss5_nr")]==1&&!kb.experimental);
    kb.handle(R"({"type":"discard"})");check(kb.dirty()==0);

    kb.handle(R"({"type":"shortcut","id":"hotkey_dlss5","value":"Ctrl+Shift+F9"})");check(kb.dirty()==0);
    kb.handle(R"({"type":"shortcut","id":"hotkey_dlss5","value":"Ctrl+Shift+F10"})");check(kb.dirty()==0);
    kb.handle(R"({"type":"shortcut","id":"hotkey_dlss5","value":"Alt+N"})");check(kb.dirty()==1);
    kb.handle(R"({"type":"save"})");kb.handle(R"({"type":"confirm"})");check(saved&&kb.dirty()==0);
    kb.handle(R"({"type":"resetShortcut","id":"hotkey_dlss5"})");check(ks.shortcuts[1]=="Ctrl+Shift+F8");
    kb.handle(R"({"type":"discard"})");check(ks.shortcuts[1]=="Alt+N"&&kb.dirty()==0);
    kb.handle(R"({"type":"shortcut","id":"hotkey_rt_toggle","value":"None"})");check(ks.shortcuts[0]=="None");
  }
  check(launcher::web::Bridge::is_collection_intent("{\"type\":\"prepareReport\"}"));
  check(launcher::web::Bridge::is_export_intent("{\"type\":\"exportReport\"}"));
  check(!launcher::web::Bridge::is_export_intent("{\"type\":\"prepareReport\",\"issue\":\"exportReport\"}"));
  check(!launcher::web::Bridge::is_collection_intent("{\"type\":\"exportReport\",\"issue\":\"prepareReport\"}"));
  check(!launcher::web::Bridge::is_collection_intent("{\"type\":\"prepareReport\",\"type\":\"exportReport\"}"));
  check(launcher::web::Bridge::is_cancel_collection_intent("{\"type\":\"closeReport\"}"));
  check(launcher::web::Bridge::is_cancel_collection_intent("{\"type\":\"cancelReport\"}"));
  check(!launcher::web::Bridge::is_cancel_collection_intent("{\"type\":\"exportReport\",\"issue\":\"closeReport\"}"));
  launcher::State s;s.caps=~0u;s.preset=st::kLow;s.values=st::preset_values(st::kLow);s.loaded=s.values;s.base={{"dev","0"}};s.user={{"unmanaged","retained"}};
  launcher::web::Hooks h;
  h.save=[&](launcher::State& candidate,const st::KVList&,std::string&){++saves;check(candidate.user.at("unmanaged")=="retained");return true;};
  h.play=[&](std::string&){++plays;return true;};
  h.prepareReport=[&](const std::string& issue,bool logs,bool settings,bool system){++prepares;check(issue=="Line\n\"quoted\" \xe2\x98\x80");check(!logs&&settings&&system);launcher::diagnostics::Snapshot r;r.status=launcher::diagnostics::Status::Complete;r.files={{"ISSUE.txt",issue,""}};r.bytes=issue.size();return r;};
  h.exportReport=[&](const launcher::diagnostics::Snapshot& r,std::string&){++exports;check(r.files[0].text=="Line\n\"quoted\" \xe2\x98\x80");return true;};
  // Retired controls stay absent even with developer/experimental access.
  {
    launcher::State retired; retired.caps=~0u; retired.base={{"dev","1"}};
    retired.values=st::preset_values(st::kUltra);
    launcher::web::Bridge rb(retired); rb.experimental=true;
    check(rb.dev);
    for (const char* id : {"rr_hair_resolve", "exp_cascade_static_only", "perf_hitmat_lists", "perf_hook_state", "perf_submit_batch"}) {
      check(rb.state_json().find(id)==std::string::npos);
      const auto before=retired.values;
      rb.handle(std::string("{\"type\":\"set\",\"id\":\"")+id+"\",\"value\":1}");
      check(retired.values==before);
      check(rb.dirty()==0);
    }
  }
  launcher::web::Bridge bridge(s,h);
  int visible=0;
  for(const auto& k:st::kKnobs){
    const bool shown=st::knob_visible(k,false);visible+=shown;
    check((bridge.state_json().find("\"id\":\""+std::string(k.id)+"\"")!=std::string::npos)==shown);
    if(!shown){const auto before=s.values;bridge.handle("{\"type\":\"set\",\"id\":\""+std::string(k.id)+"\",\"value\":1}");check(s.values==before);}
    if(shown)for(int c=0;c<st::choice_count(k);++c)if(!st::choice_visible(k,c,false)){
      const auto before=s.values;bridge.handle("{\"type\":\"set\",\"id\":\""+std::string(k.id)+"\",\"value\":"+std::to_string(c)+"}");check(s.values==before);
    }
  }
  check(visible==17);
  // Support edits stay independent of the rendering preset and last-session evidence.
  {
    launcher::State ls;ls.caps=~0u;ls.base={{"dev","0"}};ls.values=st::preset_values(st::kHigh);ls.preset=st::kHigh;
    std::string last_hint="last session was quiet";launcher::web::Hooks lh;lh.reportHint=[&]{return last_hint;};
    launcher::web::Bridge lb(ls,lh);
    lb.handle("{\"type\":\"set\",\"id\":\"detailed_logging\",\"value\":1}");
    check(lb.state_json().find("\"custom\":false")!=std::string::npos);
    check(lb.dirty()==1);
    lb.handle("{\"type\":\"preset\",\"value\":2}");lb.handle("{\"type\":\"confirm\"}");
    check(ls.values[st::knob_index("detailed_logging")]==1);
    auto json=lb.handle("{\"type\":\"report\"}");check(json.find("\"loggingHint\":\"last session was quiet\"")!=std::string::npos);
    last_hint.clear();lb.handle("{\"type\":\"set\",\"id\":\"detailed_logging\",\"value\":0}");
    json=lb.handle("{\"type\":\"report\"}");check(json.find("\"loggingHint\":\"\"")!=std::string::npos);
  }
  check(launcher::session_logging::enabled("00:00 [cfg] detailed logging: 0\n")==0);
  check(launcher::session_logging::enabled("00:00 [cfg] detailed logging: 1\r\n")==1);
  check(launcher::session_logging::enabled("[cfg] detailed logging: 10\n")==-1);
  check(launcher::session_logging::enabled("old log without marker")==-1);
  check(launcher::session_logging::enabled("[cfg] detailed logging: 0\n[cfg] detailed logging: 1\n")==1);
  // Release opt-in: the switch shows exactly settings::kOptInKnobs on their own page,
  // is saved with the settings and Off restores the qualified Off values.
  {
    launcher::State os;os.caps=~0u;os.preset=st::kHigh;os.values=st::preset_values(st::kHigh);os.base={{"dev","0"}};
    os.user_text="unmanaged=retained\nexperimental_options=0\n";std::string saved_text;int os_saves=0;
    launcher::web::Hooks oh;oh.save=[&](launcher::State& candidate,const st::KVList&,std::string&){++os_saves;saved_text=candidate.user_text;return true;};
    launcher::web::Bridge ob(os,oh);check(!ob.experimental);
    check(ob.state_json().find("\"id\":\"water_surface_fresnel\"")==std::string::npos);
    ob.handle("{\"type\":\"set\",\"id\":\"exp_hair_fibre_gi\",\"value\":1}");check(os.values[st::knob_index("exp_hair_fibre_gi")]<=0);
    ob.handle("{\"type\":\"experimental\",\"value\":true}");check(ob.experimental&&ob.dirty()==1);
    auto shown=ob.state_json();
    for(const auto& k:st::kKnobs){
      bool listed=shown.find("\"id\":\""+std::string(k.id)+"\"")!=std::string::npos;
      check(listed==(st::knob_visible(k,false)||st::opt_in_knob(k)));
    }
    auto fresnel=shown.substr(shown.find("\"id\":\"water_surface_fresnel\""));
    check(fresnel.find("\"category\":\"Experimental\"")<fresnel.find("\"choices\"")&&fresnel.find("\"developer\":false")<fresnel.find("\"choices\""));
    check(shown.find("exp_compact_hit_state")==std::string::npos&&shown.find("exp_alpha_metadata_cache")==std::string::npos&&shown.find("exp_water_empty_tiles")==std::string::npos);
    os.values[st::knob_index("exp_water")]=1;
    ob.handle("{\"type\":\"set\",\"id\":\"water_surface_fresnel\",\"value\":1}");check(os.values[st::knob_index("water_surface_fresnel")]==1);
    ob.handle("{\"type\":\"save\",\"policy\":\"enable\"}");ob.handle("{\"type\":\"confirm\"}");
    check(os_saves==1&&ob.dirty()==0&&saved_text.find("experimental_options=1\n")!=std::string::npos&&saved_text.find("experimental_options=0")==std::string::npos&&saved_text.find("unmanaged=retained")!=std::string::npos);
    ob.handle("{\"type\":\"experimental\",\"value\":false}");check(!ob.experimental&&os.values[st::knob_index("water_surface_fresnel")]==0&&ob.dirty()==2);
    check(ob.state_json().find("\"id\":\"water_surface_fresnel\"")==std::string::npos);
    ob.handle("{\"type\":\"discard\"}");check(ob.experimental&&ob.dirty()==0);
    ob.handle("{\"type\":\"experimental\",\"value\":false}");ob.handle("{\"type\":\"save\",\"policy\":\"enable\"}");ob.handle("{\"type\":\"confirm\"}");
    check(os_saves==2&&saved_text.find("experimental_options")==std::string::npos&&ob.dirty()==0);
    // A saved opt-in is restored on the next launch.
    launcher::State again=os;again.user={{"experimental_options","1"}};again.values[st::knob_index("water_surface_fresnel")]=1;
    launcher::web::Bridge ab2(again);check(ab2.experimental&&again.values[st::knob_index("water_surface_fresnel")]==1);
    // Release requirement copy: short, unmet only, no duplicates, no met hardware.
    launcher::State rq;rq.caps=~0u;rq.values=st::preset_values(st::kHigh);rq.values[st::knob_index("rt_geometry")]=0;
    launcher::web::Bridge rb2(rq);auto copy=rb2.state_json();
    check(copy.find("\"reason\":\"Needs Ray-tracing detail: Solid surfaces\"")!=std::string::npos);
    check(copy.find("\"reason\":\"Requires")==std::string::npos&&copy.find("\"requirements\":\"Requires")==std::string::npos&&copy.find("Required startup setting")==std::string::npos&&copy.find("GPU requirement")==std::string::npos);
    check(copy.find("Sets at startup: Ray-tracing support; Sets at startup: Ray-tracing support")==std::string::npos);
    check(copy.find("\"reason\":\"Needs hardware")==std::string::npos);
  }
  s.values[st::knob_index("exp_native_coverage")]=1;
  auto coverage=bridge.state_json();auto pos=coverage.find("\"id\":\"exp_native_coverage\"");
  check(pos!=std::string::npos);
  check(coverage.substr(pos,coverage.find("\"choices\"",pos)-pos).find("\"apply\":\"Restart the game to apply\"")!=std::string::npos);
  s.values[st::knob_index("exp_native_coverage")]=0;
  check(bridge.dirty()==0);auto json=bridge.state_json();check(json.find("\"type\":\"state\"")!=std::string::npos);check(!bridge.dev);check(json.find("\"id\":\"ls_k\"")==std::string::npos);
  for(auto bad:{"", "[]", "{\"type\":\"discard\"} trailing", "{\"type\":\"discard\",\"type\":\"play\"}", "{\"type\":[]}","{\"type\":\"set\",\"value\":01}","{\"type\":\"set\",\"value\":1.2}","{\"type\":\"set\",\"value\":999999999999999}","{\"type\":\"\\uD800\"}"})check(bridge.handle(bad).find("Launcher request failed. Reopen the launcher.")!=std::string::npos);
  bridge.handle("{\"type\":\"set\",\"id\":\"rr\",\"value\":1}");check(bridge.dirty()==1);check(s.values[st::knob_index("rr")]==1);
  bridge.handle("{\"type\":\"play\"}");check(plays==0);bridge.set_can_play(true);bridge.handle("{\"type\":\"play\"}");check(plays==1);check(saves==1);check(bridge.dirty()==0);bridge.set_can_play(true);plays=0;saves=0;
  bridge.handle("{\"type\":\"set\",\"id\":\"rr\",\"value\":999}");check(s.values[st::knob_index("rr")]==1);
  bridge.handle("{\"type\":\"discard\"}");check(bridge.dirty()==0);
  bridge.handle("{\"type\":\"preset\",\"value\":3}");check(s.preset==st::kLow);bridge.handle("{\"type\":\"cancel\"}");bridge.handle("{\"type\":\"confirm\"}");check(s.preset==st::kLow);
  bridge.handle("{\"type\":\"preset\",\"value\":3}");bridge.handle("{\"type\":\"confirm\"}");check(s.preset==st::kUltra);check(bridge.dirty()>0);
  bridge.handle("{\"type\":\"save\"}");check(saves==0);bridge.handle("{\"type\":\"confirm\"}");check(saves==1);check(bridge.dirty()==0);
  bridge.handle("{\"type\":\"play\"}");check(plays==1);bridge.handle("{\"type\":\"play\"}");check(plays==1);
  json=bridge.state_json();
  check(json.find("Off-screen bounce light")!=std::string::npos);
  check(json.find("Include hidden roofs")!=std::string::npos);
  check(json.find("Far-field sun bounce")==std::string::npos);
  check(json.find("Opacity micromaps")==std::string::npos);
  check(json.find("Measured: Not measured")==std::string::npos);
  check(json.find("\"cost\":\"Not measured\"")!=std::string::npos);
  check(json.find("Physical hair ray origins")==std::string::npos);
  auto saved=s.values;s.values[st::knob_index("rr")]=-1;bridge.handle("{\"type\":\"preset\",\"value\":0}");bridge.handle("{\"type\":\"confirm\"}");check(s.values[st::knob_index("rr")]==0);s.values=saved;
  bridge.handle("{\"type\":\"report\"}");check(prepares==0);bridge.handle("{\"type\":\"exportReport\"}");check(exports==0);
  json=bridge.handle("{\"type\":\"prepareReport\",\"issue\":\"Line\\n\\\"quoted\\\" \\u2600\",\"logs\":false,\"settings\":true,\"system\":true}");check(prepares==1);check(json.find("Line\\u000a\\\"quoted\\\"")!=std::string::npos);bridge.handle("{\"type\":\"exportReport\"}");check(exports==1);
  launcher::GraphicsInspection p;bridge.set_compatibility(p);check(s.caps==0);bridge.handle("{\"type\":\"set\",\"id\":\"rr\",\"value\":1}");check(bridge.state_json().find("\"reason\":\"Needs an NVIDIA RTX GPU with DLSS\"")!=std::string::npos);
  bridge.set_restart_count(3);check(bridge.state_json().find("\"restartNeeded\":true")!=std::string::npos);
  check(bridge.state_json().find("Restart the game to apply 3 changes")!=std::string::npos);
  bridge.handle("{\"type\":\"discard\"}");check(bridge.state_json().find("Restart the game to apply 3 changes")!=std::string::npos);
  bridge.set_restart_count(0);check(bridge.state_json().find("\"restartNeeded\":false")!=std::string::npos);
  bridge.handle("{\"type\":\"closeReport\"}");check(bridge.state_json().find("\"visible\":false")!=std::string::npos);
  int before=s.preset;bridge.handle("{\"type\":\"preset\",\"value\":3}");bridge.handle("{\"type\":\"experimental\",\"value\":true}");bridge.handle("{\"type\":\"confirm\"}");check(s.preset==before);
  check(bridge.handle(std::string(65537,'x')).find("Launcher request failed. Reopen the launcher.")!=std::string::npos);
  launcher::State dependencies;dependencies.caps=~0u;dependencies.preset=st::kLow;dependencies.values=st::preset_values(st::kLow);dependencies.values[st::knob_index("frame_generation")]=0;dependencies.values[st::knob_index("reflex")]=0;
  int writes=0;launcher::web::Hooks dh;dh.save=[&](launcher::State&,const st::KVList&,std::string&){++writes;return true;};
  launcher::web::Bridge db(dependencies,dh);
  db.handle("{\"type\":\"set\",\"id\":\"frame_generation\",\"value\":1}");
  auto preview=db.handle("{\"type\":\"save\",\"policy\":\"enable\"}");check(writes==0);check(preview.find("NVIDIA Reflex: Off \xE2\x86\x92 On")!=std::string::npos);check(dependencies.values[st::knob_index("reflex")]==0);
  db.handle("{\"type\":\"confirm\"}");check(writes==1);check(dependencies.values[st::knob_index("reflex")]==1);
  db.handle("{\"type\":\"set\",\"id\":\"local_shadows\",\"value\":3}");
  int hidden_choice=st::knob_index("local_shadows");if(hidden_choice>=0)check(dependencies.values[hidden_choice]!=3);
  launcher::GraphicsInspection multiple;multiple.adapters.resize(2);multiple.adapters[0].gpu.name="First";multiple.adapters[1].gpu.name="Second";db.set_compatibility(multiple);check(!dependencies.have_gpu);db.handle("{\"type\":\"adapter\",\"value\":1}");check(dependencies.have_gpu&&dependencies.gpu.name=="Second");
  if(hidden_choice<0)return 2;dependencies.values[hidden_choice]=3;launcher::web::Bridge legacy(dependencies);check(dependencies.values[hidden_choice]==-1);check(legacy.dirty()==0);
  launcher::State pending;pending.caps=~0u;pending.preset=st::kHigh;
  pending.base=st::code_defaults();pending.base["rt_alpha"]="0";
  pending.values=st::preset_values(st::kHigh);pending.values[st::knob_index("rt_geometry")]=1;
  const auto process_baseline=pending.base;
  launcher::web::Bridge* pending_bridge=nullptr;bool fail_save=false;
  launcher::web::Hooks ph;
  ph.save=[&](launcher::State& candidate,const st::KVList&,std::string& error){
    if(fail_save){error="Fixture save failed";return false;}
    auto after=candidate.base;
    for(int i=0;i<st::kKnobCount;++i){st::KVList kv;st::knob_kvs(st::kKnobs[i],candidate.values[i],kv);for(auto& v:kv)after[v.first]=v.second;}
    pending_bridge->set_restart_count(launcher::ui_state::restart_count(process_baseline,after));return true;
  };
  launcher::web::Bridge pb(pending,ph);pending_bridge=&pb;
  auto edit_save=[&](const char* edit){pb.handle(edit);pb.handle("{\"type\":\"save\",\"policy\":\"enable\"}");return pb.handle("{\"type\":\"confirm\"}");};
  json=edit_save("{\"type\":\"set\",\"id\":\"rt_geometry\",\"value\":2}");
  check(pb.dirty()==0);check(json.find("Restart the game to apply 1 change")!=std::string::npos);
  json=edit_save("{\"type\":\"set\",\"id\":\"fps_cap\",\"value\":60}");
  check(pb.dirty()==0);check(json.find("Restart the game to apply 1 change")!=std::string::npos);
  fail_save=true;json=edit_save("{\"type\":\"set\",\"id\":\"rt_geometry\",\"value\":1}");
  check(pb.dirty()>0);check(json.find("Restart the game to apply 1 change")!=std::string::npos);
  fail_save=false;pb.handle("{\"type\":\"save\",\"policy\":\"enable\"}");json=pb.handle("{\"type\":\"confirm\"}");
  check(pb.dirty()==0);check(json.find("\"restartNeeded\":false")!=std::string::npos);
  // Last Experimental edit is saved atomically with settings, never on draft/discard/failure.
  launcher::State trial;trial.base={{"dev","1"}};trial.caps=~0u;trial.preset=st::kUltra;trial.values=st::preset_values(st::kUltra);
  st::KVList written;bool reject=false;launcher::web::Hooks th;
  th.save=[&](launcher::State&,const st::KVList& keys,std::string&){if(reject)return false;written=keys;return true;};
  launcher::web::Bridge tb(trial,th);tb.experimental=true;
  tb.handle("{\"type\":\"set\",\"id\":\"exp_hair_groom\",\"value\":1}");
  check(written.empty());tb.handle("{\"type\":\"discard\"}");check(tb.dirty()==0);
  tb.handle("{\"type\":\"set\",\"id\":\"exp_hair_groom\",\"value\":1}");
  reject=true;tb.handle("{\"type\":\"save\",\"policy\":\"enable\"}");tb.handle("{\"type\":\"confirm\"}");check(written.empty()&&tb.dirty()>0);
  reject=false;tb.handle("{\"type\":\"save\",\"policy\":\"enable\"}");tb.handle("{\"type\":\"confirm\"}");
  st::Map ab=st::code_defaults();for(auto& kv:written)ab[kv.first]=kv.second;
  check(written.empty()); // Developer-only choices are not release A/B targets.
  // Both hotkeys share the gate; F9 wins even with a saved Experimental target
  // or when both edges arrive together. Target validity never routes F9 to flip.
  using Action=bg3::experimental_ab::HotkeyAction;
  for(bool enabled:{false,true})for(bool master:{false,true})for(bool target:{false,true})
    check(bg3::experimental_ab::hotkey_action(master,target,enabled)==
          (!enabled?Action::None:master?Action::Master:target?Action::Experimental:Action::None));
  check(tb.state_json().find("Live settings apply automatically")==std::string::npos);
  const auto before_master=ab;
  check(bg3::experimental_ab::hotkey_action(true,false,true)==Action::Master);
  check(ab==before_master);
  check(bg3::experimental_ab::hotkey_action(false,true,true)==Action::Experimental);
  std::string line;
  ab["ab_target"]="exp_hair_groom";ab["ab_choice"]="1";
  check(!bg3::experimental_ab::flip(ab,line));
  ab["ab_target"]="rr_post";ab["ab_choice"]="1";
  check(!bg3::experimental_ab::flip(ab,line));
  ab.erase("ab_target");const auto no_target=ab;
  check(!bg3::experimental_ab::flip(ab,line));check(ab==no_target);
  check(bg3::experimental_ab::hotkey_action(true,false,true)==Action::Master);
  ab["ab_target"]="exp_hair_gi_mean";check(!bg3::experimental_ab::flip(ab,line));
  ab["ab_target"]="rr";check(!bg3::experimental_ab::flip(ab,line));
  ab["ab_target"]="exp_native_coverage";ab["ab_choice"]="2";check(!bg3::experimental_ab::flip(ab,line));   // qualified (non-experimental) knobs are never A/B targets
  written.clear();tb.handle("{\"type\":\"set\",\"id\":\"fps_cap\",\"value\":60}");tb.handle("{\"type\":\"save\",\"policy\":\"enable\"}");tb.handle("{\"type\":\"confirm\"}");
  bool target_written=false;for(auto& kv:written)target_written|=kv.first=="ab_target";check(!target_written);

  // Unsupported strand activation is rejected independently for every required
  // device/file capability, and the missing prerequisite is visible on the card.
  const auto& primary=st::kKnobs[st::knob_index("exp_hair_groom_primary")];
  for (auto missing : {st::kCapRayQuery, st::kCapLinearSweptSpheres, st::kCapShaderInt64,
                       st::kCapFragmentStores, st::kCapHairGrooms}) {
    launcher::State hair;hair.base={{"dev","1"}};hair.caps=~unsigned(missing);hair.values=st::preset_values(st::kUltra);
    int calls=0;launcher::web::Hooks hh;hh.save=[&](launcher::State&,const st::KVList&,std::string&){++calls;return true;};
    launcher::web::Bridge hb(hair,hh);hb.experimental=true;
    hb.handle("{\"type\":\"set\",\"id\":\"exp_hair_groom_primary\",\"value\":1}");
    check(hair.values[st::knob_index(primary.id)]==0);
    auto text=hb.state_json();
    const char* reason=missing==st::kCapLinearSweptSpheres?"linear swept spheres":missing==st::kCapShaderInt64?"shaderInt64":missing==st::kCapFragmentStores?"fragmentStoresAndAtomics":missing==st::kCapHairGrooms?"local hair groom files":"hardware ray queries";
    check(text.find(reason)!=std::string::npos);
    hair.values[st::knob_index(primary.id)]=1;
    hb.handle("{\"type\":\"save\",\"policy\":\"enable\"}");hb.handle("{\"type\":\"confirm\"}");
    check(calls==0);
  }
  launcher::State hair;hair.caps=~0u;hair.base=st::code_defaults();hair.base["dev"]="1";hair.base["sharc_device"]="0";hair.values=st::preset_values(st::kUltra);
  hair.values[st::knob_index(primary.id)]=1;st::KVList startup;
  launcher::web::Hooks hh;hh.save=[&](launcher::State&,const st::KVList& keys,std::string&){startup=keys;return true;};
  launcher::web::Bridge hb(hair,hh);hb.experimental=true;
  auto hair_preview=hb.handle("{\"type\":\"save\",\"policy\":\"enable\"}");
  check(hair_preview.find("64-bit shader integers at startup")!=std::string::npos);
  hb.handle("{\"type\":\"confirm\"}");
  check(std::find(startup.begin(),startup.end(),std::make_pair(std::string("sharc_device"),std::string("1")))!=startup.end());
  // Badges follow saved-to-selected frame-generation transitions.
  for (int from=0;from<4;++from) for(int to=0;to<4;++to){
    launcher::State fg;fg.caps=~0u;fg.values=st::preset_values(st::kUltra);auto i=st::knob_index("frame_generation");fg.values[i]=from;
    launcher::web::Bridge fb(fg);fg.values[i]=to;auto text=fb.state_json();auto start=text.find("\"id\":\"frame_generation\"");
    auto card=text.substr(start,text.find("\"choices\":[",start)-start);
    check(card.find("\"apply\":\""+std::string(st::apply_label(st::transition_apply(st::kKnobs[i],from,to)))+"\"")!=std::string::npos);
  }

  // Every serialized card uses the current choice's Apply, and every
  // prerequisite is surfaced even when already satisfied.
  for(int i=0;i<st::kKnobCount;++i){
    auto& k=st::kKnobs[i];
    for(int c=0;c<st::choice_count(k);++c){
      launcher::State qs;qs.caps=~0u;qs.base={{"dev","1"}};qs.values=st::preset_values(st::kUltra);qs.values[i]=c;
      launcher::web::Bridge qb(qs);qb.experimental=true;auto text=qb.state_json();
      auto start=text.find("\"id\":\""+std::string(k.id)+"\"");check(start!=std::string::npos);
      auto end=text.find("\"choices\":[",start);auto card=text.substr(start,end-start);
      check(card.find("\"apply\":\""+std::string(st::apply_label(st::transition_apply(k,c,c),!k.dev_only))+"\"")!=std::string::npos);
      for(auto& n:k.choices[c].need)if(n.knob)check(text.substr(end,text.find("],\"min\"",end)-end).find(k.dev_only?"needs "+std::string(st::kKnobs[st::knob_index(n.knob)].label):std::string(st::kKnobs[st::knob_index(n.knob)].label)+": ")!=std::string::npos);
      for(auto& n:k.choices[c].need)if(n.knob){
        qs.values[st::knob_index(n.knob)]=0;
        int writes=0;launcher::web::Hooks hooks;hooks.save=[&](launcher::State& candidate,const st::KVList&,std::string&){++writes;check(st::unmet_need(k,c,candidate.values).empty());return true;};
        launcher::web::Bridge dependencies(qs,hooks);dependencies.experimental=true;
        dependencies.handle("{\"type\":\"save\",\"policy\":\"enable\"}");check(writes==0);
        dependencies.handle("{\"type\":\"confirm\"}");check(writes==1);
      }
    }
  }
  // Preset review replaces every visible custom value, retains hidden controls,
  // and matches the central table on fully supported hardware.
  for(int p=0;p<st::kPresetCount;++p){
    launcher::State ps;ps.caps=~0u;ps.preset=st::kLow;ps.values=st::preset_values(st::kLow);
    launcher::web::Bridge qb(ps);ps.values[st::knob_index("rr")]=-1;
    check(qb.state_json().find("\"custom\":true")!=std::string::npos);
    auto old=ps.values;qb.handle("{\"type\":\"preset\",\"value\":"+std::to_string(p)+"}");
    check(ps.values==old);qb.handle("{\"type\":\"confirm\"}");
    for(int i=0;i<st::kKnobCount;++i)check(ps.values[i]==(st::knob_visible(st::kKnobs[i],false)?st::kKnobs[i].preset[p]:old[i]));
    check(qb.state_json().find("\"custom\":false")!=std::string::npos);
  }
  std::printf("%d checks, %d failed\n",checks,failed);return failed?1:0;
}


