#include "../common/dlss5_nr_frame.h"
#include <cstdio>
#include <limits>
using namespace bg3::dlss5_frame;
#define CHECK(x) do{if(!(x)){std::fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x);return 1;}}while(0)
int main(){
  CHECK(boundary(true,false,true,true,false,true,true,false)==Boundary::Ready);
  CHECK(boundary(false,false,true,true,false,true,true,false)==Boundary::Off);
  CHECK(boundary(true,true,true,true,false,true,true,false)==Boundary::Bypass);
  CHECK(boundary(true,false,false,true,false,true,true,false)==Boundary::RrFailed);
  CHECK(boundary(true,false,true,false,false,true,true,false)==Boundary::PostFallback);
  CHECK(boundary(true,false,true,true,true,true,true,false)==Boundary::InsidePass);
  CHECK(boundary(true,false,true,true,false,false,true,false)==Boundary::Secondary);
  CHECK(boundary(true,false,true,true,false,true,false,false)==Boundary::MissingGuides);
  CHECK(boundary(true,false,true,true,false,true,true,true)==Boundary::Debug);
  auto bda=bda_plan(true,true,true,false);CHECK(bda.valid&&!bda.add_ext&&!bda.add_core);
  bda=bda_plan(true,true,false,false);CHECK(bda.valid&&!bda.add_ext&&bda.add_core);
  bda=bda_plan(true,false,true,false);CHECK(bda.valid&&bda.add_ext&&!bda.add_core);
  bda=bda_plan(true,false,false,false);CHECK(bda.valid&&bda.add_ext&&bda.add_core);
  bda=bda_plan(false,false,false,false);CHECK(bda.valid&&!bda.add_ext&&bda.add_core);
  bda=bda_plan(false,false,false,true);CHECK(bda.valid&&!bda.add_ext&&!bda.add_core);
  bda=bda_plan(true,true,false,true);CHECK(!bda.valid&&!bda.add_ext&&!bda.add_core);
  LayoutState layout;layout.begin_recording();layout.record_transition();
  layout.begin_recording();CHECK(!layout.general); // discarded before submit
  layout.submitted(true);CHECK(!layout.general); // early skip: no transition
  layout.record_transition();layout.submitted(false);CHECK(!layout.general);
  layout.submitted(true);CHECK(layout.general);
  layout.begin_recording();CHECK(layout.general); // discard never undoes actual execution
  Inputs in;in.render[0]=2560;in.render[1]=1067;in.output[0]=3840;in.output[1]=1600;
  for(unsigned a=0;a<2;++a){in.depth_extent[a]=in.render[a]+8;in.motion_extent[a]=in.render[a]+8;
    in.depth_base[a]=in.motion_base[a]=8;in.motion_size[a]=in.render[a];in.scale[a]=-1;}
  Guides g;float previous[]{.5f/2560,.25f/1067};
  CHECK(prepare(in,previous,true,g));CHECK(g.motion_origin[0]==8);CHECK(g.output[0]==3840);
  CHECK(std::abs(g.motion_scale[0]+1.0f/2560)<1e-10f);CHECK(g.motion_offset[0]==0);
  in.jittered=true;in.jitter[0]=-.5f;CHECK(prepare(in,previous,true,g));
  CHECK(std::abs(g.motion_offset[0]+1.0f/2560)<1e-10f);
  in.reset=true;CHECK(prepare(in,previous,true,g));CHECK(g.motion_offset[0]==0);
  in.depth_base[0]=9;CHECK(!prepare(in,previous,true,g));in.depth_base[0]=8;
  in.motion_base[1]=UINT32_MAX;CHECK(!prepare(in,previous,true,g));in.motion_base[1]=8;
  in.scale[0]=std::numeric_limits<float>::quiet_NaN();CHECK(!prepare(in,previous,true,g));
  in.scale[0]=-1;in.render[1]=0;CHECK(!prepare(in,previous,true,g));
  std::puts("PASS native NR full-frame guide/subrect/motion/jitter/reset contract");
}
