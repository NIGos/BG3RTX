#include "../common/pan_epoch.h"
#include <cstdio>

int main() {
  PanEpoch e;
  auto check = [](bool ok) { if (!ok) std::puts("pan epoch check failed"); return ok; };
  if (!check(!e.matches(7, 11))) return 1;
  e.bind(7, 1768, 11);
  // The present counter can advance to 1769 while this recording continues.
  if (!check(e.matches(7, 11) && e.frame == 1768)) return 1;
  if (!check(!e.matches(8, 11) && !e.matches(7, 12))) return 1;
  e.clear();
  if (!check(!e.matches(7, 11))) return 1;
  e.bind(8, 1769, 12);
  if(!check(e.matches(8, 12) && e.frame == 1769))return 1;
  constexpr std::string_view names[]={"depth","normal","albedo","material","primary","sun","local","gi","scene_pose","final"};
  PanBurstSamples complete{100};
  // The incomplete initial warmup is explicitly omitted, never counted as K16.
  complete.add(100,"depth",1);
  for(uint64_t frame=101;frame<=116;++frame)for(auto name:names)
    complete.add(frame,name,name=="final"?frame+1000:frame);
  if(!check(complete.complete() && !complete.retained(100) && complete.retained(116) && !complete.retained(117)))return 1;
  auto mixed=complete;mixed.frames[0].mask&=~(1u<<4);mixed.add(101,"primary",999);
  if(!check(!mixed.complete()))return 1;
  auto missing=complete;missing.frames[0].mask&=~(1u<<9);
  if(!check(!missing.complete()))return 1;
  auto duplicate=complete;duplicate.add(101,"final",2000);
  if(!check(!duplicate.complete()))return 1;
  WaterPanSamples water{100,8,true};
  for(uint64_t frame=101;frame<=108;++frame)for(auto name:{"water_game","water_coverage","water_compose_debug"})
    water.add(frame,name,frame);
  if(!check(water.complete() && !water.retained(100) && water.retained(108) && !water.retained(109)))return 1;
  auto water_missing=water;water_missing.masks[3]&=~2;
  if(!check(!water_missing.complete()))return 1;
  auto water_dup=water;water_dup.add(103,"water_game",103);
  if(!check(!water_dup.complete()))return 1;
  WaterPanSamples game{100,8,false};
  for(uint64_t frame=101;frame<=108;++frame)for(auto name:{"water_game","water_coverage"})game.add(frame,name,frame);
  if(!check(game.complete()))return 1;
  EdgePanSamples edge{100};
  for(uint64_t f=101;f<=108;++f)for(auto name:{"depth","final","material","albedo"})
    if(EdgePanSamples::wanted(name,f-100))edge.add(f,name,f);
  if(!check(edge.complete() && !edge.retained(100) && !edge.retained(109)))return 1;
  auto edge_missing=edge;edge_missing.masks[7]&=~4;
  if(!check(!edge_missing.complete()))return 1;
  auto edge_dup=edge;edge_dup.add(104,"final",104);
  if(!check(!edge_dup.complete()))return 1;
  auto edge_mixed=edge;edge_mixed.masks[0]&=~4;edge_mixed.add(101,"material",999);
  if(!check(!edge_mixed.complete()))return 1;
  if(!check(!EdgePanSamples::wanted("gi",1) && !EdgePanSamples::wanted("albedo",2)))return 1;
  std::puts("pan epoch and explicit warmup/K16 completeness checks passed");
  return 0;
}
