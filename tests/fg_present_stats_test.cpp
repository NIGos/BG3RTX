#include "../common/fg_present_stats.h"
int main() {
  if (!fg_present_inputs_usable(true,true,true,true,true)) return 1;
  for (int missing=0; missing<5; ++missing) {
    bool x[5]={true,true,true,true,true}; x[missing]=false;
    if (fg_present_inputs_usable(x[0],x[1],x[2],x[3],x[4])) return 2;
  }
  FgPresentStats s;
  if (s.ratio()!=0) return 3;
  s.add(0); s.add(6); s.add(3);
  if (s.presented!=9 || s.rendered!=3 || s.ratio()!=3) return 4;
  s={}; s.add(2); s.add(1); s.add(0);
  if (s.ratio()!=1) return 5;
  s={}; s.add(3); s.add(0,false); s.add(6);
  if (s.ratio()!=3 || s.failures!=1) return 6;
  FgCpuTiming timing;
  if (timing.mean_ms()!=0 || timing.maximum_ms()!=0) return 7;
  timing.add(0); timing.add(3000); timing.add(6000);
  if (timing.calls!=3 || timing.mean_ms()!=3 || timing.maximum_ms()!=6) return 8;
  timing={}; timing.add(1000);
  if (timing.calls!=1 || timing.total_us!=1000 || timing.mean_ms()!=1 || timing.maximum_ms()!=1) return 9;
  if (fg_supported_generated(3,0)!=0 || fg_supported_generated(0,5)!=0) return 10;
  if (fg_supported_generated(3,1)!=1 || fg_supported_generated(3,5)!=2) return 11;
  if (fg_supported_generated(4,5)!=3 || fg_supported_generated(9,2)!=2) return 12;
  // A phase reset discards all preceding warmup timings and presentation counts.
  s={}; timing={};
  s.add(3); timing.add(500);
  if (s.ratio()!=3 || s.rendered!=1 || timing.mean_ms()!=0.5 || timing.calls!=1) return 13;
  return 0;
}
