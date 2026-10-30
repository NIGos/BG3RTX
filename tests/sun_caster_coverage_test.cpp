#include "../common/sun_caster_coverage.h"
#include <cstdio>
#include <cstdlib>
#include <random>

using namespace bg3::sun_coverage;
static unsigned checks;
#define CHECK(x) do { ++checks; if (!(x)) { std::fprintf(stderr,"failed line %d: %s\n",__LINE__,#x); std::exit(1); } } while(0)
static NativeEpoch epoch(float phase) {
  NativeEpoch e; e.available = true;
  std::memcpy(&e.words[2], &phase, sizeof phase);
  return e;
}
static bool oracle(const Volume& v, const Ray& r, long double t) {
  for (unsigned j=0;j<3;++j) {
    long double q=v.clip[j][3];
    for (unsigned k=0;k<3;++k)
      q += static_cast<long double>(v.clip[j][k]) *
           (static_cast<long double>(r.origin[k]) + static_cast<long double>(r.direction[k])*t);
    if (!(q > (j==2?0.L:-1.L) && q < 1.L)) return false;
  }
  return true;
}
int main() {
  CommandOwners owners;
  owners.record(1,5);CHECK(owners.matches(1,5));CHECK(!owners.matches(1,7));
  owners.reset(5);CHECK(!owners.matches(1,5)); // Submitted old record, recycled handle.
  owners.record(2,5);CHECK(owners.matches(2,5));CHECK(!owners.matches(1,5));
  owners.record(3,7);CHECK(owners.matches(3,7));
  owners.forget(2);CHECK(!owners.matches(2,5));CHECK(owners.matches(3,7));
  owners.clear();CHECK(!owners.matches(3,7));CHECK(!owners.matches(0,0));
  const uint64_t good_order[]={5,7,16,17,18,19,23,27};
  CHECK(producers_before(good_order,5,18,23));
  CHECK(producers_before(good_order,18,18,23));
  CHECK(!producers_before(good_order,5,27,23));
  CHECK(!producers_before(good_order,5,18,18));
  CHECK(!producers_before(good_order,5,0,23));
  const uint64_t duplicate[]={5,18,23,18};CHECK(!producers_before(duplicate,5,18,23));
  const uint64_t missing[]={5,23};CHECK(!producers_before(missing,5,18,23));
  const uint32_t depth_spv[]={0x07230203,0x00010000,0,3,0,(3u<<16)|15,4,1,(3u<<16)|16,1,12};
  CHECK(fragment_depth_replacing(depth_spv,std::size(depth_spv)));
  auto ordinary=std::to_array(depth_spv);ordinary.back()=7;
  CHECK(!fragment_depth_replacing(ordinary.data(),ordinary.size()));
  CHECK(fragment_depth_replacing(depth_spv,std::size(depth_spv)-1));
  CHECK(fragment_depth_replacing(nullptr,0));
  Witness w; w.camera=w.cascade=w.lighting=epoch(42);
  w.camera_record=1;w.cascade_record=2;w.consumer_record=3;
  w.ordered_submission=w.geometry_complete=w.material_coverage_complete=w.converted=w.isolated_clone=true;
  CHECK(complete(w));
  for (auto member : {&Witness::ordered_submission,&Witness::geometry_complete,
                      &Witness::material_coverage_complete,&Witness::converted,&Witness::isolated_clone}) {
    Witness bad=w; bad.*member=false; CHECK(!complete(bad));
  }
  for (auto member : {&Witness::camera_record,&Witness::cascade_record,&Witness::consumer_record}) {
    Witness bad=w; bad.*member=0; CHECK(!complete(bad));
  }
  for (unsigned k=0;k<8;++k) { Witness bad=w;bad.cascade.words[k]^=1;CHECK(!complete(bad)); }
  Witness bad=w;bad.cascade=epoch(41);CHECK(!complete(bad));
  bad=w;bad.lighting=epoch(43);CHECK(!complete(bad));
  bad=w;bad.cascade.available=false;CHECK(!complete(bad));
  for (float x : {-.1f,.5f,16777216.f,INFINITY,NAN}) CHECK(!valid(epoch(x)));

  Volume box{{{{.01f,0,0,0},{0,.01f,0,0},{0,0,.001f,.5f}}},true};
  Ray ray{{0,0,0},{0,0,1},300};
  CHECK(contains_segment(box,ray)); // A real 1000 m depth domain, not guessed from 168 m.
  Volume thin=box;thin.clip[2][2]=1.f/168.2f;
  CHECK(!contains_segment(thin,ray));
  ray.tmax=40;CHECK(contains_segment(thin,ray));
  ray.origin[0]=100;CHECK(!contains_segment(box,ray)); // Boundary never certified.
  ray.origin[0]=0;ray.direction={1,0,0};ray.tmax=100;CHECK(!contains_segment(box,ray));
  ray.direction={0,0,0};CHECK(!contains_segment(box,ray));
  ray.direction={0,0,1};ray.tmax=NAN;CHECK(!contains_segment(box,ray));
  ray.tmax=300;Volume zero{};zero.available=true;CHECK(!contains_segment(zero,ray));
  Volume corrupt=box;corrupt.clip[0][0]=INFINITY;CHECK(!contains_segment(corrupt,ray));

  std::mt19937 rng(0x5a17cafe);
  std::uniform_real_distribution<float> pos(-120,120), dir(-1,1), extent(.001f,.04f), time(0,350);
  unsigned accepted=0,rejected=0;
  for(unsigned i=0;i<30000;++i) {
    Volume v=box;
    // Shear couples world axes; the oracle checks full affine transforms,
    // not a diagonal AABB shortcut.
    v.clip[0]={extent(rng),dir(rng)*.003f,dir(rng)*.003f,dir(rng)*.1f};
    v.clip[1]={dir(rng)*.003f,extent(rng),dir(rng)*.003f,dir(rng)*.1f};
    v.clip[2]={dir(rng)*.001f,dir(rng)*.001f,extent(rng)*.1f,.5f};
    Ray r{{pos(rng),pos(rng),pos(rng)},{dir(rng),dir(rng),dir(rng)},time(rng)};
    if(contains_segment(v,r)) {
      ++accepted;
      for(unsigned k=0;k<=16;++k) CHECK(oracle(v,r,static_cast<long double>(r.tmax)*k/16));
    } else ++rejected;
  }
  CHECK(accepted>0);CHECK(rejected>0);
  std::printf("sun caster coverage: %u checks, %u accepted / %u rejected finite domains; no GPU certification\n",checks,accepted,rejected);
}
