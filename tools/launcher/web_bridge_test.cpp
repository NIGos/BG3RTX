// CPU-only bridge fixture: injected effects never launch or write real settings.
#include "web_bridge.h"
#include "ui_state.h"
#include <cstdio>
namespace st=bg3::settings;
int main(int argc,char** argv){
  if(argc==2&&std::string(argv[1])=="--json"){
    launcher::State sample;sample.values=st::preset_values(st::kHigh);sample.preset=st::kHigh;
    launcher::web::Bridge bridge(sample);bridge.set_status("Line\n\"quoted\"\tcontrol\r");
    std::printf("%s",bridge.state_json().c_str());return 0;
  }
  int checks=0,failed=0,saves=0,plays=0,prepares=0,exports=0;
  auto check=[&](bool ok){++checks;if(!ok){++failed;std::printf("FAIL check %d\n",checks);}};
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
  launcher::web::Bridge bridge(s,h);
  check(bridge.dirty()==0);auto json=bridge.state_json();check(json.find("\"type\":\"state\"")!=std::string::npos);check(!bridge.dev);check(json.find("\"id\":\"ls_k\"")==std::string::npos);
  for(auto bad:{"", "[]", "{\"type\":\"discard\"} trailing", "{\"type\":\"discard\",\"type\":\"play\"}", "{\"type\":[]}","{\"type\":\"set\",\"value\":01}","{\"type\":\"set\",\"value\":1.2}","{\"type\":\"set\",\"value\":999999999999999}","{\"type\":\"\\uD800\"}"})check(bridge.handle(bad).find("Invalid launcher message")!=std::string::npos);
  bridge.handle("{\"type\":\"set\",\"id\":\"rr\",\"value\":1}");check(bridge.dirty()==1);check(s.values[st::knob_index("rr")]==1);
  bridge.handle("{\"type\":\"play\"}");check(plays==0);bridge.set_can_play(true);bridge.handle("{\"type\":\"play\"}");check(plays==0);
  bridge.handle("{\"type\":\"set\",\"id\":\"rr\",\"value\":999}");check(s.values[st::knob_index("rr")]==1);
  bridge.handle("{\"type\":\"discard\"}");check(bridge.dirty()==0);
  bridge.handle("{\"type\":\"preset\",\"value\":3}");check(s.preset==st::kLow);bridge.handle("{\"type\":\"cancel\"}");bridge.handle("{\"type\":\"confirm\"}");check(s.preset==st::kLow);
  bridge.handle("{\"type\":\"preset\",\"value\":3}");bridge.handle("{\"type\":\"confirm\"}");check(s.preset==st::kUltra);check(bridge.dirty()>0);
  bridge.handle("{\"type\":\"save\"}");check(saves==0);bridge.handle("{\"type\":\"confirm\"}");check(saves==1);check(bridge.dirty()==0);
  bridge.handle("{\"type\":\"play\"}");check(plays==1);bridge.handle("{\"type\":\"play\"}");check(plays==1);
  auto saved=s.values;s.values[st::knob_index("rr")]=-1;bridge.handle("{\"type\":\"preset\",\"value\":0}");bridge.handle("{\"type\":\"confirm\"}");check(s.values[st::knob_index("rr")]==-1);s.values=saved;
  bridge.handle("{\"type\":\"report\"}");check(prepares==0);bridge.handle("{\"type\":\"exportReport\"}");check(exports==0);
  json=bridge.handle("{\"type\":\"prepareReport\",\"issue\":\"Line\\n\\\"quoted\\\" \\u2600\",\"logs\":false,\"settings\":true,\"system\":true}");check(prepares==1);check(json.find("Line\\u000a\\\"quoted\\\"")!=std::string::npos);bridge.handle("{\"type\":\"exportReport\"}");check(exports==1);
  launcher::GraphicsInspection p;bridge.set_compatibility(p);check(s.caps==0);bridge.handle("{\"type\":\"set\",\"id\":\"rr\",\"value\":1}");check(bridge.state_json().find("Graphics compatibility unavailable")!=std::string::npos);
  bridge.set_restart_count(3);check(bridge.state_json().find("\"restartNeeded\":true")!=std::string::npos);
  check(bridge.state_json().find("Restart BG3 to apply 3 changes")!=std::string::npos);
  bridge.handle("{\"type\":\"discard\"}");check(bridge.state_json().find("Restart BG3 to apply 3 changes")!=std::string::npos);
  bridge.set_restart_count(0);check(bridge.state_json().find("\"restartNeeded\":false")!=std::string::npos);
  bridge.handle("{\"type\":\"closeReport\"}");check(bridge.state_json().find("\"visible\":false")!=std::string::npos);
  int before=s.preset;bridge.handle("{\"type\":\"preset\",\"value\":3}");bridge.handle("{\"type\":\"experimental\",\"value\":true}");bridge.handle("{\"type\":\"confirm\"}");check(s.preset==before);
  check(bridge.handle(std::string(65537,'x')).find("Invalid launcher message")!=std::string::npos);
  launcher::State dependencies;dependencies.caps=~0u;dependencies.preset=st::kLow;dependencies.values=st::preset_values(st::kLow);dependencies.values[st::knob_index("frame_generation")]=0;dependencies.values[st::knob_index("reflex")]=0;
  int writes=0;launcher::web::Hooks dh;dh.save=[&](launcher::State&,const st::KVList&,std::string&){++writes;return true;};
  launcher::web::Bridge db(dependencies,dh);
  db.handle("{\"type\":\"set\",\"id\":\"frame_generation\",\"value\":1}");
  auto preview=db.handle("{\"type\":\"save\",\"policy\":\"enable\"}");check(writes==0);check(preview.find("NVIDIA Reflex: Off -> On")!=std::string::npos);check(dependencies.values[st::knob_index("reflex")]==0);
  db.handle("{\"type\":\"confirm\"}");check(writes==1);check(dependencies.values[st::knob_index("reflex")]==1);
  db.handle("{\"type\":\"set\",\"id\":\"exp_hit_emission\",\"value\":2}");
  int emission=st::knob_index("exp_hit_emission");if(emission>=0)check(dependencies.values[emission]!=2);
  launcher::GraphicsInspection multiple;multiple.adapters.resize(2);multiple.adapters[0].gpu.name="First";multiple.adapters[1].gpu.name="Second";db.set_compatibility(multiple);check(!dependencies.have_gpu);db.handle("{\"type\":\"adapter\",\"value\":1}");check(dependencies.have_gpu&&dependencies.gpu.name=="Second");
  if(emission<0)return 2;dependencies.values[emission]=2;launcher::web::Bridge legacy(dependencies);check(dependencies.values[emission]==-1);check(legacy.dirty()==0);
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
  check(pb.dirty()==0);check(json.find("Restart BG3 to apply 1 change")!=std::string::npos);
  json=edit_save("{\"type\":\"set\",\"id\":\"fps_cap\",\"value\":60}");
  check(pb.dirty()==0);check(json.find("Restart BG3 to apply 1 change")!=std::string::npos);
  fail_save=true;json=edit_save("{\"type\":\"set\",\"id\":\"rt_geometry\",\"value\":1}");
  check(pb.dirty()>0);check(json.find("Restart BG3 to apply 1 change")!=std::string::npos);
  fail_save=false;pb.handle("{\"type\":\"save\",\"policy\":\"enable\"}");json=pb.handle("{\"type\":\"confirm\"}");
  check(pb.dirty()==0);check(json.find("\"restartNeeded\":false")!=std::string::npos);
  std::printf("%d checks, %d failed\n",checks,failed);return failed?1:0;
}


