// Offline: common/omm_mask_bake.h against the NVIDIA OMM SDK 1.10 CPU baker (EXT micromap layout). No GPU.
//   1. Order: a mask painted per micro-triangle (index pattern through omb_micro_cell) baked by the SDK with
//      nearest filtering must give back that pattern: any known state that contradicts it means the index ->
//      barycentric order or the vertex convention (b1 weights vertex 1) is wrong. Three uv layouts, levels 1..5.
//   2. Bound: the SDK's linear-filter states and omb_bake_triangle on the same masks never contradict.
// Built with BG3RTX_OMM_CPU_PROBE=ON (CTest omm_mask_bake_sdk_probe). Prints one line per case and a summary.
#include <omm.h>
#include "../common/omm_mask_bake.h"
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

using namespace bg3::omm_mask;

static void message(ommMessageSeverity severity, const char* text, void*) {
  if (severity >= ommMessageSeverity_Error) std::fprintf(stderr, "SDK error: %s\n", text);
}
static void require(ommResult result, const char* what) {
  if (result != ommResult_SUCCESS) { std::fprintf(stderr, "%s failed, result %d\n", what, int(result)); std::exit(2); }
}
static uint32_t pattern(uint32_t i) {   // a fixed pseudo-random bit per micro-triangle index
  uint32_t h = i * 0x9E3779B1u + 0x7F4A7C15u;
  h ^= h >> 15; h *= 0x2C1B3C6Du; h ^= h >> 12;
  return h & 1u;
}
struct Case { const char* name; float uv[6]; };

// SDK states of one triangle (index 0), DisableSpecialIndices: always a real array.
static std::vector<uint32_t> sdk_bake(ommBaker baker, const std::vector<uint8_t>& tex, uint32_t n, const float uv[6],
                                      uint32_t level, ommTextureFilterMode filter) {
  auto mip = ommCpuTextureMipDescDefault();
  mip.width = mip.height = n; mip.rowPitch = n; mip.textureData = tex.data();
  auto td = ommCpuTextureDescDefault();
  td.format = ommCpuTextureFormat_UNORM8; td.mips = &mip; td.mipCount = 1;
  ommCpuTexture texture = nullptr;
  require(ommCpuCreateTexture(baker, &td, &texture), "create texture");
  const uint32_t indices[3] = {0, 1, 2};
  auto in = ommCpuBakeInputDescDefault();
  in.texture = texture;
  in.runtimeSamplerDesc.addressingMode = ommTextureAddressMode_Wrap;
  in.runtimeSamplerDesc.filter = filter;
  in.alphaMode = ommAlphaMode_Test;
  in.texCoordFormat = ommTexCoordFormat_UV32_FLOAT;
  in.texCoords = uv; in.texCoordStrideInBytes = 8;
  in.indexFormat = ommIndexFormat_UINT_32; in.indexBuffer = indices; in.indexCount = 3;
  in.dynamicSubdivisionScale = 0; in.maxSubdivisionLevel = uint8_t(level);
  in.alphaCutoff = 128.5f / 255.f;   // between UNORM8 codes: > (SDK) and >= (native) agree
  in.format = ommFormat_OC1_4_State;
  in.bakeFlags = ommCpuBakeFlags(ommCpuBakeFlags_DisableSpecialIndices | ommCpuBakeFlags_DisableDuplicateDetection);
  ommCpuBakeResult result = nullptr;
  require(ommCpuBake(baker, &in, &result), "bake");
  const ommCpuBakeResultDesc* d = nullptr;
  require(ommCpuGetBakeResultDesc(result, &d), "result");
  std::vector<uint32_t> states;
  if (d && d->indexCount == 1 && d->descArrayCount >= 1) {
    int32_t idx = d->indexFormat == ommIndexFormat_UINT_32 ? int32_t(static_cast<const uint32_t*>(d->indexBuffer)[0])
                : d->indexFormat == ommIndexFormat_UINT_16 ? int16_t(static_cast<const uint16_t*>(d->indexBuffer)[0])
                : int8_t(static_cast<const uint8_t*>(d->indexBuffer)[0]);
    if (idx >= 0 && uint32_t(idx) < d->descArrayCount && d->descArray[idx].subdivisionLevel == level) {
      const auto& desc = d->descArray[idx];
      const uint8_t* data = static_cast<const uint8_t*>(d->arrayData) + desc.offset;
      const uint32_t count = 1u << (2u * level);
      for (uint32_t i = 0; i < count; ++i) states.push_back((data[i / 4] >> ((i % 4) * 2)) & 3u);
    }
  }
  require(ommCpuDestroyBakeResult(result), "destroy result");
  require(ommCpuDestroyTexture(baker, texture), "destroy texture");
  return states;
}

int main() {
  auto bd = ommBakerCreationDescDefault();
  bd.type = ommBakerType_CPU; bd.messageInterface.messageCallback = message;
  ommBaker baker = nullptr;
  require(ommCreateBaker(&bd, &baker), "create baker");
  const Case cases[] = {{"unit", {0.f, 0.f, 1.f, 0.f, 0.f, 1.f}},
                        {"skewed", {0.1f, 0.2f, 0.9f, 0.3f, 0.2f, 0.85f}},
                        {"mirrored", {0.9f, 0.1f, 0.15f, 0.25f, 0.8f, 0.9f}}};
  const uint32_t n = 512;
  unsigned failures = 0;
  uint64_t order_known = 0, order_total = 0, bound_known_sdk = 0, bound_known_ours = 0, bound_both = 0, bound_total = 0;
  for (const Case& c : cases) {
    for (uint32_t level = 1; level <= 5; ++level) {
      const uint32_t cells = 1u << (2u * level), steps = 1u << level;
      // Cell (iu, iv, upright) -> index, through the function under test.
      std::vector<int32_t> index_of(size_t(steps) * steps * 2, -1);
      bool bijective = true;
      for (uint32_t i = 0; i < cells; ++i) {
        uint32_t iu, iv; bool up;
        omb_micro_cell(i, level, iu, iv, up);
        const uint32_t cu = up ? iu : iu - 1u, cv = up ? iv : iv - 1u;   // lower-left of the cell's square
        const bool inside = cu < steps && cv < steps && (up ? cu + cv + 1u <= steps : cu + cv + 2u <= steps);
        const size_t slot = inside ? (size_t(cv) * steps + cu) * 2 + (up ? 0 : 1) : 0;
        if (!inside || index_of[slot] >= 0) bijective = false;
        else index_of[slot] = int32_t(i);
      }
      // The mask: each texel centre takes the pattern bit of the micro-triangle containing it (barycentrics of
      // the uv triangle: b1 weights vertex 1, b2 vertex 2), 0 outside the triangle.
      const float* u = c.uv;
      const double ax = u[2] - u[0], ay = u[3] - u[1], bx = u[4] - u[0], by = u[5] - u[1];
      const double det = ax * by - ay * bx;
      std::vector<uint8_t> tex(size_t(n) * n, 0);
      for (uint32_t y = 0; y < n; ++y)
        for (uint32_t x = 0; x < n; ++x) {
          const double px = (x + 0.5) / n - u[0], py = (y + 0.5) / n - u[1];
          const double b1 = (px * by - py * bx) / det, b2 = (ax * py - ay * px) / det;
          if (b1 < 0 || b2 < 0 || b1 + b2 >= 1) continue;
          const uint32_t cu = uint32_t(b1 * steps), cv = uint32_t(b2 * steps);
          const bool up = (b1 * steps - cu) + (b2 * steps - cv) < 1.0;
          const int32_t idx = index_of[(size_t(cv) * steps + cu) * 2 + (up ? 0 : 1)];
          if (idx >= 0 && pattern(uint32_t(idx))) { tex[size_t(y) * n + x] = 255; }
        }
      // 1. order
      const auto nearest = sdk_bake(baker, tex, n, c.uv, level, ommTextureFilterMode_Nearest);
      uint32_t known = 0, contra = 0;
      for (uint32_t i = 0; i < nearest.size(); ++i) {
        if (nearest[i] > 1) continue;
        ++known;
        contra += nearest[i] != pattern(i);
      }
      // 2. bound: ours (conservative bilinear support) vs the SDK's linear filter, on a foliage-like mask
      // (discs of alpha 1 on 0, two grey levels at their rims) under the same triangle.
      std::vector<uint8_t> disc(size_t(n) * n, 0);
      std::vector<float> discf(size_t(n) * n, 0.f);
      for (uint32_t k = 0; k < 40; ++k) {
        const double cx = (pattern(k * 7 + 1) * 0.5 + (k * 37 % 100) / 200.0) * n, cy = ((k * 53 % 100) / 100.0) * n;
        const double r = (8 + (k * 29 % 40)) * n / 512.0;
        for (uint32_t y = 0; y < n; ++y)
          for (uint32_t x = 0; x < n; ++x) {
            const double dx = x + 0.5 - cx, dy = y + 0.5 - cy, dd = std::sqrt(dx * dx + dy * dy);
            const uint8_t a = dd < r ? 255 : dd < r + 1 ? 160 : dd < r + 2 ? 60 : 0;
            if (a > disc[size_t(y) * n + x]) { disc[size_t(y) * n + x] = a; discf[size_t(y) * n + x] = a / 255.f; }
          }
      }
      const auto linear = sdk_bake(baker, disc, n, c.uv, level, ommTextureFilterMode_Linear);
      std::vector<uint32_t> words((cells + 15) / 16);
      OmbMask m{n, n, discf.data()};
      const float cut = 128.5f / 255.f;   // between codes: the SDK's > and the test's >= agree
      omb_bake_triangle(m, c.uv, level, cut, cut, words.data());
      uint32_t ours_known = 0, sdk_known = 0, both = 0, bound_contra = 0, ours_only = 0;
      for (uint32_t i = 0; i < cells && i < linear.size(); ++i) {
        const uint32_t s = (words[i / 16] >> (2 * (i % 16))) & 3u;
        ours_known += s <= 1; sdk_known += linear[i] <= 1;
        if (s <= 1 && linear[i] <= 1) { ++both; bound_contra += s != linear[i]; }
        ours_only += s <= 1 && linear[i] > 1;   // reported: the SDK's own conservatism differs from ours
      }
      // A wrong order or vertex convention contradicts about half of the known states; the unit layout aligns the
      // micro-triangle edges with texel edges, so most of its states are known.
      const uint32_t need = level < 3 ? 0u : std::string(c.name) == "unit" ? cells / 2u : cells / 64u;
      const bool ok = bijective && nearest.size() == cells && linear.size() == cells && contra == 0 && known >= need &&
                      bound_contra == 0;
      failures += !ok;
      order_known += known; order_total += cells;
      bound_known_sdk += sdk_known; bound_known_ours += ours_known; bound_both += both; bound_total += cells;
      std::printf("%s %-8s level %u: bijective %d; order: SDK nearest known %u of %u, contradictions %u; bound: ours known "
                  "%u, SDK linear known %u, both %u, ours only %u, contradictions %u\n",
                  ok ? "PASS" : "FAIL", c.name, level, int(bijective), known, cells, contra, ours_known, sdk_known, both,
                  ours_only, bound_contra);
    }
  }
  require(ommDestroyBaker(baker), "destroy baker");
  std::printf("omm_mask_bake_sdk_probe: %u failures; order known %llu of %llu micro-triangles; bound: ours %llu, SDK %llu, "
              "both %llu of %llu\n", failures, (unsigned long long)order_known, (unsigned long long)order_total,
              (unsigned long long)bound_known_ours, (unsigned long long)bound_known_sdk, (unsigned long long)bound_both,
              (unsigned long long)bound_total);
  return failures ? 1 : 0;
}
