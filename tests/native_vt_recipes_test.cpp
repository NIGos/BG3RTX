// Pins the recipe rows of the hit-material families audited from the dumped GLSL
// (common/native_vt_surface.h, docs/MATERIAL_FAMILIES.md classifier terms, PLAN_M4 "next families").
// Optional arguments: dumped VS modules (captures/shaders/<hash>.spv); their class A verdict
// (spirv_xfb.h analyze_vs_instancing) is checked against the recipe's capture path.
#include "../common/native_hit_pool.h"
#include "../layer/spirv_xfb.h"
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
    std::printf("%s: class A %d, recipe VS %d\n", argv[i], int(inst.ok), int(known));
  }
  std::printf("native_vt_recipes_test: %u checks passed\n", checks);
  return 0;
}
