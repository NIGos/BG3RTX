#include "../common/omm_alpha_bounds.h"
#include <array>
#include <cstdio>
#include <cstdlib>
#include <vector>

using namespace bg3::omm;
static uint64_t checks = 0;
static void check(bool condition, const char* name) {
  ++checks;
  if (!condition) { std::fprintf(stderr, "FAIL: %s\n", name); std::exit(1); }
}

struct Pyramid {
  std::vector<std::vector<uint8_t>> storage;
  std::vector<AlphaMipPlane> planes;
  Pyramid(uint32_t w, uint32_t h, uint32_t levels, bool constant = false, uint8_t value = 0) {
    std::vector<std::pair<uint32_t, uint32_t>> dimensions;
    for (uint32_t i = 0; i < levels; ++i) {
      dimensions.emplace_back(w, h);
      // Padding must never influence the bound.
      std::vector<uint8_t> data(size_t(w + 2) * h, 255);
      for (uint32_t y = 0; y < h; ++y)
        for (uint32_t x = 0; x < w; ++x)
          data[size_t(y) * (w + 2) + x] = constant ? value : uint8_t(19 + ((x*17 + y*23 + i*61) % 191));
      storage.push_back(std::move(data)); w = std::max(1u, w >> 1); h = std::max(1u, h >> 1);
    }
    for (size_t i = 0; i < storage.size(); ++i)
      planes.push_back({dimensions[i].first, dimensions[i].second,
                        dimensions[i].first + 2, storage[i], true});
  }
};
static AlphaCodeQuery query(const Pyramid& p) {
  AlphaCodeQuery q; q.encoding = AlphaEncoding::LinearUnorm8; q.texture_levels = uint32_t(p.planes.size());
  q.lod_max = float(q.texture_levels - 1); return q;
}

// Independent ideal sampler oracle. It does not inspect min/max bounds or use
// the provider's mip-range selection. This is NOT a GPU precision certificate.
static double texel(const AlphaMipPlane& m, int64_t x, int64_t y) {
  x = (x % m.width + m.width) % m.width; y = (y % m.height + m.height) % m.height;
  return m.codes[size_t(y) * m.row_stride + size_t(x)];
}
static double sample_plane(const AlphaMipPlane& m, double u, double v, AlphaFilter filter) {
  u -= std::floor(u); v -= std::floor(v);
  if (filter == AlphaFilter::Point) return texel(m, int64_t(std::floor(u*m.width)), int64_t(std::floor(v*m.height)));
  const double px = u*m.width - .5, py = v*m.height - .5;
  const int64_t x = int64_t(std::floor(px)), y = int64_t(std::floor(py));
  const double fx = px-x, fy = py-y;
  return (1-fy)*((1-fx)*texel(m,x,y)+fx*texel(m,x+1,y)) +
         fy*((1-fx)*texel(m,x,y+1)+fx*texel(m,x+1,y+1));
}
static double sample(const Pyramid& p, const AlphaCodeQuery& q, double u, double v, double lod) {
  u *= q.u_scale; v *= q.v_scale;
  lod = std::max(double(q.sampler_min_lod), std::min(double(q.sampler_max_lod), lod));
  lod = std::max(0.0, std::min(double(p.planes.size()-1), lod));
  if (q.mip_filter == AlphaFilter::Point) {
    const auto level = size_t(std::min(double(p.planes.size()-1), std::floor(lod+.5)));
    return sample_plane(p.planes[level],u,v,q.texel_filter);
  }
  const size_t lo = size_t(std::floor(lod)), hi = std::min(lo+1,p.planes.size()-1);
  const double f = lod-lo;
  return (1-f)*sample_plane(p.planes[lo],u,v,q.texel_filter) + f*sample_plane(p.planes[hi],u,v,q.texel_filter);
}

static void oracle(const Pyramid& p, const AlphaCodeQuery& q) {
  const auto bounds = alpha_code_bounds(p.planes,q);
  check(bounds.complete_code_coverage(), "oracle domain has complete code coverage");
  // Higham gamma_n bound for at most 64 double-precision interpolation
  // operations on a convex combination of at most eight codes in [0,255].
  // This is oracle arithmetic slack, not a GPU/sample/OMM quality tolerance.
  constexpr double ne = 64 * std::numeric_limits<double>::epsilon();
  constexpr double arithmetic_slack = 255 * ne / (1-ne);
  for (int yi = 0; yi <= 20; ++yi) for (int xi = 0; xi <= 32; ++xi) for (int li = 0; li <= 24; ++li) {
    const double u = q.u_min + (q.u_max-q.u_min)*xi/32.0;
    const double v = q.v_min + (q.v_max-q.v_min)*yi/20.0;
    const double lod = q.lod_min + (q.lod_max-q.lod_min)*li/24.0;
    const double value = sample(p,q,u,v,lod);
    check(value >= bounds.lower - arithmetic_slack && value <= bounds.upper + arithmetic_slack,
          "independent point/bilinear/trilinear sample remains inside integer hull");
  }
}

int main() {
  Pyramid p(5,3,3); auto q = query(p);
  for (AlphaFilter tf : {AlphaFilter::Point,AlphaFilter::Linear})
    for (AlphaFilter mf : {AlphaFilter::Point,AlphaFilter::Linear})
      for (auto scales : {std::array<float,2>{1,1}, {-3,2}, {0,-.5f}}) {
        auto a=q; a.texel_filter=tf; a.mip_filter=mf;
        a.u_min=-2; a.u_max=3; a.v_min=-1.5f; a.v_max=2.5f;
        a.u_scale=scales[0]; a.v_scale=scales[1]; oracle(p,a);
      }
  for (auto lods : {std::array<float,2>{-.5f,-.25f}, {.2f,.8f}, {1,1}, {1.25f,1.5f}, {20,80}}) {
    auto a=q; a.lod_min=lods[0]; a.lod_max=lods[1]; oracle(p,a);
  }
  {auto a=q;a.sampler_min_lod=.5f;a.sampler_max_lod=.75f;a.lod_min=-100;a.lod_max=100;oracle(p,a);}
  {auto a=q;a.sampler_max_lod=0;a.lod_min=0;a.lod_max=100;auto b=alpha_code_bounds(p.planes,a);
   check(b.first_mip==0&&b.last_mip==0&&b.texels_visited==15,"sampler clamp restricts scan domain");oracle(p,a);}
  for (uint8_t code : {uint8_t(0),uint8_t(64),uint8_t(191),uint8_t(255)}) {
    Pyramid uniform(7,5,3,true,code); auto a=query(uniform); auto b=alpha_code_bounds(uniform.planes,a);
    check(b.complete_code_coverage()&&b.lower==code&&b.upper==code,"constant codes stay exact; not a numeric sampler proof");
    oracle(uniform,a);
  }
  // A Vulkan image view may expose a partial mip chain; all DECLARED levels
  // still have to exist. Odd dimensions halve with floor, not ceiling.
  {Pyramid partial(9,7,2);auto a=query(partial);check(alpha_code_bounds(partial.planes,a).complete_code_coverage(),"declared partial chain supported");}
  {auto b=alpha_code_bounds(p.planes,q);check(b.upper<255,"row padding excluded");check(b.texels_visited==18,"scan counts odd mip extents 15+2+1");}
  auto incomplete = [&](AlphaCodeQuery a, AlphaBoundsStatus status, const char* why) {
    const auto b=alpha_code_bounds(p.planes,a);check(!b.complete_code_coverage()&&b.status==status&&b.lower==0&&b.upper==255,why);
  };
  {auto a=q;a.encoding=AlphaEncoding::VirtualTexture;incomplete(a,AlphaBoundsStatus::Unsupported,"VT cannot claim a plain texture bound");}
  {auto a=q;a.encoding=AlphaEncoding::Unsupported;incomplete(a,AlphaBoundsStatus::Unsupported,"unknown decoding cannot claim a code bound");}
  {auto a=q;a.texel_filter=AlphaFilter::Unsupported;incomplete(a,AlphaBoundsStatus::Unsupported,"unsupported texel filtering");}
  {auto a=q;a.mip_filter=static_cast<AlphaFilter>(99);incomplete(a,AlphaBoundsStatus::Unsupported,"unknown mip filter enum");}
  {auto a=q;a.texture_levels=4;incomplete(a,AlphaBoundsStatus::MissingMip,"missing declared mip rejected");}
  {auto a=q;a.max_texels=17;incomplete(a,AlphaBoundsStatus::BudgetExceeded,"budget checked before scanning");}
  {auto a=q;a.max_texels=0;incomplete(a,AlphaBoundsStatus::BudgetExceeded,"zero budget has no scan");}
  {auto a=q;a.u_min=std::numeric_limits<float>::quiet_NaN();incomplete(a,AlphaBoundsStatus::InvalidDomain,"NaN UV rejected");}
  {auto a=q;a.lod_max=std::numeric_limits<float>::infinity();incomplete(a,AlphaBoundsStatus::InvalidDomain,"infinite LOD rejected");}
  {auto a=q;a.u_scale=std::numeric_limits<float>::max();a.u_max=2;incomplete(a,AlphaBoundsStatus::InvalidDomain,"UV scale overflow rejected");}
  {auto a=q;a.u_min=2;a.u_max=1;incomplete(a,AlphaBoundsStatus::InvalidDomain,"inverted UV domain rejected");}
  {auto a=q;a.lod_min=2;a.lod_max=1;incomplete(a,AlphaBoundsStatus::InvalidDomain,"inverted LOD domain rejected");}
  {auto a=q;a.sampler_min_lod=2;a.sampler_max_lod=1;incomplete(a,AlphaBoundsStatus::InvalidDomain,"inverted sampler LOD clamp rejected");}
  {auto bad=p.planes;bad[2].resident=false;check(!alpha_code_bounds(bad,q).complete_code_coverage(),"nonresident coarse mip rejected");}
  {auto bad=p.planes;bad[2].codes={};check(!alpha_code_bounds(bad,q).complete_code_coverage(),"empty coarse mip rejected");}
  {auto bad=p.planes;bad[0].row_stride=4;check(!alpha_code_bounds(bad,q).complete_code_coverage(),"short row stride rejected");}
  {auto bad=p.planes;bad[0].codes=bad[0].codes.first(4);check(!alpha_code_bounds(bad,q).complete_code_coverage(),"truncated alpha plane rejected");}
  {auto bad=p.planes;bad[1].width=3;check(!alpha_code_bounds(bad,q).complete_code_coverage(),"wrong odd mip dimensions rejected");}
  {auto bad=p.planes;bad[0].row_stride=std::numeric_limits<size_t>::max();check(!alpha_code_bounds(bad,q).complete_code_coverage(),"byte-span overflow rejected");}
  {auto a=q;a.lod_max=0;auto bad=p.planes;bad[2].resident=false;check(!alpha_code_bounds(bad,a).complete_code_coverage(),"strict snapshot rejects missing even currently unreachable mips");}
  std::printf("PASS: %llu checks; integer support coverage only, no GPU sample certification or runtime change\n", static_cast<unsigned long long>(checks));
}
