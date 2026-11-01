// Pins the per-draw rows of instanced wind / two-sided alpha-test draws (common/native_hit_instanced.h,
// rt_hit_material_instanced): families, the shared draw key, geometry/qualify rules, the engine staging
// into the gather layout and the two-sided row flag.
#include "../common/native_hit_instanced.h"
#include "../common/native_hit_pool.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
using namespace bg3::native_hit;
namespace vt = bg3::native_vt;
static unsigned checks;
#define CHECK(x) do{++checks;if(!(x)){std::printf("FAIL %d: %s\n",__LINE__,#x);std::exit(1);}}while(0)
static constexpr uint64_t kWindDepth = 0x1719e9d0d14390f5ull, kWindVs = 0x50fbfceb06192473ull, kWindFs = 0x8e49cde81b680054ull;
static constexpr uint64_t k2sDepth = 0x106a055077bc19d6ull, k2sVs = 0x1e9145e4b621a5e0ull, k2sFs = 0x923131c7039f33daull;
static constexpr uint64_t kStiVs = 0x2d2ac3c38992b7f0ull, kStiFs = 0x14678345c10aefbcull;
static DrawKey ordinary(uint32_t kind, uint32_t count, uint32_t instances, uint32_t first, int32_t vertex, uint32_t first_instance,
                        uint64_t ib, uint64_t ib_offset, uint64_t vb0) {
  DrawKey k;
  const uint64_t w[] = {kind, count, instances, first, uint64_t(uint32_t(vertex)), first_instance, ib, ib_offset, 1, vb0, 64, 48};
  for (size_t i = 0; i < sizeof w / sizeof w[0]; ++i) k.words[i] = w[i];
  return k;
}
int main() {
  using namespace instanced;
  // 1. Families: depth by VS only, G-buffer by the exact pair; the class A STI pair is none of them.
  CHECK(depth_family(kWindDepth) == Wind && depth_family(k2sDepth) == AlphaTest2s && depth_family(kWindVs) == None);
  CHECK(deferred_family(kWindVs, kWindFs) == Wind && deferred_family(k2sVs, k2sFs) == AlphaTest2s);
  CHECK(deferred_family(kWindVs, k2sFs) == None && deferred_family(kStiVs, kStiFs) == None && deferred_family(kWindDepth, kWindFs) == None);
  CHECK(frame_vs(kWindVs) && frame_vs(k2sVs) && !frame_vs(kWindDepth) && !frame_vs(kStiVs));
  // 2. Recipes: CaptureDraw rows, never pool pairs, still "no pool path" for the key-off census.
  const auto* w = recipe(kWindVs, kWindFs);const auto* a = recipe(k2sVs, k2sFs);
  CHECK(w && a && w->capture == vt::CaptureDraw && a->capture == vt::CaptureDraw);
  CHECK(recipe(kStiVs, kStiFs) == nullptr && recipe(kWindVs, kStiFs) == nullptr && recipe(k2sVs, kWindFs) == nullptr);
  CHECK(pool::pool_recipe(kWindVs, kWindFs) == nullptr && pool::pool_recipe(k2sVs, k2sFs) == nullptr);
  CHECK(pool::uncaptured_recipe(kWindVs, kWindFs) && pool::uncaptured_recipe(k2sVs, k2sFs) && !pool::uncaptured_recipe(kStiVs, kStiFs));
  // 3. Key: depth and G-buffer draws of one mesh join although they bind different vertex streams
  //    and first instances; count, instances, index range and family keep draws apart.
  const DrawKey depth = key(ordinary(1, 300, 12, 60, 7, 0, 0xabc, 4096, 0x111), Wind);
  const DrawKey gbuf = key(ordinary(1, 300, 12, 60, 7, 24, 0xabc, 4096, 0x222), Wind);
  CHECK(depth == gbuf && depth.words[20] == kKeyMagic && depth.words[21] == Wind && depth.words[9] == 0 && depth.words[5] == 0);
  CHECK(!(key(ordinary(1, 300, 13, 60, 7, 0, 0xabc, 4096, 0x111), Wind) == depth));   // instances
  CHECK(!(key(ordinary(1, 303, 12, 60, 7, 0, 0xabc, 4096, 0x111), Wind) == depth));   // count
  CHECK(!(key(ordinary(1, 300, 12, 63, 7, 0, 0xabc, 4096, 0x111), Wind) == depth));   // first index
  CHECK(!(key(ordinary(1, 300, 12, 60, 8, 0, 0xabc, 4096, 0x111), Wind) == depth));   // vertex offset
  CHECK(!(key(ordinary(1, 300, 12, 60, 7, 0, 0xabd, 4096, 0x111), Wind) == depth));   // index buffer
  CHECK(!(key(ordinary(1, 300, 12, 60, 7, 0, 0xabc, 4160, 0x111), Wind) == depth));   // index offset
  CHECK(!(key(ordinary(1, 300, 12, 60, 7, 0, 0xabc, 4096, 0x111), AlphaTest2s) == depth));
  CHECK(!(depth == ordinary(1, 300, 12, 60, 7, 0, 0xabc, 4096, 0x111)));             // never an ordinary key
  // Non-indexed: the stale index binding is not part of the key.
  CHECK(key(ordinary(0, 300, 4, 0, 0, 0, 0xabc, 4096, 1), Wind) == key(ordinary(0, 300, 4, 0, 0, 0, 0, 0, 2), Wind));
  // 4. Geometry: every instance's triangles (vertices = count x instances), the 64 B material stride.
  CHECK(prepass_geometry_ok(300 * 12, 12, 1200, 64) && prepass_geometry_ok(300, 1, 100, 64));
  CHECK(!prepass_geometry_ok(300 * 12, 12, 100, 64) && !prepass_geometry_ok(300 * 12, 12, 1200, 24) && !prepass_geometry_ok(0, 0, 0, 64));
  // The ordinary rule keeps declining instanced non-tileset prepass draws (unchanged).
  CHECK(!bg3::native_hit::prepass_geometry_ok(false, 300 * 12, 12, 1200, 64));
  CHECK(instanced::deferred_qualify(true, 1, 12, 64) == DeferredQualify::Accept && instanced::deferred_qualify(true, 0, 1, 64) == DeferredQualify::Accept);
  CHECK(instanced::deferred_qualify(false, 1, 12, 64) == DeferredQualify::PipelineDeclined);
  CHECK(instanced::deferred_qualify(true, 2, 12, 64) == DeferredQualify::PipelineDeclined);   // indirect
  CHECK(instanced::deferred_qualify(true, 1, 0, 64) == DeferredQualify::PipelineDeclined);
  CHECK(instanced::deferred_qualify(true, 1, 12, 24) == DeferredQualify::PipelineDeclined);
  CHECK(bg3::native_hit::deferred_qualify(true, false, true, 1, 12, 64) == DeferredQualify::PipelineDeclined);  // unchanged
  // 5. Staging into the gather's 8995 layout (hit_material_gather.hlsl Engine v[4]/v[5]/words.y/.z,
  //    Physical x at +256): wind engine {p0, p1, index @32, channel @36}, reflectance 1:1 byte 16;
  //    2S engine {p0, p1, offset @32, index @44, channel @48, flag @52}, reflectance byte 0.
  {
    uint32_t engine[13], material[8];
    for (uint32_t i = 0; i < 13; ++i) engine[i] = 0x1000u + i;
    for (uint32_t i = 0; i < 8; ++i) material[i] = 0x2000u + i;
    unsigned char s[kStageBytes];
    auto word = [&](uint32_t byte) { uint32_t v; std::memcpy(&v, s + byte, 4); return v; };
    CHECK(stage(*w, engine, 10, material, 5, s));
    for (uint32_t i = 0; i < 8; ++i) CHECK(word(64 + 4 * i) == 0x1000u + i);
    CHECK(word(100) == 0x1008u && word(104) == 0x1009u && word(96) == 0 && word(108) == 0 && word(kStagePhysical) == 0x2004u);
    CHECK(word(0) == 0 && word(60) == 0 && word(kStagePhysical + 4) == 0);
    CHECK(!stage(*w, engine, 9, material, 5, s) && !stage(*w, engine, 10, material, 4, s));   // short reads decline
    CHECK(stage(*a, engine, 13, material, 1, s));
    for (uint32_t i = 0; i < 8; ++i) CHECK(word(64 + 4 * i) == 0x1000u + i);
    CHECK(word(100) == 0x100bu && word(104) == 0x100cu && word(kStagePhysical) == 0x2000u);
    CHECK(!stage(*a, engine, 12, material, 1, s));
    // The engine reads cover engine_bytes (capture's minimum binding range).
    CHECK(pool::engine_bytes(*w) == 40 && pool::engine_bytes(*a) == 52 && vt::material_bytes(*w) == 20 && vt::material_bytes(*a) == 4);
    // Only the base layout with a UBO reflectance can be staged (other recipes keep their own paths).
    CHECK(!stage(*vt::recipe(0x3df678c09f13ea60ull), engine, 13, material, 8, s));
    CHECK(!stage(*vt::recipe(0x613ead9c442b9c8aull), engine, 13, material, 8, s));
  }
  // 6. Row flags (word 220): two-sided only for 923131c7.
  CHECK(row_flags(*a) == kRowTwoSided && row_flags(*w) == 0 && row_flags(*vt::recipe(kStiFs)) == 0 && kRowFlagsWord == 220);
  std::printf("native_hit_instanced_test: %u checks passed\n", checks);
  return 0;
}
