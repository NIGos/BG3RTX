// Pins the recipe rows of the hit-material families audited from the dumped GLSL
// (common/native_vt_surface.h, docs/MATERIAL_FAMILIES.md classifier terms, PLAN_M4 "next families").
// Optional arguments: dumped VS modules (captures/shaders/<hash>.spv); their class A verdict
// (spirv_xfb.h analyze_vs_instancing) is checked against the recipe's capture path.
#include "../common/native_hit_pool.h"
#include "../common/native_hit_resources.h"
#include "../common/native_vt_fields.h"
#include "../layer/spirv_xfb.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
namespace vt = bg3::native_vt;
using namespace bg3::native_hit::pool;
static unsigned checks;
#define CHECK(x) do{++checks;if(!(x)){std::printf("FAIL %d: %s\n",__LINE__,#x);std::exit(1);}}while(0)
static constexpr uint64_t kSti = 0x14678345c10aefbcull, kStiVs = 0x2d2ac3c38992b7f0ull;
static constexpr uint64_t kWindFs = 0x8e49cde81b680054ull, kWindVs = 0x50fbfceb06192473ull;
static vt::Samples samples(float reflectance) {
  vt::Samples s;s.albedo={.8f,.4f,.2f,.7f};s.normal={.3f,.6f,.45f,.55f};s.physical={.1f,.9f,.25f,1};
  s.available_layers=7;s.native_view_interpretation=true;s.tileset_magic=2202;s.reflectance_available=true;s.reflectance=reflectance;return s;
}
static bool same(const vt::Surface& a, const vt::Surface& b) {
  return a.albedo==b.albedo && a.native_frame_normal==b.native_frame_normal && a.perceptual_roughness==b.perceptual_roughness &&
         a.reflectance==b.reflectance && a.metalness==b.metalness && a.albedo_aux==b.albedo_aux;
}
// std140 byte offset of member _mN of a block of N scalar floats (classifier field names).
static uint32_t scalar_member(uint32_t n) { return 4u * n; }
int main(int argc, char** argv) {
  const auto& sti = *vt::recipe(kSti);
  // 1. ATM_Wind_Vertex_RG_VT (50fbfceb/8e49cde8): COPY of 14678345, out2 `_m0` -> `_m4` of the
  //    1:1 block {float _m0.._m7}: the reflectance is byte 16; the engine UBO 1:0 is unchanged.
  {
    const auto* w = vt::recipe(kWindFs);
    CHECK(w && w->vs == kWindVs);
    CHECK(w->transform_offset == sti.transform_offset && w->tileset_index_offset == sti.tileset_index_offset &&
          w->channel_offset == sti.channel_offset && engine_bytes(*w) == engine_bytes(sti));
    CHECK(w->reflectance == vt::ReflectanceUbo && w->reflectance_offset == scalar_member(4) && vt::material_bytes(*w) == 20);
    CHECK(sti.reflectance_offset == 0 && vt::material_bytes(sti) == 4 && sti.capture == vt::CapturePool);
    CHECK(w->layout == vt::LayoutBase && !w->two_sided);
    // Same post-sampling decode as 14678345 once the reflectance comes from its own byte.
    vt::Surface a, b;
    CHECK(vt::decode(kWindFs, samples(.35f), a) == vt::Status::SupportedFields);
    CHECK(vt::decode(kSti, samples(.35f), b) == vt::Status::SupportedFields && same(a, b));
    // VS 50fbfceb is not class A (wind displaces W before the clip transform): no pool record,
    // counted as an audited recipe without a capture path.
    CHECK(w->capture == vt::CaptureNone);
    CHECK(pool_recipe(kWindVs, kWindFs) == nullptr && uncaptured_recipe(kWindVs, kWindFs));
    CHECK(!uncaptured_recipe(kStiVs, kSti) && !uncaptured_recipe(kStiVs, kWindFs) && pool_recipe(kStiVs, kSti) == &sti);
  }
  // 2. Base_AlphaTest_2s_VT (1e9145e4/923131c7): VARIANT of 14678345, fields only
  //    ubo1_0._m1@32->_m2@44 (tile-set index), _m2@36->_m3@48 (channel word, `& 7`),
  //    _m3@40->_m4@52 (flag, not read by the decode), plus the two-sided N column (out0).
  {
    const auto* a = vt::recipe(0x923131c7039f33daull);
    CHECK(a && a->vs == 0x1e9145e4b621a5e0ull);
    CHECK(a->transform_offset == 0 && a->tileset_index_offset == 44 && a->channel_offset == 48);
    CHECK(engine_bytes(*a) == 52);  // max(p0+p1 32, index 48, channel 52)
    CHECK(a->reflectance == vt::ReflectanceUbo && a->reflectance_offset == 0 && vt::material_bytes(*a) == 4);
    CHECK(a->two_sided && !sti.two_sided && a->layout == vt::LayoutBase);
    vt::Surface x, y;
    CHECK(vt::decode(0x923131c7039f33daull, samples(.6f), x) == vt::Status::SupportedFields);
    CHECK(vt::decode(kSti, samples(.6f), y) == vt::Status::SupportedFields && same(x, y));
    // VS 1e9145e4 adds the per-draw engine offset to the instance translation: not class A.
    CHECK(a->capture == vt::CaptureNone && pool_recipe(a->vs, a->fs) == nullptr && uncaptured_recipe(a->vs, a->fs));
    // Facing at a hit: the N column flips when the viewer is behind the authored normal,
    // whatever the triangle's winding (ng either sign).
    using bg3::transport::nv_two_sided_sign;using V = bg3::transport::NvVec3;
    const V up{0, 0, 1}, down{0, 0, -1}, from_above{0, 0, -1}, from_below{0, 0, 1};
    for (const V ng : {up, down}) {
      CHECK(nv_two_sided_sign(ng, up, from_above) == 1.f);    // viewer on the N side: front
      CHECK(nv_two_sided_sign(ng, up, from_below) == -1.f);   // behind: N flipped toward the viewer
      CHECK(nv_two_sided_sign(ng, down, from_below) == 1.f);
      CHECK(nv_two_sided_sign(ng, down, from_above) == -1.f);
    }
    CHECK(nv_two_sided_sign(up, V{.6f, 0, .8f}, V{.6f, 0, -.8f}) == 1.f);  // oblique, same side
  }
  // 3. ENV_DetailMap_VT (54df894a/613ead9c): NEW recipe. Bindings: engine 1:0 as 14678345
  //    {vec4 _m0[2]; uint index @32, channel @36 (`& 7`), flag @40}, 1:1 {float _m0.._m7}:
  //    _m0 strength, _m1 tiling, _m2 reflectance (RT2.y), _m3 mask power, _m4 roughness add.
  {
    const auto* d = vt::recipe(0x613ead9c442b9c8aull);
    CHECK(d && d->vs == 0x54df894ab5230cbbull && d->layout == vt::LayoutDetail && d->capture == vt::CapturePool);
    CHECK(d->transform_offset == 0 && d->tileset_index_offset == 32 && d->channel_offset == 36 && engine_bytes(*d) == 40);
    CHECK(d->reflectance == vt::ReflectanceUbo && d->reflectance_offset == scalar_member(2) && vt::material_bytes(*d) == scalar_member(5));
    CHECK(pool_recipe(d->vs, d->fs) == d && !uncaptured_recipe(d->vs, d->fs) && !d->two_sided);
    CHECK(pool_recipe(d->vs, kSti) == nullptr && pool_recipe(kStiVs, d->fs) == nullptr);
    CHECK(bg3::native_hit::nh_pool_row_detail == 224 && bg3::native_hit::nh_pool_row_detail + 32 == 256);  // Row::padding
    // nv_detail_fields against the FS 613ead9c statements written out independently.
    using namespace bg3::transport;
    auto unit = [](double x) { return std::min(1.0, std::max(0.0, x)); };
    auto reference = [&](const float nb_wzy[3], const float dn_wzy[3], float mx, float my, float py, float pz, float m0, float m3, float m4,
                         double out[5]) {
      double b[3] = {nb_wzy[0] * 2.0 - 1, nb_wzy[1] * 2.0 - 1, -(nb_wzy[2] * 2.0 - 1)}, e[3] = {dn_wzy[0] * 2.0 - 1, dn_wzy[1] * 2.0 - 1, -(dn_wzy[2] * 2.0 - 1)};
      const double lb = std::sqrt(b[0] * b[0] + b[1] * b[1] + b[2] * b[2]), le = std::sqrt(e[0] * e[0] + e[1] * e[1] + e[2] * e[2]);
      double f[3];const double mask[3] = {1, 0, 1};
      for (int k = 0; k < 3; ++k) f[k] = std::fma(e[k] / le * m0, mask[k], b[k] / lb);   // fma(n_d * _m0, (1,0,1), n_b)
      const double lf = std::sqrt(f[0] * f[0] + f[1] * f[1] + f[2] * f[2]);
      for (int k = 0; k < 3; ++k) out[k] = f[k] / lf;
      out[3] = unit(unit(std::fma(1.0 - my, m4, py)));                                     // RT2.x
      out[4] = unit(std::pow(std::max(std::fabs(double(mx)), 9.9999997473787516355514526367188e-05), m3)) * pz;  // RT1.w
    };
    unsigned cases = 0;
    for (float s : {0.f, .35f, 1.f, 2.5f}) for (float m3 : {.5f, 1.f, 2.2f}) for (float m4 : {-.4f, 0.f, .6f}) for (float my : {0.f, .3f, 1.f}) {
      const float nb[3] = {.62f, .41f, .35f}, dn[3] = {.3f, .58f, .2f};
      const float mx = .45f, py = .55f, pz = .8f;
      const NvFields base = nv_fields({.5f, .4f, .3f}, {nb[0], nb[1], nb[2]}, {.2f, py, pz}, .5f);
      CHECK(base.status == 0);
      const NvFields got = nv_detail_fields(base, {dn[0], dn[1], dn[2]}, mx, my, py, pz, s, m3, m4);
      double want[5];reference(nb, dn, mx, my, py, pz, s, m3, m4, want);
      CHECK(got.status == 0);
      CHECK(std::fabs(got.frame_normal.x - want[0]) < 2e-6 && std::fabs(got.frame_normal.y - want[1]) < 2e-6 && std::fabs(got.frame_normal.z - want[2]) < 2e-6);
      CHECK(std::fabs(got.roughness - want[3]) < 2e-6 && std::fabs(got.aux - want[4]) < 2e-6);
      CHECK(got.albedo.x == base.albedo.x && got.metalness == base.metalness && got.reflectance == base.reflectance);
      ++cases;
    }
    CHECK(cases == 108);
    // Strength 0 keeps the base frame; a non-finite detail sample or parameter is unknown.
    const NvFields base = nv_fields({.5f, .4f, .3f}, {.62f, .41f, .35f}, {.2f, .55f, .8f}, .5f);
    const NvFields flat = nv_detail_fields(base, {.3f, .58f, .2f}, .45f, .3f, .55f, .8f, 0.f, 1.f, 0.f);
    CHECK(flat.status == 0 && std::fabs(flat.frame_normal.x - base.frame_normal.x) < 1e-7 && std::fabs(flat.frame_normal.z - base.frame_normal.z) < 1e-7);
    CHECK(nv_detail_fields(base, {NAN, .5f, .5f}, .45f, .3f, .55f, .8f, 1.f, 1.f, 0.f).status == 1);
    CHECK(nv_detail_fields(base, {.5f, .5f, .5f}, .45f, .3f, .55f, .8f, INFINITY, 1.f, 0.f).status == 1);
    NvFields bad = base;bad.status = 7;CHECK(nv_detail_fields(bad, {.3f, .58f, .2f}, .45f, .3f, .55f, .8f, 1.f, 1.f, 0.f).status == 7);
    // Detail UV scale |A P| / |P|: a uniform scale 2.5 instance gives 2.5 for any P.
    CHECK(std::fabs(nv_detail_scale(2.5f * 3.f, 3.f) - 2.5f) < 1e-7f);
    // The second sampler slot (ResourceBudget::plan_second_sampler).
    using bg3::native_hit::ResourceBudget;using bg3::native_hit::ResourcePlan;using bg3::native_hit::CacheIdentity;
    ResourceBudget budget;budget.samplers = {11, 12};budget.sampler_count = 2;
    const std::array<CacheIdentity, 3> views{CacheIdentity{1, 1, 1, 1, 1}, CacheIdentity{2, 1, 2, 1, 1}, CacheIdentity{3, 1, 3, 1, 1}};
    ResourcePlan first, second;
    CHECK(budget.plan(21, views, first) && first.indices.new_sampler && first.indices.sampler == 2);
    CHECK(budget.plan_second_sampler(first, 21, second) && !second.indices.new_sampler && second.indices.sampler == 2);  // same sampler
    CHECK(budget.plan_second_sampler(first, 12, second) && !second.indices.new_sampler && second.indices.sampler == 1);  // existing
    CHECK(budget.plan_second_sampler(first, 22, second) && second.indices.new_sampler && second.indices.sampler == 3);   // after the first
    budget.commit(first);budget.commit_sampler(second);
    CHECK(budget.sampler_count == 4 && budget.samplers[2] == 21 && budget.samplers[3] == 22 && budget.material_count == 1);
    CHECK(!budget.plan_second_sampler(first, 0, second));
    ResourceBudget fullb;for (uint64_t s = 1; s <= bg3::native_hit::nh_sampler_slots - 1; ++s) fullb.samplers.push_back(s);
    fullb.sampler_count = bg3::native_hit::nh_sampler_slots - 1;
    CHECK(fullb.plan(1000, views, first) && first.indices.new_sampler);
    CHECK(!fullb.plan_second_sampler(first, 1001, second) && second.failure == bg3::native_hit::ResourceFailure::Samplers);
    // XFB frame patch at location 4 (VS 54df894a interface: vec3 W/T/P at 0/1/2, vec2 UV at 3,
    // vec3 N/B/T at 4/5/6): UV at 16, N/B/T at 24/36/48; at the default location 1 no frame.
    std::vector<uint32_t> code = {0x07230203, 0x10000, 0, 100, 0};
    auto emit = [&](std::initializer_list<uint32_t> v) { code.insert(code.end(), v); };
    emit({(2u << 16) | 17, 1});emit({(5u << 16) | 15, 0, 50, 0x6e69616d, 0});emit({(4u << 16) | 71, 20, 11, 0});
    for (uint32_t i = 0; i < 7; ++i) emit({(4u << 16) | 71, 30 + i, 30, i});
    emit({(3u << 16) | 22, 1, 32});emit({(4u << 16) | 23, 2, 1, 2});emit({(4u << 16) | 23, 3, 1, 3});emit({(4u << 16) | 23, 4, 1, 4});
    emit({(4u << 16) | 32, 5, 3, 2});emit({(4u << 16) | 32, 6, 3, 3});emit({(4u << 16) | 32, 7, 3, 4});emit({(4u << 16) | 59, 7, 20, 3});
    for (uint32_t i = 0; i < 7; ++i) emit({(4u << 16) | 59, i == 3 ? 5u : 6u, 30 + i, 3});
    std::vector<uint32_t> patched, plain;int uv = -1;uint32_t hair = 0, frame = 9;
    CHECK(bg3::patch_vs(code.data(), code.size(), patched, 64, &uv, &hair, true, &frame, false, nullptr, false, 4) && uv == 3 && frame == 1);
    auto offset_of = [&](uint32_t var) { for (size_t i = 5; i < patched.size(); i += patched[i] >> 16)
        if ((patched[i] & 65535) == 71 && (patched[i] >> 16) == 4 && patched[i + 1] == var && patched[i + 2] == 35) return int(patched[i + 3]);
      return -1; };
    CHECK(offset_of(33) == 16 && offset_of(34) == 24 && offset_of(35) == 36 && offset_of(36) == 48);
    CHECK(offset_of(30) == -1 && offset_of(31) == -1 && offset_of(32) == -1);
    CHECK(bg3::patch_vs(code.data(), code.size(), plain, 64, &uv, &hair, true, &frame) && uv == 3 && frame == 0);  // location 1: no frame
    std::vector<uint32_t> off;CHECK(bg3::patch_vs(code.data(), code.size(), off, 64) && off == plain);       // unchanged without the flag
  }
  // 4. ENV_DetailMap_Iridescent (c32ce535/9064a6bd): NEW recipe, five plain 2D textures. Bindings
  //    (dumped GLSL): engine 1:0 {uint _m0 channel (`& 7`), _m1 flag, _m2 profile (RT3.x), _m3},
  //    1:1 {float _m0.._m7}: _m0 strength, _m1 tiling, _m2 iridescence, _m3/_m4 reflectance
  //    lo/hi (RT2.y = mix by p.w), _m5 thickness (RT3.z); sampler 0:7; RT2.w = 4 | flag << 3 | ch << 4.
  {
    const auto* r = vt::recipe(0x9064a6bde6a4161cull);
    CHECK(r && r->vs == 0xc32ce535d664fbbcull && r->layout == vt::LayoutIridescent && r->capture == vt::CapturePool);
    CHECK(r->channel_offset == 0 && engine_bytes(*r) == 16 && vt::material_bytes(*r) == scalar_member(6));
    CHECK(r->reflectance == vt::ReflectanceUbo && r->reflectance_offset == scalar_member(3) && !r->two_sided);
    CHECK(vt::material_class(*r) == 4 && vt::material_class(sti) == 0);
    CHECK(pool_recipe(r->vs, r->fs) == r && !uncaptured_recipe(r->vs, r->fs) && pool_recipe(kStiVs, r->fs) == nullptr && pool_recipe(r->vs, kSti) == nullptr);
    // Row words: layout at 224, five 2D views at 228..244, all inside the 256 B row.
    CHECK(bg3::native_hit::nh_pool_row_detail + 20 + 4 <= 256);
    using namespace bg3::transport;
    auto unit = [](double x) { return std::min(1.0, std::max(0.0, x)); };
    // The FS 9064a6bd statements (_86 .. _242, _10) written out independently, in double.
    auto reference = [&](const float a_wzy[3], const float b_wzy[3], const float p[4], const float c[3], const float ir[3], const float v[3],
                         float m0, float m2, float m3, float m4, double out[9]) {
      double a[3] = {a_wzy[0] * 2.0 - 1, a_wzy[1] * 2.0 - 1, -(a_wzy[2] * 2.0 - 1)}, b[3] = {b_wzy[0] * 2.0 - 1, b_wzy[1] * 2.0 - 1, -(b_wzy[2] * 2.0 - 1)};
      const double la = std::sqrt(a[0] * a[0] + a[1] * a[1] + a[2] * a[2]), lb = std::sqrt(b[0] * b[0] + b[1] * b[1] + b[2] * b[2]);
      const double k[3] = {1.0 * m0, 0.0 * m0, -1.0 * m0};  // _86 = vec3(1, 0, -1) * _m0
      double f[3];for (int i = 0; i < 3; ++i) f[i] = std::fma(k[i], a[i] / la * p[3], b[i] / lb);
      const double lf = std::sqrt(f[0] * f[0] + f[1] * f[1] + f[2] * f[2]);
      const double lv = std::sqrt(double(v[0]) * v[0] + double(v[1]) * v[1] + double(v[2]) * v[2]);
      double d = 0;for (int i = 0; i < 3; ++i) { out[i] = f[i] / lf;d += out[i] * (v[i] / lv); }
      const double t = std::pow(std::max(std::fabs(1.0 - unit(d)), 9.9999997473787516355514526367188e-05), 1.0) * p[3] * m2;
      for (int i = 0; i < 3; ++i) out[3 + i] = unit(c[i] * (1 - t) + ir[i] * t);  // clamp(mix(c, i, t))
      out[6] = unit(p[1]);out[7] = unit(m3 * (1.0 - p[3]) + m4 * p[3]);out[8] = unit(p[0]);  // RT2.x, .y, .z
    };
    unsigned cases = 0;
    const float views[3][3] = {{.2f, .9f, .35f}, {-.7f, .1f, .7f}, {0.f, 1.f, 0.f}};
    for (float m0 : {0.f, .5f, 1.f, 2.f}) for (float m2 : {0.f, .8f, 3.f}) for (float pw : {0.f, .45f, 1.f}) for (const auto& v : views) {
      const float a[3] = {.62f, .41f, .35f}, b[3] = {.48f, .57f, .22f}, p[4] = {.15f, .7f, .33f, pw};
      const float c[3] = {.4f, .3f, .2f}, ir[3] = {.1f, .8f, .95f};const float m3 = .3f, m4 = .9f;
      const NvFields got = nv_iridescent_fields({a[0], a[1], a[2]}, {b[0], b[1], b[2]}, p[0], p[1], p[2], p[3], {c[0], c[1], c[2]}, {ir[0], ir[1], ir[2]},
                                                {v[0], v[1], v[2]}, m0, m2, m3, m4);
      double want[9];reference(a, b, p, c, ir, v, m0, m2, m3, m4, want);
      CHECK(got.status == 0);
      CHECK(std::fabs(got.frame_normal.x - want[0]) < 2e-6 && std::fabs(got.frame_normal.y - want[1]) < 2e-6 && std::fabs(got.frame_normal.z - want[2]) < 2e-6);
      CHECK(std::fabs(got.albedo.x - want[3]) < 4e-6 && std::fabs(got.albedo.y - want[4]) < 4e-6 && std::fabs(got.albedo.z - want[5]) < 4e-6);
      CHECK(std::fabs(got.roughness - want[6]) < 1e-6 && std::fabs(got.reflectance - want[7]) < 1e-6 && std::fabs(got.metalness - want[8]) < 1e-6);
      CHECK(got.aux == p[2]);
      ++cases;
    }
    CHECK(cases == 108);
    // Iridescence 0 keeps the albedo layer; strength 0 or p.w 0 keeps normal B (the z term is -_m0).
    const NvFields plain = nv_iridescent_fields({.62f, .41f, .35f}, {.48f, .57f, .22f}, .15f, .7f, .33f, 0.f, {.4f, .3f, .2f}, {.1f, .8f, .95f},
                                                {0, 1, 0}, 1.f, 0.f, .3f, .9f);
    const NvFields base_b = nv_fields({.4f, .3f, .2f}, {.48f, .57f, .22f}, {.15f, .7f, .33f}, .3f);
    CHECK(plain.status == 0 && base_b.status == 0 && plain.albedo.x == .4f && plain.albedo.z == .2f && plain.reflectance == base_b.reflectance);
    CHECK(std::fabs(plain.frame_normal.x - base_b.frame_normal.x) < 1e-7 && std::fabs(plain.frame_normal.z - base_b.frame_normal.z) < 1e-7);
    // Grazing view (dot 0): t = p.w * iridescence; head-on view along n: t = 1e-4 * p.w * iridescence.
    const NvFields head = nv_iridescent_fields({.5f, .5f, .5f}, {.5f, .5f, .5f}, 0, .5f, 0, 1.f, {0, 0, 0}, {1, 1, 1}, {0, 0, 1}, 0.f, 1.f, 0, 0);
    CHECK(head.status == 2);  // normal (0,0,0) after the decode: degenerate
    const NvFields up = nv_iridescent_fields({.5f, .5f, 0.f}, {.5f, .5f, 0.f}, 0, .5f, 0, 1.f, {0, 0, 0}, {1, 1, 1}, {0, 0, 1}, 0.f, 1.f, 0, 0);
    CHECK(up.status == 0 && std::fabs(up.frame_normal.z - 1.f) < 1e-7 && std::fabs(up.albedo.x - 1e-4f) < 1e-9);
    const NvFields side = nv_iridescent_fields({.5f, .5f, 0.f}, {.5f, .5f, 0.f}, 0, .5f, 0, 1.f, {0, 0, 0}, {1, 1, 1}, {1, 0, 0}, 0.f, 1.f, 0, 0);
    CHECK(side.status == 0 && side.albedo.x == 1.f);
    CHECK(nv_iridescent_fields({.5f, .5f, 0.f}, {.5f, .5f, 0.f}, 0, .5f, 0, 1.f, {0, 0, 0}, {1, 1, 1}, {0, 0, 0}, 0.f, 1.f, 0, 0).status == 2);  // no view
    CHECK(nv_iridescent_fields({.5f, .5f, 0.f}, {.5f, .5f, 0.f}, 0, .5f, 0, NAN, {0, 0, 0}, {1, 1, 1}, {1, 0, 0}, 0.f, 1.f, 0, 0).status == 1);
    CHECK(nv_iridescent_fields({.5f, .5f, 0.f}, {.5f, .5f, 0.f}, 0, .5f, 0, 1.f, {0, 0, 0}, {1, 1, 1}, {1, 0, 0}, INFINITY, 1.f, 0, 0).status == 1);
  }
  // Dumped VS modules: class A exactly when the recipe has a pool capture path.
  for (int i = 1; i < argc; ++i) {
    std::ifstream f(argv[i], std::ios::binary);std::string bytes((std::istreambuf_iterator<char>(f)), {});
    CHECK(bytes.size() >= 20 && bytes.size() % 4 == 0);
    std::vector<uint32_t> code(bytes.size() / 4);std::memcpy(code.data(), bytes.data(), bytes.size());
    const std::string name = argv[i];const auto slash = name.find_last_of("/\\");
    const uint64_t hash = std::stoull(name.substr(slash == std::string::npos ? 0 : slash + 1, 16), nullptr, 16);
    const auto inst = bg3::analyze_vs_instancing(code.data(), code.size(), -1);
    bool known = false;
    for (const auto& r : vt::recipes) if (r.vs == hash && r.vs != 0x3812bc980438747full) {
      known = true;CHECK(inst.ok == (r.capture == vt::CapturePool));
    }
    // A pooled recipe VS gets the XFB frame at its N location (geocapture.cpp: 54df894a at 4).
    if (known && inst.ok) {
      const uint32_t location = hash == 0x54df894ab5230cbbull ? 4u : 1u;std::vector<uint32_t> patched;int uv = -1;uint32_t hair = 0, frame = 0;
      CHECK(bg3::patch_vs(code.data(), code.size(), patched, 64, &uv, &hair, true, &frame, false, nullptr, false, location) &&
            frame == 1 && uv == int(location) - 1);
    }
    std::printf("%s: class A %d, recipe VS %d\n", argv[i], int(inst.ok), int(known));
  }
  std::printf("native_vt_recipes_test: %u checks passed\n", checks);
  return 0;
}
