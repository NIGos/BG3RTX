#include "../common/omm_spatial_support.h"
#include <cstdio>
#include <cstdlib>
#include <random>
#include <vector>

using namespace bg3::omm;
static unsigned checks = 0;
static void check(bool ok) { ++checks; if (!ok) { std::fprintf(stderr, "spatial check %u failed\n", checks); std::exit(1); } }
static double texel(const AlphaMipPlane& m, int64_t x, int64_t y) {
  x %= m.width; y %= m.height;
  if (x < 0) x += m.width;
  if (y < 0) y += m.height;
  return m.codes[size_t(y) * m.row_stride + size_t(x)];
}
static double sample(const AlphaMipPlane& m, float u, float v, AlphaFilter filter) {
  if (filter == AlphaFilter::Point) return texel(m, int64_t(std::floor(double(u)*m.width)), int64_t(std::floor(double(v)*m.height)));
  const double x = double(u)*m.width - .5, y = double(v)*m.height - .5;
  const int64_t ix = int64_t(std::floor(x)), iy = int64_t(std::floor(y));
  const double fx = x - double(ix), fy = y - double(iy);
  return (1-fy)*((1-fx)*texel(m,ix,iy)+fx*texel(m,ix+1,iy)) +
         fy*((1-fx)*texel(m,ix,iy+1)+fx*texel(m,ix+1,iy+1));
}
int main() {
  std::vector<uint8_t> fine(64*64, 128), medium(32*32, 128), coarse(16*16, 128);
  fine[0] = 0; fine.back() = 255;
  std::array<AlphaMipPlane,3> mips{{{64,64,64,fine,true},{32,32,32,medium,true},{16,16,16,coarse,true}}};
  AlphaCodeQuery q; q.encoding=AlphaEncoding::LinearUnorm8; q.texture_levels=3;
  q.u_min=q.u_max=q.v_min=q.v_max=.5f;
  const auto global=alpha_code_bounds(mips,q), local=spatial_alpha_code_bounds(mips,q);
  check(global.complete_code_coverage() && global.lower==0 && global.upper==255);
  check(local.complete_code_coverage() && local.lower==128 && local.upper==128);
  check(local.texels_visited==16 && local.texels_visited<global.texels_visited);
  q.max_texels=15; check(spatial_alpha_code_bounds(mips,q).status==AlphaBoundsStatus::BudgetExceeded);
  q.max_texels=16; check(spatial_alpha_code_bounds(mips,q).complete_code_coverage());
  q.u_min=1; q.u_max=-1; check(spatial_alpha_code_bounds(mips,q).status==AlphaBoundsStatus::InvalidDomain);
  q.u_min=q.u_max=.5f; q.lod_min=.25f; q.lod_max=1.25f; q.max_texels=48;
  check(spatial_alpha_code_bounds(mips,q).last_mip==2);
  q.encoding=AlphaEncoding::VirtualTexture; check(spatial_alpha_code_bounds(mips,q).status==AlphaBoundsStatus::Unsupported);
  q.encoding=AlphaEncoding::LinearUnorm8;
  mips[1].resident=false; check(spatial_alpha_code_bounds(mips,q).status==AlphaBoundsStatus::MissingMip); mips[1].resident=true;
  WrappedAxis axis;
  check(!spatial_axis(NAN,1,1,64,AlphaFilter::Linear,axis));
  check(!spatial_axis(0,1,INFINITY,64,AlphaFilter::Linear,axis));
  check(!spatial_axis(0,1,1,0,AlphaFilter::Linear,axis));
  check(spatial_axis(-1e20f,1e20f,1,64,AlphaFilter::Linear,axis) && axis.count==64);
  std::mt19937 rng(0x24af01u);
  for (auto* data : {&fine,&medium,&coarse}) for (auto& value : *data) value=uint8_t(rng());
  // Independent ideal sampler across wrapped UVs, negative/zero scales,
  // fractional/clamped LODs and endpoint positions. No helper calls in oracle.
  std::uniform_real_distribution<float> origin(-5,5), width(0,.4f), unit(0,1);
  for (unsigned test=0;test<3000;++test) {
    q={}; q.encoding=AlphaEncoding::LinearUnorm8; q.texture_levels=3;
    q.texel_filter=test%2?AlphaFilter::Linear:AlphaFilter::Point;
    q.u_min=origin(rng); q.u_max=q.u_min+width(rng);
    q.v_min=origin(rng); q.v_max=q.v_min+width(rng);
    q.u_scale=test%11==0?0.f:(test%3==0?-2.f:1.f);
    q.v_scale=test%5==0?-1.5f:1.f;
    q.lod_min=origin(rng); q.lod_max=q.lod_min+width(rng)*6;
    const auto bound=spatial_alpha_code_bounds(mips,q);
    check(bound.complete_code_coverage());
    for (unsigned n=0;n<81;++n) {
      const float t=n==0?0:n==80?1:unit(rng), s=n<2?float(n):unit(rng);
      const float u=(q.u_min+(q.u_max-q.u_min)*t)*q.u_scale;
      const float v=(q.v_min+(q.v_max-q.v_min)*s)*q.v_scale;
      const float lod=std::clamp(q.lod_min+(q.lod_max-q.lod_min)*unit(rng),0.f,2.f);
      const unsigned l=unsigned(std::floor(lod)), h=unsigned(std::ceil(lod));
      const double f=double(lod)-l;
      const double a=(1-f)*sample(mips[l],u,v,q.texel_filter)+f*sample(mips[h],u,v,q.texel_filter);
      check(a>=double(bound.lower)-1e-10 && a<=double(bound.upper)+1e-10);
    }
  }
  std::printf("OMM spatial integer-support checks=%u; no GPU sampling certificate\n",checks);
}
