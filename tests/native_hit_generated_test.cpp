// Pins the generated hit-material rows (rt_hit_material_generated; common/native_hit_generated.h from
// tools/material_families/recipe_rows.py) and that the key off changes no audited lookup: pool pairs, instanced families,
// depth families, the VS frame patch, its location and the gather staging.
#include "../common/native_hit_generated.h"
#include "../common/native_hit_instanced.h"
#include "../common/native_hit_pool.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
using namespace bg3::native_hit;
namespace vt = bg3::native_vt;
static unsigned checks;
#define CHECK(x) do{++checks;if(!(x)){std::printf("FAIL %d: %s\n",__LINE__,#x);std::exit(1);}}while(0)

int main() {
  // 1. Key off and on agree on every audited pair and VS (the generated FSs are never audited ones).
  for (const auto& r : vt::recipes) {
    CHECK(generated::row(r.vs, r.fs) == nullptr);
    CHECK(pool::pool_recipe(r.vs, r.fs, true) == pool::pool_recipe(r.vs, r.fs, false));
    for (bool w : {false, true})
      for (bool g : {false, true}) {
        CHECK(instanced::recipe(r.vs, r.fs, w, g, true) == instanced::recipe(r.vs, r.fs, w, g, false));
        CHECK(instanced::deferred_family(r.vs, r.fs, w, g, true) == instanced::deferred_family(r.vs, r.fs, w, g, false));
      }
  }
  for (const auto& p : instanced::kPairs)
    for (bool w : {false, true})
      for (bool g : {false, true}) {
        CHECK(instanced::depth_family(p.depth_vs, w, g, true) == instanced::depth_family(p.depth_vs, w, g, false) ||
              instanced::generated_depth(p.depth_vs) != nullptr);
        CHECK(instanced::frame_vs(p.vs, w, g, false) == (instanced::gate(p.family, w, g) != instanced::None));
        CHECK(instanced::frame_location(p.vs, false) == instanced::frame_location(p.vs, true));
      }
  CHECK(instanced::frame_location(0x105e47e448736d89ull) == 2u && instanced::frame_location(0x2d2ac3c38992b7f0ull) == 1u);
  // 2. Each generated row: reachable only with the key, through exactly one capture path, with fields its path decodes.
  CHECK(generated::kRowCount >= 1);
  for (uint32_t i = 0; i < generated::kRowCount; ++i) {
    const auto& g = generated::kRows[i];
    const auto& r = g.recipe;
    CHECK(vt::recipe(r.fs) == nullptr);
    CHECK(generated::row(r.vs, r.fs) == &g && generated::row_of(&r) == &g);
    CHECK(r.layout == vt::LayoutBase || r.layout == vt::LayoutVt4);
    CHECK(pool::engine_bytes(r) <= 64u && vt::material_bytes(r) <= 32u);   // the layer's engine[16] / ubo[8] reads
    if (r.capture == vt::CapturePool) {
      CHECK(g.family == 0 && g.depth_vs == 0 && r.layout == vt::LayoutBase);   // two-sided: RT_HIT_POOL_2S consumers (h2.w 5)
      CHECK(pool::pool_recipe(r.vs, r.fs, false) == nullptr && pool::pool_recipe(r.vs, r.fs, true) == &r);
      CHECK(instanced::recipe(r.vs, r.fs, true, true, true) == nullptr);
    } else {
      CHECK(r.capture == vt::CaptureDraw);
      CHECK(r.reflectance == vt::ReflectanceUbo || r.reflectance == vt::ReflectanceConstant);
      const auto f = instanced::Family(g.family);
      CHECK(f == instanced::AlphaTest2s || f == instanced::WindRgb || f == instanced::IpGrass);   // bit-exact families only
      CHECK(pool::pool_recipe(r.vs, r.fs, true) == nullptr);
      for (bool w : {false, true})
        for (bool ip : {false, true}) {
          CHECK(instanced::deferred_family(r.vs, r.fs, w, ip, false) == instanced::None);
          CHECK(instanced::recipe(r.vs, r.fs, w, ip, false) == nullptr);
          CHECK(instanced::deferred_family(r.vs, r.fs, w, ip, true) == f);
          CHECK(instanced::recipe(r.vs, r.fs, w, ip, true) == &r);
          CHECK(instanced::depth_family(g.depth_vs, w, ip, true) == f);
          CHECK(instanced::frame_vs(r.vs, w, ip, true));
        }
      CHECK(g.frame_location >= 1u && g.frame_location <= 13u);
      CHECK(instanced::frame_location(r.vs, true) == g.frame_location);
      // The gather staging: engine words at the row's offsets; the 1:1 float, or 0.5 for the constant mode.
      uint32_t engine[16], ubo[8];
      for (uint32_t w = 0; w < 16; ++w) engine[w] = 1000u + w;
      for (uint32_t w = 0; w < 8; ++w) ubo[w] = 2000u + w;
      unsigned char out[instanced::kStageBytes];
      CHECK(instanced::stage(r, engine, pool::engine_bytes(r) / 4u, ubo, vt::material_bytes(r) / 4u, out));
      uint32_t w100, w104, wp;float fp;
      std::memcpy(&w100, out + 100, 4);std::memcpy(&w104, out + 104, 4);std::memcpy(&wp, out + instanced::kStagePhysical, 4);
      std::memcpy(&fp, out + instanced::kStagePhysical, 4);
      CHECK(w100 == engine[r.tileset_index_offset / 4u] && w104 == engine[r.channel_offset / 4u]);
      CHECK(std::memcmp(out + 64, engine + r.transform_offset / 4u, 32) == 0);
      if (r.reflectance == vt::ReflectanceConstant) CHECK(fp == 0.5f);
      else CHECK(wp == ubo[r.reflectance_offset / 4u]);
    }
  }
  // 3. The rows the generator found on main 378d72c's inputs (asset DB + classifier), pinned: a regenerated header that
  // drops or changes one fails here and in tests/hit_material_generated_test.py.
  const auto* ip = generated::row(0x0de1b186f155904full, 0x641822a7f6dd36ecull);   // IP_AlphaTest_VT: = 35a18400, refl @8
  CHECK(ip && ip->family == instanced::AlphaTest2s && ip->depth_vs == 0x106a055077bc19d6ull && ip->frame_location == 2u &&
        ip->recipe.layout == vt::LayoutBase && ip->recipe.reflectance_offset == 8u && ip->recipe.tileset_index_offset == 44u &&
        ip->recipe.channel_offset == 48u && !ip->recipe.two_sided);
  const auto* pulse = generated::row(0x1e9145e4b621a5e0ull, 0xe4ade32941af03daull);  // Base_GM_Pulse_AlphaTest_VT: = 3da860be
  CHECK(pulse && pulse->recipe.reflectance == vt::ReflectanceConstant && pulse->family == instanced::AlphaTest2s);
  const auto* gm = generated::row(0x2d2ac3c38992b7f0ull, 0x651863a6ff4825d7ull);     // Base_GM_VT: = 3df678c0
  CHECK(gm && gm->recipe.capture == vt::CapturePool && gm->recipe.reflectance == vt::ReflectanceConstant);
  const auto* rgb = generated::row(0x9cc7094f4a674c1aull, 0x4464f4425bbb62aeull);    // wind RGB 2S: = 923131c7, two-sided
  CHECK(rgb && rgb->family == instanced::WindRgb && rgb->recipe.two_sided && rgb->recipe.reflectance_offset == 20u &&
        rgb->depth_vs == 0xea8f4071c5a3c181ull);
  const auto* two = generated::row(0x2d2ac3c38992b7f0ull, 0x2d815e2ab9046501ull);    // Base_2S_VT: = 923131c7, pool two-sided
  CHECK(two && two->recipe.capture == vt::CapturePool && two->recipe.two_sided && pool::pool_recipe(two->recipe.vs, two->recipe.fs, true) == &two->recipe);
  for (const auto& r : vt::recipes) if (r.capture == vt::CapturePool) CHECK(!r.two_sided);   // audited pool rows keep h2.w 3
  std::printf("native_hit_generated_test: %u checks, %u generated rows\n", checks, generated::kRowCount);
  return 0;
}
