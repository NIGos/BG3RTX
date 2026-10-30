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
static int32_t index_at(const ommCpuBakeResultDesc& d, uint32_t primitive = 0) {
  if (!d.indexBuffer || primitive >= d.indexCount) return INT32_MAX;
  if (d.indexFormat == ommIndexFormat_UINT_8) return int8_t(static_cast<const uint8_t*>(d.indexBuffer)[primitive]);
  if (d.indexFormat == ommIndexFormat_UINT_16) return int16_t(static_cast<const uint16_t*>(d.indexBuffer)[primitive]);
  if (d.indexFormat == ommIndexFormat_UINT_32) return int32_t(static_cast<const uint32_t*>(d.indexBuffer)[primitive]);
  return INT32_MAX;
}
// Check subtraction before pointer formation, including truncated and overflowed
// descriptors. Only four-state packed arrays are supported by this probe.
static bool array_range(const ommCpuBakeResultDesc& d, const ommCpuOpacityMicromapDesc& desc) {
  if (!d.arrayData || desc.format != ommFormat_OC1_4_State || desc.subdivisionLevel > 12)
    return false;
  const uint32_t count = 1u << (2u * desc.subdivisionLevel);
  const uint32_t bytes = (count + 3u) / 4u;
  return desc.offset <= d.arrayDataSize && bytes <= d.arrayDataSize - desc.offset;
}
static bool decoder_checks() {
  const uint8_t data[4] = {};
  ommCpuBakeResultDesc d{};
  d.arrayData = data; d.arrayDataSize = sizeof(data);
  ommCpuOpacityMicromapDesc desc{};
  desc.format = ommFormat_OC1_4_State;
  desc.offset = 3; // Last valid byte at subdivision 0.
  if (!array_range(d, desc)) return false;
  desc.offset = 4;
  if (array_range(d, desc)) return false;
  desc.offset = UINT32_MAX;
  if (array_range(d, desc)) return false;
  desc.offset = 0; desc.subdivisionLevel = 2; // Exactly four bytes.
  if (!array_range(d, desc)) return false;
  d.arrayDataSize = 3;
  if (array_range(d, desc)) return false;
  d.arrayDataSize = 4; desc.subdivisionLevel = 13;
  if (array_range(d, desc)) return false;
  desc.subdivisionLevel = 0; desc.format = ommFormat_OC1_2_State;
  if (array_range(d, desc)) return false;
  desc.format = ommFormat_OC1_4_State; d.arrayData = nullptr;
  return !array_range(d, desc);
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
    if (!d->descArray) std::exit(2);
    const auto& desc = d->descArray[index];
    if (!array_range(*d, desc)) std::exit(2);
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

// Independent ideal normalized-coordinate nearest/bilinear sampler, mip 0,
// repeat addressing. This finite CPU oracle is not a GPU precision certificate.
static double sample(const std::array<uint8_t, 16>& tex, double u, double v,
                     ommTextureFilterMode filter) {
  auto at = [&](int x, int y) {
    x = (x % 4 + 4) % 4; y = (y % 4 + 4) % 4;
    return double(tex[y * 4 + x]) / 255.0;
  };
  if (filter == ommTextureFilterMode_Nearest)
    return at(int(std::floor(u * 4)), int(std::floor(v * 4)));
  const double x = u * 4 - .5, y = v * 4 - .5;
  const int ix = int(std::floor(x)), iy = int(std::floor(y));
  const double fx = x - ix, fy = y - iy;
  return (at(ix, iy) * (1 - fx) + at(ix + 1, iy) * fx) * (1 - fy) +
         (at(ix, iy + 1) * (1 - fx) + at(ix + 1, iy + 1) * fx) * fy;
}
static bool mixed_probe(ommBaker baker, ommTextureFilterMode filter, bool transpose) {
  std::array<uint8_t, 16> tex{};
  for (unsigned y = 0; y != 4; ++y)
    for (unsigned x = 0; x != 4; ++x)
      tex[y * 4 + x] = (transpose ? y : x) >= 2 ? 255 : 0;
  auto mip = ommCpuTextureMipDescDefault();
  mip.width = mip.height = mip.rowPitch = 4; mip.textureData = tex.data();
  auto td = ommCpuTextureDescDefault();
  td.format = ommCpuTextureFormat_UNORM8; td.mips = &mip; td.mipCount = 1;
  ommCpuTexture texture = nullptr;
  require(ommCpuCreateTexture(baker, &td, &texture), "create mixed texture");
  // Whole filter footprints of primitive 0/1 lie in constant zero/one patches:
  // normalized x [3/16,5/16] / [11/16,13/16]. Even linear's +/- half texel
  // support stays in columns 0,1 / 2,3. Primitive 2 crosses both regions and
  // exact alpha=cut at u=.5; no epsilon or cutoff shift is used.
  float uv[18] = {.1875f,.25f, .3125f,.25f, .1875f,.75f,
                  .6875f,.25f, .8125f,.25f, .6875f,.75f,
                  .1875f,.25f, .8125f,.25f, .1875f,.75f};
  if (transpose) for (unsigned i = 0; i != 18; i += 2) std::swap(uv[i], uv[i + 1]);
  const uint32_t indices[9] = {0,1,2,3,4,5,6,7,8};
  auto input = ommCpuBakeInputDescDefault();
  input.texture = texture; input.runtimeSamplerDesc.addressingMode = ommTextureAddressMode_Wrap;
  input.runtimeSamplerDesc.filter = filter; input.alphaMode = ommAlphaMode_Test;
  input.texCoordFormat = ommTexCoordFormat_UV32_FLOAT; input.texCoords = uv;
  input.texCoordStrideInBytes = 2 * sizeof(float); input.indexFormat = ommIndexFormat_UINT_32;
  input.indexBuffer = indices; input.indexCount = 9;
  input.dynamicSubdivisionScale = 0; input.maxSubdivisionLevel = 0;
  input.alphaCutoff = .5f; input.alphaCutoffLessEqual = ommOpacityState_UnknownOpaque;
  input.format = ommFormat_OC1_4_State;
  // Debug fixture only: force genuine arrays, without changing the alpha policy.
  // Level 0 has exactly one microtriangle per primitive, so no undocumented
  // bird-order vertex buffer layout or guessed recursive mapping is needed.
  input.bakeFlags = static_cast<ommCpuBakeFlags>(ommCpuBakeFlags_EnableValidation |
                    ommCpuBakeFlags_DisableSpecialIndices | ommCpuBakeFlags_Force32BitIndices);
  ommCpuBakeResult result = nullptr;
  require(ommCpuBake(baker, &input, &result), "mixed CPU bake");
  const ommCpuBakeResultDesc* d = nullptr;
  require(ommCpuGetBakeResultDesc(result, &d), "get mixed result");
  if (!d || d->indexCount != 3 || !d->descArray) std::exit(2);
  unsigned states[4] = {}, samples = 0, contradictions = 0, equality_samples = 0;
  bool layout = true;
  for (unsigned p = 0; p != 3; ++p) {
    const int32_t index = index_at(*d, p);
    if (index < 0 || uint32_t(index) >= d->descArrayCount) std::exit(2);
    const auto& desc = d->descArray[index];
    if (!array_range(*d, desc) || desc.subdivisionLevel != 0) std::exit(2);
    const unsigned state = static_cast<const uint8_t*>(d->arrayData)[desc.offset] & 3;
    ++states[state];
    layout &= state == (p == 1 ? 1u : 3u); // Solid + unknown required, not all-unknown.
    // 65-edge barycentric lattice includes vertices, edges and the exact-cut
    // midpoint. Finite point checks supplement, not prove, continuous coverage.
    for (unsigned a = 0; a <= 64; ++a) for (unsigned b = 0; b <= 64 - a; ++b) {
      const double wa = double(a) / 64, wb = double(b) / 64, wc = 1 - wa - wb;
      const double u = wa * uv[p*6] + wb * uv[p*6+2] + wc * uv[p*6+4];
      const double v = wa * uv[p*6+1] + wb * uv[p*6+3] + wc * uv[p*6+5];
      const double alpha = sample(tex, u, v, filter);
      ++samples; equality_samples += alpha == .5;
      contradictions += (state == 0 && alpha >= .5) || (state == 1 && alpha < .5);
    }
  }
  std::printf("MIXED filter=%s stripe=%c level=0 primitives=3 descriptors=%u array_bytes=%u T=%u O=%u UT=%u UO=%u samples=%u exact_cut_samples=%u known_state_contradictions=%u layout=%s\n",
    filter == ommTextureFilterMode_Linear ? "linear" : "nearest", transpose ? 'y' : 'x',
    d->descArrayCount, d->arrayDataSize, states[0], states[1], states[2], states[3], samples,
    equality_samples, contradictions, layout ? "pass" : "fail");
  require(ommCpuDestroyBakeResult(result), "destroy mixed result");
  require(ommCpuDestroyTexture(baker, texture), "destroy mixed texture");
  return layout && contradictions == 0 && samples == 6435 &&
    (filter != ommTextureFilterMode_Linear || equality_samples > 0);
}
int main() {
  if (!decoder_checks()) { std::fprintf(stderr, "decoder byte-range self-check failed\n"); return 2; }
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
  unsigned mixed_passed = 0;
  for (auto filter : {ommTextureFilterMode_Nearest, ommTextureFilterMode_Linear})
    for (bool transpose : {false, true}) mixed_passed += mixed_probe(baker, filter, transpose);
  require(ommDestroyBaker(baker), "destroy CPU baker");
  std::printf("MIXED_COMPLETE cases=4 passed=%u decoder_byte_range_checks=8; finite CPU samples only, no higher-level bird-order/GPU certification\n", mixed_passed);
  std::printf("COMPLETE cases=%u SDK known-state contradictions_vs_native_ge=%u; this is not a Vulkan traversal or performance gate\n", cases, mismatches);
  // Pin the observed SDK-1.10 CPU contract for this offline regression. A new
  // SDK with different behavior needs review, not silent runtime promotion.
  std::printf("CONSERVATIVE hard_cut_cases=%u known_state_contradictions=%u T=%u O=%u UNKNOWN=%u; no stochastic/GPU/filter-domain certification\n",
    guarded_cases, guarded_mismatches, transparent_states, opaque_states, unknown_states);
  return cases == 18 && mismatches == 6 && guarded_cases == 1532 &&
    guarded_mismatches == 0 && transparent_states == 0 && opaque_states > 0 &&
    unknown_states > 0 && mixed_passed == 4 ? 0 : 1;
}
