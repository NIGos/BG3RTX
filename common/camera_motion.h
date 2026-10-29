#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>

// Consumes GPU-read camera forward vectors, not requested input deltas.
struct CameraMotionWindow {
  double previous[3]{};
  bool have_previous = false;
  uint32_t frames = 0, scene_frames = 0, steps = 0;
  double angle_sum = 0, angle_max = 0, yaw_sum = 0, scene_angle_max = 0, translation_max = 0;
  double previous_origin[3]{};
  bool have_origin = false;
  double origin_motion_max = 0, origin_precision_bound = 0;

  void add(const float* data, bool scene_valid, const float* origin = nullptr) {
    // data: scene translation xyz, scene-vs-lighting rotation radians, forward xyz.
    double f[3] = {data[4], data[5], data[6]};
    double norm = std::sqrt(f[0]*f[0]+f[1]*f[1]+f[2]*f[2]);
    if (!std::isfinite(norm) || norm < 1e-20) { have_previous = false; have_origin = false; return; }
    for (double& v : f) v /= norm;
    ++frames;
    if (scene_valid && std::isfinite(data[0]) && std::isfinite(data[1]) &&
        std::isfinite(data[2]) && std::isfinite(data[3])) {
      ++scene_frames;
      translation_max = std::max(translation_max,std::sqrt(double(data[0])*data[0]+double(data[1])*data[1]+double(data[2])*data[2]));
      scene_angle_max = std::max(scene_angle_max,double(std::abs(data[3])));
    }
    if (have_previous) {
      const double dot = std::clamp(previous[0]*f[0]+previous[1]*f[1]+previous[2]*f[2],-1.0,1.0);
      const double angle = std::acos(dot);
      angle_sum += angle;
      angle_max = std::max(angle_max,angle);
      // Signed azimuth change; xz projections vanish for a perfectly vertical view.
      const double horizontal = std::hypot(previous[0],previous[2])*std::hypot(f[0],f[2]);
      if (horizontal > 1e-20)
        yaw_sum += std::atan2(previous[2]*f[0]-previous[0]*f[2],previous[0]*f[0]+previous[2]*f[2]);
      ++steps;
    }
    std::copy(f,f+3,previous); have_previous = true;
    if (origin && std::isfinite(origin[0]) && std::isfinite(origin[1]) && std::isfinite(origin[2])) {
      double distance2=0,bound2=0;
      for(int i=0;i<3;++i) {
        const double step=double(origin[i])-previous_origin[i];
        distance2+=step*step;
        const float old=float(previous_origin[i]);
        const double ulp_now=std::max(std::abs(double(std::nextafter(origin[i],INFINITY))-origin[i]),
                                      std::abs(double(std::nextafter(origin[i],-INFINITY))-origin[i]));
        const double ulp_old=std::max(std::abs(double(std::nextafter(old,INFINITY))-old),
                                      std::abs(double(std::nextafter(old,-INFINITY))-old));
        bound2+=(ulp_now+ulp_old)*(ulp_now+ulp_old);
        previous_origin[i]=origin[i];
      }
      if(have_origin) {
        origin_motion_max=std::max(origin_motion_max,std::sqrt(distance2));
        origin_precision_bound=std::max(origin_precision_bound,std::sqrt(bound2));
      }
      have_origin=true;
    } else have_origin=false;
  }
  void reset_window() {
    frames=scene_frames=steps=0;
    angle_sum=angle_max=yaw_sum=scene_angle_max=translation_max=0;
    origin_motion_max=origin_precision_bound=0;
  }
};
