#include "../common/omm_plain_bake.h"
#include <cstdio>
#include <cstdlib>
#include <limits>
using namespace bg3::omm;
static unsigned checks;
static void check(bool ok) {++checks;if(!ok){std::fprintf(stderr,"plain bake check %u failed\n",checks);std::exit(1);}}
int main() {
  PlainBakePush p; p.vertex_stride=24;p.triangles=1;p.source_bytes=72;
  check(valid_plain_push(p));
  // Default has no sampling certificate: the GPU kernel must emit UNKNOWN.
  check(p.sampling_certificate==0);
  for (unsigned stride:{24u,88u}) for(unsigned n:{1u,2u,50u,4096u}) {
    p.vertex_stride=stride;p.triangles=n;p.source_bytes=n*3*stride;
    check(valid_plain_push(p));
    --p.source_bytes;check(!valid_plain_push(p));++p.source_bytes;
  }
  p={};p.vertex_stride=24;p.triangles=1;p.source_bytes=72;
  auto base=p;
  p.vertex_offset=4;check(!valid_plain_push(p));p=base;
  p.vertex_stride=16;check(!valid_plain_push(p));p=base;
  p.vertex_stride=25;p.source_bytes=75;check(!valid_plain_push(p));p=base;
  p.vertex_offset=1;p.source_bytes=73;check(!valid_plain_push(p));p=base;
  p.triangles=0;check(!valid_plain_push(p));p=base;
  p.channel=4;check(!valid_plain_push(p));p=base;
  p.threshold_lo=.6f;p.threshold_hi=.5f;check(!valid_plain_push(p));p=base;
  p.threshold_lo=-.01f;check(!valid_plain_push(p));p=base;
  p.threshold_hi=1.01f;check(!valid_plain_push(p));p=base;
  p.scale_u=NAN;check(!valid_plain_push(p));p=base;
  p.alpha_error=-1;check(!valid_plain_push(p));p=base;
  p.uv_error_u=INFINITY;check(!valid_plain_push(p));p=base;
  p.sampling_certificate=2;check(!valid_plain_push(p));p=base;
  p.max_lod=17;check(!valid_plain_push(p));p=base;
  p.max_texels=UINT32_MAX;check(!valid_plain_push(p));p=base;
  p.vertex_stride=UINT32_MAX;check(!valid_plain_push(p));p=base;
  p.source_bytes=UINT32_MAX;p.triangles=UINT32_MAX;check(!valid_plain_push(p));
  std::vector<Triangle> t;
  check(!level_zero_descriptors(0,t)&&t.empty());
  check(!level_zero_descriptors(UINT32_MAX,t)&&t.empty());
  check(level_zero_descriptors(4096,t)&&t.size()==4096);
  Plan plan;
  check(make_plan(t,4096*4,0,plan)&&plan.triangles==4096&&plan.usage.size()==1);
  for(uint32_t i=0;i<4096;++i) check(t[i].dataOffset==i*4&&t[i].subdivisionLevel==0&&t[i].format==2);
  check(offsetof(PlainBakePush,scale_u)==16&&offsetof(PlainBakePush,max_lod)==32&&offsetof(PlainBakePush,alpha_error)==48);
  std::printf("OMM plain bake planning checks=%u; no GPU sample certificate\n",checks);
}
