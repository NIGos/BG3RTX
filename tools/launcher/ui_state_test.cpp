// Standalone CPU-only fixture (CTest launcher_ui_state_test).
#include "ui_state.h"
#include <cstdio>
int main() {
  namespace ui=launcher::ui_state;namespace st=bg3::settings;
  int failed=0,checks=0;
  auto check=[&](bool ok){++checks;if(!ok){++failed;std::printf("FAIL check %d\n",checks);}};
  check(ui::dirty({1,2},{1,2},2,2,L"game",L"game")==0);
  check(ui::dirty({1,3},{1,2},2,2,L"game",L"game")==1);
  check(ui::dirty({1,2},{1,2},3,2,L"new",L"game")==2);
  check(!ui::same_session("", ""));check(ui::same_session("12:34","12:34"));check(!ui::same_session("12:34","12:35"));
  st::Map baseline{{"rt_alpha","0"}}, changed{{"rt_alpha","1"}};
  check(ui::restart(baseline,changed));check(!ui::restart(baseline,baseline));
  auto disk=st::parse_ini(ui::serialize(3,true,"12:34",baseline));
  check(st::lookup(disk,"onboarding")=="3");check(st::lookup(disk,"pending_restart")=="1");
  check(st::lookup(disk,"baseline_rt_alpha")=="0");check(ui::same_session(st::lookup(disk,"session"),"12:34"));
  // A save must not replace the process baseline; repeated saves still require restart.
  st::Map restored{{"rt_alpha",st::lookup(disk,"baseline_rt_alpha")}};
  check(ui::restart(restored,changed));check(ui::restart(restored,changed));check(!ui::restart(restored,baseline));
  st::Map start{{"rt_hit_material","0"},{"rt_hit_material_wind_rgb","0"},{"rt_alpha","0"},{"rt","0"}};
  auto saved=start;saved["rt_hit_material"]="1";saved["rt_hit_material_wind_rgb"]="1";
  check(ui::restart_count(start,saved)==1); // One bundled control, two raw startup keys.
  saved["rt_alpha"]="1";saved["rt"]="1";check(ui::restart_count(start,saved)==3);
  saved["fps_cap"]="60";check(ui::restart_count(start,saved)==3); // Later live-only Save retains warning.
  auto persisted=st::parse_ini(ui::serialize(4,true,"12:34",start));
  st::Map reopened;for(auto& kv:persisted)if(kv.first.rfind("baseline_",0)==0)reopened[kv.first.substr(9)]=kv.second;
  check(ui::restart_count(reopened,saved)==3); // Closing/reopening keeps the process baseline.
  check(ui::restart_count(start,start)==0); // Saving a revert clears it.
  check(ui::restart_banner(3)=="Restart the game to apply 3 changes.");
  check(ui::restart_banner(1)=="Restart the game to apply 1 change.");check(ui::restart_banner(0).empty());
  // The same changes by name: settings a player can see, then the count of the rest. Same total as restart_count.
  {
    st::Map from{{"rt_hit_material","0"},{"rt_hit_material_wind_rgb","0"},{"rt_alpha","0"},{"rt","0"}},to=from;
    to["rt_alpha"]="1";to["rt"]="1";to["rt_hit_material"]="1";to["fps_cap"]="60";
    int others=-1;auto names=ui::restart_names(from,to,&others);
    check(int(names.size())+others==ui::restart_count(from,to));
    check(std::find(names.begin(),names.end(),"Ray-tracing detail")!=names.end());
    check(std::find(names.begin(),names.end(),"Rendered frame limit")==names.end());   // a live change is not listed
    check(ui::restart_names(from,from,&others).empty()&&others==0);
    // Hair shadows On and Detailed logging take effect at game start: the banner names them; Hair shadows Off is live.
    {
      st::Map a{{"rt_native_hair","0"},{"rt_native_scalp","0"},{"detailed_logging","0"}},b=a;
      b["rt_native_hair"]="1";b["rt_native_scalp"]="1";b["detailed_logging"]="1";
      auto n=ui::restart_names(a,b,&others);
      check(std::find(n.begin(),n.end(),"Hair shadows")!=n.end()&&std::find(n.begin(),n.end(),"Detailed logging")!=n.end());
      b["detailed_logging"]="0";check(ui::restart_count(b,a)==0);
    }
    check(ui::restart_banner({"Water lighting","Hair shadows"},0)=="Restart the game to apply: Water lighting, Hair shadows.");
    check(ui::restart_banner({"Water lighting"},2)=="Restart the game to apply: Water lighting and 2 more settings.");
    check(ui::restart_banner({},1)=="Restart the game to apply 1 change."&&ui::restart_banner({},0).empty());
  }
  for(const char* initial:{"0","1"}){
    st::Map startup{{"rt_native_tileset",initial}},candidate=startup;
    candidate["rt_native_tileset"]=initial[0]=='0'?"1":"0";
    candidate["rt_native_hair"]="1";
    check(ui::restart_count(startup,candidate)==1);
    auto disk=candidate;st::retain_startup_tilesets(candidate,startup);
    check(candidate["rt_native_tileset"]==initial);
    check(candidate["rt_native_hair"]=="1");
    // Repeated reloads retain the process baseline; the next process takes disk.
    st::retain_startup_tilesets(candidate,startup);check(candidate["rt_native_tileset"]==initial);
    st::retain_startup_tilesets(candidate,disk);check(candidate["rt_native_tileset"]==disk["rt_native_tileset"]);
  }
  std::printf("%d checks, %d failed\n",checks,failed);return failed?1:0;
}
