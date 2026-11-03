// Standalone CPU-only fixture; C2 may register this in CMake.
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
  check(ui::restart_banner(3)=="Restart BG3 to apply 3 changes");
  check(ui::restart_banner(1)=="Restart BG3 to apply 1 change");check(ui::restart_banner(0).empty());
  std::printf("%d checks, %d failed\n",checks,failed);return failed?1:0;
}
