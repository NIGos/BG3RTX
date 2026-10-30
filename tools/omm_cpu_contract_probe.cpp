// Offline CPU SDK contract probe. No Vulkan/device/GPU/game calls.
#include <omm.h>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <utility>

static void message(ommMessageSeverity severity, const char* text, void*) {
  if (severity >= ommMessageSeverity_Error) std::fprintf(stderr, "SDK error: %s\n", text);
}
static void require(ommResult result, const char* what) {
  if (result != ommResult_SUCCESS) {
    std::fprintf(stderr, "%s failed, result %d\n", what, int(result));
    std::exit(2);
  }
}
static int32_t index_at(const ommCpuBakeResultDesc& d) {
  if (d.indexFormat == ommIndexFormat_UINT_8) return int8_t(static_cast<const uint8_t*>(d.indexBuffer)[0]);
  if (d.indexFormat == ommIndexFormat_UINT_16) return int16_t(static_cast<const uint16_t*>(d.indexBuffer)[0]);
  if (d.indexFormat == ommIndexFormat_UINT_32) return int32_t(static_cast<const uint32_t*>(d.indexBuffer)[0]);
  return INT32_MAX;
}
struct ProbeResult { bool contradiction; unsigned transparent, opaque, unknown; };
static ProbeResult probe(ommBaker baker, uint8_t code, float cut, ommTextureFilterMode filter,
                         bool conservative = false, bool verbose = true) {
  std::array<uint8_t, 4> alpha{code, code, code, code};
  auto mip = ommCpuTextureMipDescDefault();
  mip.width = mip.height = 2;
  mip.rowPitch = 2;
  mip.textureData = alpha.data();
  auto tex_desc = ommCpuTextureDescDefault();
  tex_desc.format = ommCpuTextureFormat_UNORM8;
  tex_desc.mips = &mip;
  tex_desc.mipCount = 1;
  ommCpuTexture texture = nullptr;
  require(ommCpuCreateTexture(baker, &tex_desc, &texture), "create texture");
  const float uv[6] = {.125f, .125f, .875f, .125f, .125f, .875f};
  const uint32_t indices[3] = {0, 1, 2};
  auto input = ommCpuBakeInputDescDefault();
  input.texture = texture;
  input.runtimeSamplerDesc.addressingMode = ommTextureAddressMode_Wrap;
  input.runtimeSamplerDesc.filter = filter;
  input.runtimeSamplerDesc.borderAlpha = 0;
  input.alphaMode = ommAlphaMode_Test;
  input.texCoordFormat = ommTexCoordFormat_UV32_FLOAT;
  input.texCoords = uv;
  input.texCoordStrideInBytes = sizeof(float) * 2;
  input.indexFormat = ommIndexFormat_UINT_32;
  input.indexBuffer = indices;
  input.indexCount = 3;
  input.dynamicSubdivisionScale = 0;
  input.maxSubdivisionLevel = 2;
  input.alphaCutoff = cut;
  // Native >= accepts equality. The SDK only offers > / <=: keep the <=
  // side UNKNOWN, rather than incorrectly certifying it transparent. This
  // deliberately sacrifices known transparent coverage and equality acceleration.
  // This adapter is only for a fixed hard-cut predicate with the same sample;
  // it is NOT valid for a varying stochastic threshold or a different sampler.
  if (conservative) input.alphaCutoffLessEqual = ommOpacityState_UnknownOpaque;
  // The baseline deliberately retains SDK defaults (> opaque, <= transparent).
  // Four-state format retains unknown candidates; no forced two-state path.
  input.format = ommFormat_OC1_4_State;
  input.bakeFlags = ommCpuBakeFlags_EnableValidation;
  ommCpuBakeResult result = nullptr;
  require(ommCpuBake(baker, &input, &result), "CPU bake");
  const ommCpuBakeResultDesc* d = nullptr;
  require(ommCpuGetBakeResultDesc(result, &d), "get bake result");
  if (!d || d->indexCount != 1 || !d->indexBuffer) {
    std::fprintf(stderr, "unexpected result layout\n");
    std::exit(2);
  }
  const int32_t index = index_at(*d);
  uint32_t states[4] = {};
  if (index == -1) states[0] = 16;
  else if (index == -2) states[1] = 16;
  else if (index == -3) states[2] = 16;
  else if (index == -4) states[3] = 16;
  else if (index >= 0 && uint32_t(index) < d->descArrayCount) {
    const auto& desc = d->descArray[index];
    if (desc.format != ommFormat_OC1_4_State || desc.subdivisionLevel > 12) std::exit(2);
    const uint32_t count = 1u << (2u * desc.subdivisionLevel);
    const uint8_t* data = static_cast<const uint8_t*>(d->arrayData) + desc.offset;
    for (uint32_t i = 0; i < count; ++i) ++states[(data[i / 4] >> ((i % 4) * 2)) & 3];
  } else {
    std::fprintf(stderr, "unexpected index %d\n", index);
    std::exit(2);
  }
  const float native_alpha = float(code) / 255.0f;
  const bool native_solid = native_alpha >= cut;
  const bool contradiction = native_solid ? states[0] != 0 : states[1] != 0;
  if (verbose) std::printf("policy=%s filter=%s code=%u alpha=%.9g cut=%.9g native_ge=%d index=%d T=%u O=%u UT=%u UO=%u mismatch=%d array_bytes=%u descriptors=%u\n",
    conservative ? "native_ge_conservative" : "SDK_default",
    filter == ommTextureFilterMode_Linear ? "linear" : "nearest", code, native_alpha, cut,
    native_solid, index, states[0], states[1], states[2], states[3], contradiction,
    d->arrayDataSize, d->descArrayCount);
  require(ommCpuDestroyBakeResult(result), "destroy bake result");
  require(ommCpuDestroyTexture(baker, texture), "destroy texture");
  return {contradiction, states[0], states[1], states[2] + states[3]};
}
int main() {
  const auto version = ommGetLibraryDesc();
  std::printf("OMM SDK CPU library %u.%u.%u; no GPU/device creation\n",
    version.versionMajor, version.versionMinor, version.versionBuild);
  auto desc = ommBakerCreationDescDefault();
  desc.type = ommBakerType_CPU;
  desc.messageInterface.messageCallback = message;
  ommBaker baker = nullptr;
  require(ommCreateBaker(&desc, &baker), "create CPU baker");
  unsigned mismatches = 0, cases = 0;
  for (auto filter : {ommTextureFilterMode_Nearest, ommTextureFilterMode_Linear}) {
    for (const auto& v : std::array<std::pair<uint8_t, float>, 9>{{
      {0, 0.f}, {0, std::nextafter(0.f, 1.f)}, {1, 0.f},
      {127, 128.f / 255.f}, {128, 128.f / 255.f}, {129, 128.f / 255.f},
      {255, 1.f}, {255, std::nextafter(1.f, 0.f)}, {254, 1.f}
    }}) {
      mismatches += probe(baker, v.first, v.second, filter).contradiction;
      ++cases;
    }
  }
  unsigned guarded_cases = 0, guarded_mismatches = 0, unknown_states = 0,
           opaque_states = 0, transparent_states = 0;
  // Exhaust every UNORM8 code at its float32 cutoff and both adjacent float32
  // thresholds (within the SDK's [0,1] domain), using the real CPU baker.
  for (auto filter : {ommTextureFilterMode_Nearest, ommTextureFilterMode_Linear}) {
    for (unsigned code = 0; code != 256; ++code) {
      const float alpha = float(code) / 255.f;
      for (float cut : {std::nextafter(alpha, -INFINITY), alpha,
                        std::nextafter(alpha, INFINITY)}) {
        if (cut < 0 || cut > 1) continue;
        const auto r = probe(baker, uint8_t(code), cut, filter, true, false);
        ++guarded_cases;
        guarded_mismatches += r.contradiction;
        unknown_states += r.unknown;
        opaque_states += r.opaque;
        transparent_states += r.transparent;
      }
    }
  }
  require(ommDestroyBaker(baker), "destroy CPU baker");
  std::printf("COMPLETE cases=%u SDK known-state contradictions_vs_native_ge=%u; this is not a Vulkan traversal or performance gate\n", cases, mismatches);
  // Pin the observed SDK-1.10 CPU contract for this offline regression. A new
  // SDK with different behavior needs review, not silent runtime promotion.
  std::printf("CONSERVATIVE hard_cut_cases=%u known_state_contradictions=%u T=%u O=%u UNKNOWN=%u; no stochastic/GPU/filter-domain certification\n",
    guarded_cases, guarded_mismatches, transparent_states, opaque_states, unknown_states);
  return cases == 18 && mismatches == 6 && guarded_cases == 1532 &&
    guarded_mismatches == 0 && transparent_states == 0 && opaque_states > 0 &&
    unknown_states > 0 ? 0 : 1;
}
