#include "../common/camera_motion.h"
#include <limits>

int main() {
  CameraMotionWindow w;
  float a[7] = {0,0,0,0,0,0,1};
  w.add(a,true); w.add(a,true);
  if(w.frames!=2 || w.steps!=1 || w.angle_sum>1e-7) return 1;
  float b[7] = {3,4,0,.1f,1,0,0};
  w.add(b,true);
  if(std::abs(w.angle_sum-std::acos(0.0))>1e-7 || w.yaw_sum<=0 || w.translation_max!=5) return 2;
  w.reset_window(); w.add(a,false);
  if(w.frames!=1 || w.scene_frames!=0 || w.yaw_sum>=0) return 3;
  a[4]=std::numeric_limits<float>::quiet_NaN(); w.add(a,true);
  if(w.have_previous || w.frames!=1) return 4;
  a[4]=0; a[6]=2; w.add(a,true);
  if(w.steps!=1 || w.frames!=2 || w.scene_frames!=1) return 5;
  CameraMotionWindow translation;
  float origin[3]={100,20,-300};
  translation.add(a,true,origin); origin[0]+=.1f; translation.add(a,true,origin);
  if(translation.origin_motion_max<.09 || translation.origin_precision_bound>=.001) return 6;
  return 0;
}
