#pragma once
// Offline frame replay pack (tools/frame_replay.md): everything ONE production compute pass reads in
// one frame, as files plus a line-based manifest, so tools/frame_replay can rebuild the TLAS, bind
// the same descriptors and dispatch the real shader without the game. Header-only, no Vulkan calls:
// the layer/core writer and the harness share the pass contract and the manifest grammar.
//
// manifest.txt, one directive per line ('#' comments; file names have no spaces, relative to the pack):
//   bg3rtx_replay_pack 1
//   pass gi_hit_direct                      a kPasses entry (its shader and descriptor table)
//   defines A=1;B=2                         the shader variant's -D pairs ('-' none)
//   frame N                                 Push::frame of the captured dispatch
//   dispatch X Y Z                          workgroup counts
//   push FILE                               the 256 push-constant bytes
//   buffer S B BYTES [FILE]                 descriptor range of BYTES; FILE fills it from 0 (rest zero)
//   range S B OFFSET FILE [FOFF BYTES]      a further slice at OFFSET (sparse: only what instances use):
//                                           FILE whole, or BYTES of it from byte FOFF (many slices, one file)
//   image S B E FORMAT W H LAYERS MIPS FILE [SOURCE]   VkFormat number; mip-major, each mip all layers
//                                           (a kSampledCube slot: 6 layers, the cube faces in order),
//                                           packed. SOURCE: the game view's VkFormat when the writer read
//                                           the image through a shader (no TRANSFER_SRC usage) and stored
//                                           it decoded in FORMAT (BCn -> R8/RG8/RGBA8/RGBA16F, shader_read_format)
//   omitted S B E                           an image left out (budget, format): bound NaN, so a
//                                           material lookup that needs it fails as "sampling" (6)
//   sampler S B E MAG MIN MIPMODE AU AV AW ANISO MAXLOD   (Vk enums as numbers; ANISO 0 = off)
//   instances FILE COUNT                    VkAccelerationStructureInstanceKHR x COUNT, TLAS order
//                                           (= InstInfo order); accelerationStructureReference is an
//                                           opaque BLAS key, the geometry comes from InstInfo
//   output S B FORMAT W H [FILE]            a storage image the pass writes; FILE: the live result
//   casters FILE INSTANCE TRIS STRIDE       rt_cascade_casters: the cascade-only soup (cut-away roofs,
//                                           off-screen walls) of TLAS instance INSTANCE, TRIS non-indexed
//                                           triangles of STRIDE bytes (position first), in the soup's own
//                                           camera-relative space (the instance's transform moves it into
//                                           this frame's). Its InstInfo says tris 0 (live shaders never read
//                                           its vertices): the replay builds its BLAS from FILE and appends
//                                           FILE to the soup (b6) at caster_patch's base for the PT.
//   note TEXT                               free text (scene, build, keys)
// The geometry is NOT duplicated: each TLAS instance's InstInfo (set 1 b7: base, meta, rec, tris)
// names its non-indexed triangle list in the opaque soup (b6, camera-relative) or the mesh pool
// (b8, object space, kInfoPool), stride meta & 255, exactly as the layer builds the BLAS
// (layer/bvhsplit.cpp tri_geometry); opaque unless kInfoAlpha. So the pack holds those buffers
// (sparse ranges) and the instance array as read at submit.
#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <iterator>
#include <string>
#include <vector>
#include <sstream>

namespace bg3::replay {
inline constexpr int kVersion = 1;
// Geometry budget (tools/frame_replay.md "Size"): the soup / pool are captured whole while the
// never-dropped part stays under it, else as the InstInfo spans.
inline constexpr uint64_t kPackBudgetBytes = 1ull << 30;
// Hard cap (disk budget, default): a pack whose never-dropped part (set 0, buffers, geometry) passes it is
// "over budget" and not written; under it the textures fill the rest up to this cap (fill_budget:
// the hit material's bindings first, smallest first, each image that still fits).
inline constexpr uint64_t kPackHardCapBytes = 2ull << 30;
// The cap the writer uses: ini `replay_pack_cap_mb` (default 2048 = kPackHardCapBytes). A key
// <= 0 keeps the default; the value is clamped to [kPackCapMinMb, kPackCapMaxMb] (a pack is staged
// in host-visible chunks and replayed on one GPU: tools/frame_replay.md "Size").
inline constexpr int kPackCapMinMb = 256;
inline constexpr int kPackCapMaxMb = 16384;
inline uint64_t pack_cap_bytes(int cap_mb) {
  if (cap_mb <= 0) return kPackHardCapBytes;
  if (cap_mb < kPackCapMinMb) cap_mb = kPackCapMinMb;
  if (cap_mb > kPackCapMaxMb) cap_mb = kPackCapMaxMb;
  return uint64_t(cap_mb) << 20;
}
inline constexpr uint64_t kWholeSize = ~0ull;   // VK_WHOLE_SIZE

// kSampledCube: a cube view; its pack image has 6 layers (+X -X +Y -Y +Z -Z, as Vulkan orders them).
enum Kind : uint32_t { kUbo, kSsbo, kSampled2D, kSampled2DArray, kStorage, kSampler, kTlas, kSampledCube };
struct Slot {
  uint32_t set, binding;
  Kind kind;
  uint32_t count;           // descriptor array size
  uint32_t storage_format;  // VkFormat the shader declares (kStorage), 0 otherwise
  const char* name;
};
// VkFormat numbers used by the contracts (no vulkan.h dependency here).
inline constexpr uint32_t kR32Sfloat = 100, kR16G16Sfloat = 83, kR32G32Sfloat = 103, kRgba16Sfloat = 97,
                          kRgba32Sfloat = 109, kRgba32Uint = 107, kRgba8Unorm = 37;

// gi_hit_direct (shaders/gi/gi_trace_rt_hit_direct.hlsl, core/gi.cpp hit_gi_dispatch, base variant):
// set 0 = the GI push set (common/native_hit_gi_bindings.h, as the compiled SPIR-V uses it),
// set 1 = the material AlphaDSL (shaders/rt/rt_common.hlsli + native_hit_material.hlsli).
inline constexpr Slot kGiHitDirect[] = {
    {0, 0, kUbo, 1, 0, "cam"},          {0, 1, kUbo, 1, 0, "vp"},
    {0, 2, kSampled2D, 1, 0, "gNormal"}, {0, 5, kSampled2D, 1, 0, "gDepth"},
    {0, 8, kStorage, 1, kRgba16Sfloat, "giRaw"},        {0, 11, kStorage, 1, kRgba16Sfloat, "prevRadiance"},
    {0, 13, kStorage, 1, kR32Sfloat, "giRawDepth"},     {0, 17, kSampled2D, 1, 0, "gVelocity"},
    {0, 18, kTlas, 1, 0, "gScene"},      {0, 20, kSsbo, 1, 0, "rtStats"},
    {0, 21, kUbo, 1, 0, "lFlags"},       {0, 22, kSsbo, 1, 0, "lScale"},
    {0, 23, kSsbo, 1, 0, "lClusters"},   {0, 24, kSsbo, 1, 0, "lIndex"},
    {0, 25, kSsbo, 1, 0, "lLights"},     {0, 26, kUbo, 1, 0, "lConsts"},
    {0, 29, kUbo, 1, 0, "cam2"},         {0, 40, kStorage, 1, kRgba16Sfloat, "rtOrigin"},
    {0, 46, kStorage, 1, kRgba32Uint, "hdSlots"},
    {1, 0, kSsbo, 1, 0, "gAlphaRec"},    {1, 1, kSsbo, 1, 0, "gAlphaSoup"},
    {1, 2, kSampled2D, 4096, 0, "gAlphaTex"},           {1, 3, kSampled2DArray, 16, 0, "gAlphaCache"},
    {1, 4, kSampler, 1, 0, "gAlphaWrap"}, {1, 5, kSampler, 1, 0, "gAlphaClamp"},
    {1, 6, kSsbo, 1, 0, "gOpaqueSoup"},  {1, 7, kSsbo, 1, 0, "gInstInfo"},
    {1, 8, kSsbo, 1, 0, "gMeshPool"},    {1, 9, kSsbo, 1, 0, "gSceneStamp"},
    {1, 12, kSsbo, 1, 0, "gHitRecords"}, {1, 13, kSsbo, 1, 0, "gHitBasis"},
    {1, 14, kSampled2DArray, 256, 0, "gHitCache"},      {1, 15, kSampler, 64, 0, "gHitSampler"},
    // Sets 2-3: the deferred lighting's inputs (not read by gi_hit_direct; tools/native_ambient.hlsl and
    // tools/pt_reference.hlsl PT_SUN_LUT / PT_ENV read them; tools/frame_replay.md "Lighting inputs").
    // Set 2 = the tiled lighting's own set 1 (f469db78) at its binding numbers, as D-RF binds it
    // (shaders/rt/bg3_ambient.hlsli, native_hit_shade.hlsli): G-buffer albedo / material, AO, the global
    // irradiance (b34/b36) and specular (b35/b37) cubes, the DFG LUT, the local probe records and cubes.
    // Set 3 = the sun set in the shaders/rt/bg3_sun.hlsli layout (atmosphere constants, transmittance
    // LUT, cloud shadows). Sources: kLightingSources. Packs without them bind zero dummies.
    {2, 2, kSampled2D, 1, 0, "lgAlbedo"},  {2, 3, kSampled2D, 1, 0, "lgMaterial"}, {2, 6, kSampled2D, 1, 0, "lgAO"},
    {2, 16, kSampler, 1, 0, "lgSampCube"}, {2, 32, kSampler, 1, 0, "lgSampLut"},   {2, 33, kSsbo, 1, 0, "lgProbes"},
    {2, 34, kSampledCube, 1, 0, "lgIrrCube0"}, {2, 35, kSampledCube, 1, 0, "lgSpecCube0"},
    {2, 36, kSampledCube, 1, 0, "lgIrrCube1"}, {2, 37, kSampledCube, 1, 0, "lgSpecCube1"},
    {2, 38, kSampled2D, 1, 0, "lgDfg"},    {2, 45, kSampledCube, 4096, 0, "lgCubes"},
    {3, 0, kUbo, 1, 0, "lAtmo"},           {3, 1, kSampled2D, 1, 0, "lTransLut"},
    {3, 2, kSampled2D, 1, 0, "lCloudA0"},  {3, 3, kSampled2D, 1, 0, "lCloudA1"},
    {3, 4, kSampled2D, 1, 0, "lCloudB0"},  {3, 5, kSampled2D, 1, 0, "lCloudB1"},  {3, 6, kSampler, 1, 0, "lLinearClamp"},
};
// Where the live writer reads each set 2-3 slot: the game's descriptor (set, binding) bound for the
// tiled lighting dispatch (layer/replay.cpp, at the core's pre-lighting hook; the lighting's set 5
// is its atmosphere set). Set 3 b6 (lLinearClamp) has no game source: the writer declares a
// linear clamp sampler, as core/gi.cpp write_sun_set binds its own.
struct LightingSource { uint32_t set, binding, game_set, game_binding; };
inline constexpr LightingSource kLightingSources[] = {
    {2, 2, 1, 2},   {2, 3, 1, 3},   {2, 6, 1, 6},   {2, 16, 1, 16}, {2, 32, 1, 32}, {2, 33, 1, 33},
    {2, 34, 1, 34}, {2, 35, 1, 35}, {2, 36, 1, 36}, {2, 37, 1, 37}, {2, 38, 1, 38}, {2, 45, 1, 45},
    {3, 0, 5, 0},   {3, 1, 5, 1},   {3, 2, 5, 4},   {3, 3, 5, 5},   {3, 4, 1, 42},  {3, 5, 1, 43},
};
inline const LightingSource* lighting_source(uint32_t set, uint32_t binding) {
  for (const LightingSource& s : kLightingSources) if (s.set == set && s.binding == binding) return &s;
  return nullptr;
}
// Local env probe records (set 2 b33, f469db78 struct _57, 144 bytes): m5 at 136 (bit 0 box, bit 1 a
// diffuse cube pair in bits 3-16 / 17-30, bit 2 a specular pair), m6 at 140 (that specular pair,
// bits 0-15 / 16-31). Each
// pair field is index + 1 (0 = none) of an rgb cube at element 2 i and its weight cube at 2 i + 1 of
// the bindless array b45 (bg3_ambient.hlsli bg3_probe_cube). Returns the b45 elements the records
// name, sorted and unique, below `limit` (the slot's count); `beyond` counts those at or above it.
inline constexpr uint32_t kProbeStride = 144;
inline std::vector<uint32_t> probe_cube_elements(const uint8_t* records, uint64_t bytes, uint32_t limit, uint32_t& beyond) {
  std::vector<uint32_t> out;
  beyond = 0;
  auto u32 = [&](uint64_t at) { uint32_t v = 0; for (int k = 0; k < 4; ++k) v |= uint32_t(records[at + k]) << (8 * k); return v; };
  auto pair = [&](uint32_t field) {
    if (!field) return;
    const uint64_t e = uint64_t(field - 1) * 2;
    for (uint64_t x : {e, e + 1}) { if (x < limit) out.push_back(uint32_t(x)); else ++beyond; }
  };
  for (uint64_t r = 0; records && r + kProbeStride <= bytes; r += kProbeStride) {
    const uint32_t m5 = u32(r + 136), m6 = u32(r + 140);
    if (m5 & 2u) { pair((m5 >> 3) & 16383u); pair((m5 >> 17) & 16383u); }
    if (m5 & 4u) { pair(m6 & 65535u); pair(m6 >> 16); }
  }
  std::sort(out.begin(), out.end());
  out.erase(std::unique(out.begin(), out.end()), out.end());
  return out;
}
struct Pass {
  const char* name;
  const char* shader;   // relative to shaders/
  const Slot* slots;
  uint32_t slot_count, set_count, push_bytes;
};
inline constexpr Pass kPasses[] = {
    {"gi_hit_direct", "gi/gi_trace_rt_hit_direct.hlsl", kGiHitDirect, uint32_t(std::size(kGiHitDirect)), 4, 256},
};
inline const Pass* find_pass(const std::string& name) {
  for (const Pass& p : kPasses) if (name == p.name) return &p;
  return nullptr;
}
inline const Slot* find_slot(const Pass& p, uint32_t set, uint32_t binding) {
  for (uint32_t i = 0; i < p.slot_count; ++i) if (p.slots[i].set == set && p.slots[i].binding == binding) return p.slots + i;
  return nullptr;
}
// InstInfo meta bits (layer/bvhsplit.cpp, shaders/rt/rt_common.hlsli).
inline constexpr uint32_t kInfoPool = 1u << 8, kInfoLegacy = 1u << 9, kInfoAlpha = 1u << 10, kInfoHair = 1u << 12;

// Bytes of a W x H x LAYERS mip chain of `format`; 0 = a format the pack does not carry.
inline uint32_t block_bytes(uint32_t f, uint32_t& block) {
  block = 1;
  switch (f) {
    case 9: case 10: case 13: return 1;                            // R8 UNORM / SNORM / UINT
    case 16: case 17: case 70: case 74: case 76: return 2;         // RG8 UNORM / SNORM, R16 UNORM / UINT / SFLOAT
    case 37: case 38: case 41: case 43: case 44: case 50: case 58: case 64: case 77: case 81: case 83:
    case 98: case 99: case 100: case 122: case 123: return 4;      // RGBA8, RGB10A2, RG16, R32, B10G11R11, E5B9G9R9
    case 97: case 91: case 103: case 102: return 8;                // RGBA16F, RGBA16 UNORM, RG32F, RG32UI
    case 107: case 109: return 16;                                 // RGBA32UI, RGBA32F
    case 131: case 132: case 133: case 134: case 139: case 140: block = 4; return 8;    // BC1, BC4
    case 135: case 136: case 137: case 138: case 141: case 142: case 143: case 144:
    case 145: case 146: block = 4; return 16;                                         // BC2, BC3, BC5, BC6H, BC7
    default: return 0;
  }
}
inline uint64_t image_bytes(uint32_t f, uint32_t w, uint32_t h, uint32_t layers, uint32_t mips) {
  uint32_t block;
  const uint32_t b = block_bytes(f, block);
  uint64_t n = 0;
  for (uint32_t m = 0; m < mips; ++m) {
    const uint32_t mw = w >> m ? w >> m : 1, mh = h >> m ? h >> m : 1;
    n += uint64_t((mw + block - 1) / block) * ((mh + block - 1) / block) * b * layers;
  }
  return n;
}

// ---- shader read (layer/replay.cpp, shaders/replay_image.hlsl) ----
// A game image without TRANSFER_SRC usage cannot be copied, but it is SAMPLED: the writer reads every
// texel through a 2D-array view of the game view's format (Load: no filtering, the decoded value the
// pass's shader would fetch) and stores it in `pack_format`. BCn cannot be read back as raw blocks,
// so it is stored decoded at the precision the decoder delivers to the filter: 8 bits per channel for
// BC1/2/3/7 (sRGB kept as sRGB, so the replay's sampler linearizes and filters as the game's does),
// BC4/BC5 as R8/RG8 (UNORM or SNORM: the values the hardware rounds to 8 bits before the filter, at
// most 0.5/255 off where it keeps more), BC6H as RGBA16F (exact: it decodes to half floats).
// Uncompressed formats keep their bits (BGRA8 is stored as RGBA8, B10G11R11 / E5B9G9R9 as RGBA16F,
// A2B10G10R10 as RGBA16 UNORM). Integer and depth formats are not read this way (omitted "format").
// `swizzled`: the game view has a non-identity component mapping; the read applies it, so the value
// is stored with 4 channels (the replay binds an identity view).
enum Encode : uint32_t { kEncNone, kEncUnorm8, kEncSnorm8, kEncSrgb8, kEncUnorm16, kEncHalf, kEncFloat };
struct ShaderRead { uint32_t pack_format = 0, encode = kEncNone, channels = 0; };
inline uint32_t encode_bits(uint32_t e) { return e == kEncFloat ? 32 : (e == kEncUnorm16 || e == kEncHalf) ? 16 : e ? 8 : 0; }
inline bool shader_read_format(uint32_t f, bool swizzled, ShaderRead& r) {
  auto set = [&](uint32_t pf, uint32_t e, uint32_t c) { r = {pf, e, c}; return true; };
  switch (f) {
    case 9: return swizzled ? set(37, kEncUnorm8, 4) : set(9, kEncUnorm8, 1);           // R8 UNORM
    case 10: return swizzled ? set(38, kEncSnorm8, 4) : set(10, kEncSnorm8, 1);         // R8 SNORM
    case 16: return swizzled ? set(37, kEncUnorm8, 4) : set(16, kEncUnorm8, 2);         // RG8 UNORM
    case 17: return swizzled ? set(38, kEncSnorm8, 4) : set(17, kEncSnorm8, 2);         // RG8 SNORM
    case 37: case 44: return set(37, kEncUnorm8, 4);                                    // RGBA8 / BGRA8 UNORM
    case 38: return set(38, kEncSnorm8, 4);
    case 43: case 50: return set(43, kEncSrgb8, 4);                                     // RGBA8 / BGRA8 SRGB
    case 64: return set(91, kEncUnorm16, 4);                                            // A2B10G10R10 UNORM
    case 70: return swizzled ? set(91, kEncUnorm16, 4) : set(70, kEncUnorm16, 1);       // R16 UNORM
    case 77: return swizzled ? set(91, kEncUnorm16, 4) : set(77, kEncUnorm16, 2);       // RG16 UNORM
    case 91: return set(91, kEncUnorm16, 4);
    case 76: return swizzled ? set(97, kEncHalf, 4) : set(76, kEncHalf, 1);             // R16 SFLOAT
    case 83: return swizzled ? set(97, kEncHalf, 4) : set(83, kEncHalf, 2);             // RG16 SFLOAT
    case 97: case 122: case 123: return set(97, kEncHalf, 4);                           // RGBA16F, B10G11R11, E5B9G9R9
    case 100: return swizzled ? set(109, kEncFloat, 4) : set(100, kEncFloat, 1);        // R32 SFLOAT
    case 103: return swizzled ? set(109, kEncFloat, 4) : set(103, kEncFloat, 2);        // RG32 SFLOAT
    case 109: return set(109, kEncFloat, 4);
    case 131: case 133: case 135: case 137: case 145: return set(37, kEncUnorm8, 4);    // BC1 / BC2 / BC3 / BC7 UNORM
    case 132: case 134: case 136: case 138: case 146: return set(43, kEncSrgb8, 4);     // the same, SRGB
    case 139: return swizzled ? set(37, kEncUnorm8, 4) : set(9, kEncUnorm8, 1);         // BC4 UNORM
    case 140: return swizzled ? set(38, kEncSnorm8, 4) : set(10, kEncSnorm8, 1);        // BC4 SNORM
    case 141: return swizzled ? set(37, kEncUnorm8, 4) : set(16, kEncUnorm8, 2);        // BC5 UNORM
    case 142: return swizzled ? set(38, kEncSnorm8, 4) : set(17, kEncSnorm8, 2);        // BC5 SNORM
    case 143: case 144: return set(97, kEncHalf, 4);                                    // BC6H UFLOAT / SFLOAT
    default: return false;
  }
}
inline bool block_compressed(uint32_t f) { return f >= 131 && f <= 146; }
inline uint32_t shader_read_texel_bytes(const ShaderRead& r) { return r.channels * encode_bits(r.encode) / 8; }
// Texels one thread of replay_image.hlsl encodes: it always writes whole 32-bit words.
inline uint32_t shader_read_per_thread(const ShaderRead& r) { const uint32_t tb = shader_read_texel_bytes(r); return tb >= 4 ? 1 : 4 / tb; }
// One dispatch range of a shader read: texels [first, first + count) of mip `mip` in the packed
// layout (layer-major, rows, x), which is `bytes` of the image's file at `file_offset`. Pieces start
// at a multiple of shader_read_per_thread texels, so each thread's words belong to one piece; the
// staging slot of a piece is rounded up to 4 bytes (the last thread's word).
struct ReadPiece { uint32_t mip; uint64_t first, count, file_offset, bytes; };
inline std::vector<ReadPiece> shader_read_pieces(const ShaderRead& r, uint32_t w, uint32_t h, uint32_t layers, uint32_t mips, uint64_t max_bytes) {
  std::vector<ReadPiece> out;
  const uint64_t tb = shader_read_texel_bytes(r), per = shader_read_per_thread(r);
  if (!tb) return out;
  const uint64_t cap = (std::max)(per, (max_bytes / tb) / per * per);
  uint64_t at = 0;
  for (uint32_t m = 0; m < mips; ++m) {
    const uint64_t mw = w >> m ? w >> m : 1, mh = h >> m ? h >> m : 1, n = mw * mh * layers;
    for (uint64_t first = 0; first < n; first += cap) {
      const uint64_t count = (std::min)(cap, n - first);
      out.push_back({m, first, count, at + first * tb, count * tb});
    }
    at += n * tb;
  }
  return out;
}
// One region of a transfer copy (vkCmdCopyImageToBuffer) of at most `max_bytes`: mip `mip`, layers
// [layer, layer + layers), texel rows [row, row + rows) (a multiple of the block height, the last
// one clipped), `bytes` of the file at `file_offset`. Whole mips when they fit, else layer groups,
// else block rows of one layer. Empty when a single block row is larger than `max_bytes`.
struct CopyPiece { uint32_t mip, layer, layers, row, rows; uint64_t file_offset, bytes; };
inline std::vector<CopyPiece> transfer_pieces(uint32_t f, uint32_t w, uint32_t h, uint32_t layers, uint32_t mips, uint64_t max_bytes) {
  std::vector<CopyPiece> out;
  uint32_t block;
  const uint64_t bb = block_bytes(f, block);
  if (!bb) return out;
  uint64_t at = 0;
  for (uint32_t m = 0; m < mips; ++m) {
    const uint32_t mw = w >> m ? w >> m : 1, mh = h >> m ? h >> m : 1;
    const uint64_t brow = uint64_t((mw + block - 1) / block) * bb, bh = (mh + block - 1) / block, layer_bytes = brow * bh;
    if (layer_bytes * layers <= max_bytes) {
      out.push_back({m, 0, layers, 0, mh, at, layer_bytes * layers});
    } else if (layer_bytes <= max_bytes) {
      const uint32_t group = uint32_t(max_bytes / layer_bytes);
      for (uint32_t l = 0; l < layers; l += group) {
        const uint32_t n = (std::min)(group, layers - l);
        out.push_back({m, l, n, 0, mh, at + layer_bytes * l, layer_bytes * n});
      }
    } else {
      if (brow > max_bytes) return {};
      const uint64_t rows = max_bytes / brow;   // block rows per piece
      for (uint32_t l = 0; l < layers; ++l)
        for (uint64_t r = 0; r < bh; r += rows) {
          const uint64_t n = (std::min)(rows, bh - r);
          const uint32_t row = uint32_t(r * block), texel_rows = (std::min)(uint32_t(n * block), mh - row);
          out.push_back({m, l, 1, row, texel_rows, at + layer_bytes * l + brow * r, brow * n});
        }
    }
    at += layer_bytes * layers;
  }
  return out;
}

struct Buffer { uint32_t set, binding; uint64_t bytes; std::string file; };
struct Range { uint32_t set, binding; uint64_t offset; std::string file; uint64_t file_offset = 0, bytes = 0; };   // bytes 0: whole file
struct Image { uint32_t set, binding, element, format, w, h, layers, mips; std::string file; uint32_t source_format = 0; };   // 0: copied as is
struct Omitted { uint32_t set, binding, element; };
struct Sampler { uint32_t set, binding, element, mag, min, mip, au, av, aw; float aniso, max_lod; };
struct Output { uint32_t set, binding, format, w, h; std::string reference; };
struct Casters { std::string file; uint32_t instance = 0, tris = 0, stride = 0; };   // file empty: none
struct Manifest {
  int version = 0;
  std::string pass, defines, push, instances;
  uint64_t frame = 0;
  uint32_t groups[3] = {0, 0, 0}, instance_count = 0;
  std::vector<Buffer> buffers;
  std::vector<Range> ranges;
  std::vector<Image> images;
  std::vector<Omitted> omitted;
  std::vector<Sampler> samplers;
  std::vector<Output> outputs;
  Casters casters;
  std::vector<std::string> notes;
};

// The variant defines a pack records (the live request's "A=1;B=2", core/gi.cpp pipe_defines): only those
// the pass contract carries the bindings for. RT_CASCADE_MASKS (rt_cascade_casters: GI rays trace 0x01, so
// they never meet the cascade instance) changes no binding. The others (OMM scene, native hair / scalp,
// tile set, instanced rows) bind more than the contract holds: left out, as every pack before the casters,
// and returned in `dropped` for the pack's note.
inline std::string contract_defines(const std::string& live, std::string* dropped = nullptr) {
  static const char* const kKept[] = {"RT_CASCADE_MASKS"};
  std::string out;
  if (dropped) dropped->clear();
  for (size_t a = 0; a < live.size();) {
    size_t e = live.find(';', a);
    if (e == std::string::npos) e = live.size();
    const std::string d = live.substr(a, e - a), name = d.substr(0, d.find('='));
    bool keep = false;
    for (const char* k : kKept) keep |= name == k;
    if (!d.empty()) {
      std::string& to = keep ? out : (dropped ? *dropped : out);
      if (keep || dropped) to += (to.empty() ? "" : ";") + d;
    }
    a = e + 1;
  }
  return out;
}

// ---- rt_cascade_casters in the pack (the "casters" directive) ----
// The caster instance's bytes: TRIS x 3 vertices of STRIDE (layer/bvhsplit.cpp builds its BLAS from them).
inline uint64_t caster_bytes(uint32_t tris, uint32_t stride) { return uint64_t(tris) * 3 * stride; }
// A usable casters directive: an instance of the TLAS, triangles, a stride with the position (12 B) that
// the shaders' InstInfo stride field (meta & 255) can hold, and the file of exactly that size.
inline bool casters_valid(const Casters& c, uint32_t instance_count, uint64_t file_bytes) {
  return !c.file.empty() && c.instance < instance_count && c.tris && c.stride >= 12 && c.stride <= 255 && c.stride % 4 == 0 &&
         file_bytes == caster_bytes(c.tris, c.stride);
}
// The replay's InstInfo for the caster instance (frame_replay): the live entry is {0, stride, 0, 0} (no
// vertices, never read live). The replay appends the casters to the soup (b6) at the first multiple of 16
// at or past `soup_bytes` (the soup buffer as the pack binds it) and points the entry there, so rt_hit_normal
// and native_hit_position see real triangles (kInfoPool and kInfoAlpha stay clear: solid, no material
// record). Returns the soup size with the casters appended; 0 (info untouched) when the entry is not
// {*, stride, 0, 0} of this stride or the result passes 4 GB (a 32-bit InstInfo base).
inline uint64_t caster_patch(uint32_t* info, uint32_t count, const Casters& c, uint64_t soup_bytes) {
  if (c.instance >= count || !c.tris) return 0;
  uint32_t* e = info + size_t(c.instance) * 4;
  if ((e[1] & 255u) != c.stride || (e[1] & (kInfoPool | kInfoLegacy | kInfoAlpha | kInfoHair)) || e[2] || e[3]) return 0;
  const uint64_t base = (soup_bytes + 15) & ~uint64_t(15), end = base + caster_bytes(c.tris, c.stride);
  if (end > 0xFFFFFFFFull) return 0;
  e[0] = uint32_t(base);
  e[3] = c.tris;
  return end;
}

// What a pack carries of the lighting inputs (sets 2-3; older packs: none): the native ambient
// replica needs the irradiance cube and the DFG LUT, the PT's sky the specular cube, its LUT sun
// the atmosphere constants and the transmittance LUT.
struct LightingInputs { bool ambient = false, sky = false, sun_lut = false, probes = false; uint32_t images = 0, probe_cubes = 0; };
inline LightingInputs lighting_inputs(const Manifest& m) {
  LightingInputs r;
  bool irr = false, dfg = false, lut = false, atmo = false;
  for (const Image& i : m.images) {
    if (i.set < 2) continue;
    ++r.images;
    if (i.set == 2 && i.binding == 34) irr = true;
    if (i.set == 2 && i.binding == 38) dfg = true;
    if (i.set == 2 && i.binding == 35) r.sky = true;
    if (i.set == 2 && i.binding == 45) ++r.probe_cubes;
    if (i.set == 3 && i.binding == 1) lut = true;
  }
  for (const Buffer& b : m.buffers) {
    if (b.set == 3 && b.binding == 0 && !b.file.empty()) atmo = true;
    if (b.set == 2 && b.binding == 33 && !b.file.empty()) r.probes = true;
  }
  r.ambient = irr && dfg;
  r.sun_lut = lut && atmo;
  return r;
}

// Parses manifest text; false with `error` set on the first malformed line.
inline bool parse(const std::string& text, Manifest& m, std::string& error) {
  std::istringstream lines(text);
  std::string line;
  int number = 0;
  while (std::getline(lines, line)) {
    ++number;
    if (!line.empty() && line.back() == '\r') line.pop_back();
    if (line.empty() || line[0] == '#') continue;
    std::istringstream t(line);
    std::string k;
    t >> k;
    bool ok = true;
    if (k == "bg3rtx_replay_pack") ok = bool(t >> m.version);
    else if (k == "pass") ok = bool(t >> m.pass);
    else if (k == "defines") { ok = bool(t >> m.defines); if (m.defines == "-") m.defines.clear(); }
    else if (k == "frame") ok = bool(t >> m.frame);
    else if (k == "dispatch") ok = bool(t >> m.groups[0] >> m.groups[1] >> m.groups[2]);
    else if (k == "push") ok = bool(t >> m.push);
    else if (k == "instances") ok = bool(t >> m.instances >> m.instance_count);
    else if (k == "buffer") { Buffer b{}; ok = bool(t >> b.set >> b.binding >> b.bytes); t >> b.file; m.buffers.push_back(b); }
    else if (k == "range") {
      Range r{}; ok = bool(t >> r.set >> r.binding >> r.offset >> r.file);
      if (ok && (t >> r.file_offset)) ok = bool(t >> r.bytes) && r.bytes; else r.file_offset = 0;
      m.ranges.push_back(r);
    }
    else if (k == "image") {
      Image i{}; ok = bool(t >> i.set >> i.binding >> i.element >> i.format >> i.w >> i.h >> i.layers >> i.mips >> i.file);
      if (ok && !(t >> i.source_format)) i.source_format = 0;
      ok = ok && i.w && i.h && i.layers && i.mips && image_bytes(i.format, i.w, i.h, i.layers, i.mips);
      m.images.push_back(i);
    } else if (k == "omitted") { Omitted o{}; ok = bool(t >> o.set >> o.binding >> o.element); m.omitted.push_back(o); }
    else if (k == "sampler") {
      Sampler s{}; ok = bool(t >> s.set >> s.binding >> s.element >> s.mag >> s.min >> s.mip >> s.au >> s.av >> s.aw >> s.aniso >> s.max_lod);
      m.samplers.push_back(s);
    } else if (k == "output") { Output o{}; ok = bool(t >> o.set >> o.binding >> o.format >> o.w >> o.h); t >> o.reference; m.outputs.push_back(o); }
    else if (k == "casters") ok = m.casters.file.empty() && bool(t >> m.casters.file >> m.casters.instance >> m.casters.tris >> m.casters.stride) && m.casters.tris;
    else if (k == "note") { std::string rest; std::getline(t, rest); m.notes.push_back(rest); }
    else ok = false;
    if (!ok) { error = "manifest line " + std::to_string(number) + ": " + line; return false; }
  }
  if (m.version != kVersion) { error = "not a version " + std::to_string(kVersion) + " replay pack"; return false; }
  if (!find_pass(m.pass)) { error = "unknown pass '" + m.pass + "'"; return false; }
  if (m.push.empty() || !m.groups[0] || !m.groups[1] || !m.groups[2]) { error = "push or dispatch missing"; return false; }
  return true;
}
// ---- writer side (layer/replay.cpp, tools/frame_replay.cpp --synthesize) ----

// The manifest text of `m` (parse(serialize(m)) == m for every field the grammar carries).
inline std::string serialize(const Manifest& m) {
  std::ostringstream o;
  o.precision(9);
  o << "bg3rtx_replay_pack " << (m.version ? m.version : kVersion) << "\n";
  o << "pass " << m.pass << "\n";
  o << "defines " << (m.defines.empty() ? "-" : m.defines) << "\n";
  o << "frame " << m.frame << "\n";
  for (const auto& n : m.notes) o << "note" << (n.empty() || n[0] == ' ' ? "" : " ") << n << "\n";
  o << "dispatch " << m.groups[0] << " " << m.groups[1] << " " << m.groups[2] << "\n";
  o << "push " << m.push << "\n";
  for (const auto& b : m.buffers) o << "buffer " << b.set << " " << b.binding << " " << b.bytes << (b.file.empty() ? "" : " " + b.file) << "\n";
  for (const auto& r : m.ranges) {
    o << "range " << r.set << " " << r.binding << " " << r.offset << " " << r.file;
    if (r.bytes) o << " " << r.file_offset << " " << r.bytes;
    o << "\n";
  }
  for (const auto& i : m.images)
    o << "image " << i.set << " " << i.binding << " " << i.element << " " << i.format << " " << i.w << " " << i.h << " " << i.layers << " "
      << i.mips << " " << i.file << (i.source_format ? " " + std::to_string(i.source_format) : "") << "\n";
  for (const auto& x : m.omitted) o << "omitted " << x.set << " " << x.binding << " " << x.element << "\n";
  for (const auto& s : m.samplers)
    o << "sampler " << s.set << " " << s.binding << " " << s.element << " " << s.mag << " " << s.min << " " << s.mip << " " << s.au << " "
      << s.av << " " << s.aw << " " << s.aniso << " " << s.max_lod << "\n";
  if (!m.instances.empty()) o << "instances " << m.instances << " " << m.instance_count << "\n";
  if (!m.casters.file.empty())
    o << "casters " << m.casters.file << " " << m.casters.instance << " " << m.casters.tris << " " << m.casters.stride << "\n";
  for (const auto& x : m.outputs)
    o << "output " << x.set << " " << x.binding << " " << x.format << " " << x.w << " " << x.h << (x.reference.empty() ? "" : " " + x.reference) << "\n";
  return o.str();
}

// A byte span of a buffer. merge_spans sorts and joins spans that overlap or are at most `gap`
// bytes apart (the gap is copied too: fewer, larger copies), so each byte is written once.
struct Span { uint64_t offset, bytes; };
inline void merge_spans(std::vector<Span>& v, uint64_t gap) {
  std::vector<Span> in;
  for (const Span& s : v) if (s.bytes) in.push_back(s);
  std::sort(in.begin(), in.end(), [](const Span& a, const Span& b) { return a.offset < b.offset; });
  v.clear();
  for (const Span& s : in) {
    if (!v.empty() && s.offset <= v.back().offset + v.back().bytes + gap) {
      const uint64_t end = std::max(v.back().offset + v.back().bytes, s.offset + s.bytes);
      v.back().bytes = end - v.back().offset;
    } else {
      v.push_back(s);
    }
  }
}
inline uint64_t span_bytes(const std::vector<Span>& v) { uint64_t n = 0; for (const Span& s : v) n += s.bytes; return n; }

// The bytes a buffer descriptor binds (only these are captured, never the whole VkBuffer):
// `range` (kWholeSize = to the end) clipped to the buffer; 0 when the offset is past its end.
inline uint64_t bound_bytes(uint64_t buffer_size, uint64_t offset, uint64_t range) {
  if (offset >= buffer_size) return 0;
  const uint64_t rest = buffer_size - offset;
  return range == kWholeSize ? rest : std::min(range, rest);
}

// A descriptor bound inside a buffer range another descriptor already captured (split BVH:
// gAlphaSoup b1 binds the same whole soup as gOpaqueSoup b6) reuses that capture's file slices
// instead of copying the bytes again. `src`: the captured slices (explicit bytes) of the descriptor
// at [src_base, src_base + src_bytes) of the buffer. Appends the same bytes as slices of (set,
// binding) bound at [dst_base, dst_base + dst_bytes), clipped to it. False (nothing appended) when
// the destination is not inside the source's bound range: it is then captured on its own.
inline bool alias_ranges(const std::vector<Range>& src, uint64_t src_base, uint64_t src_bytes, uint32_t set, uint32_t binding,
                         uint64_t dst_base, uint64_t dst_bytes, std::vector<Range>& out) {
  if (!dst_bytes || dst_base < src_base || dst_base + dst_bytes > src_base + src_bytes) return false;
  const uint64_t hi_d = dst_base + dst_bytes;
  for (const Range& r : src) {
    const uint64_t a = src_base + r.offset, lo = std::max(a, dst_base), hi = std::min(a + r.bytes, hi_d);
    if (!r.bytes || hi <= lo) continue;
    out.push_back({set, binding, lo - dst_base, r.file, r.file_offset + (lo - a), hi - lo});
  }
  return true;
}

// The spans of the soup (b6) and pool (b8) that a frame's InstInfo entries (16 B each: base, meta,
// rec, tris; layer/bvhsplit.cpp) name, as the BLAS builds read them: tris * 3 vertices of stride
// meta & 255 from base, opaque soup unless kInfoPool. Legacy soup, hair AABBs and empty entries name
// nothing (frame_replay leaves them out of the TLAS too).
inline void instinfo_spans(const uint32_t* info, uint32_t count, std::vector<Span>& soup, std::vector<Span>& pool) {
  for (uint32_t i = 0; i < count; ++i) {
    const uint32_t base = info[i * 4], meta = info[i * 4 + 1], tris = info[i * 4 + 3], stride = meta & 255u;
    if (!tris || stride < 12 || (meta & (kInfoLegacy | kInfoHair))) continue;
    (meta & kInfoPool ? pool : soup).push_back({base, uint64_t(tris) * 3 * stride});
  }
}

// Texture priority of a set-1 image binding (higher is kept first, fill_budget): the hit material
// samples gHitCache (b14) and the texture table gAlphaTex (b2: its VT page tables and 2D detail /
// iridescent / tile-set views; the writer cannot tell those entries from the alpha test's, and the
// whole table is small), so both rank above the alpha test's own tile caches (b3). An image bound
// by several bindings takes the highest. The lighting inputs (sets 2-3: G-buffer, AO, global cubes,
// LUTs, clouds; tens of MB) rank first; the local probe cubes (set 2 b45, as many as the records
// name) rank between the hit material and the alpha caches, so they never displace the VT hit cache
// (the S8 pack keeps its 2054 MB cache under replay_pack_cap_mb=3300; probes fill what is left).
inline constexpr uint32_t kPriorityAlphaCache = 1, kPriorityProbes = 2, kPriorityHit = 3, kPriorityLighting = 4;
inline uint32_t texture_priority(uint32_t set, uint32_t binding) {
  if (set == 2) return binding == 45 ? kPriorityProbes : kPriorityLighting;
  if (set == 3) return kPriorityLighting;
  if (set != 1) return 0;
  return binding == 14 || binding == 2 ? kPriorityHit : binding == 3 ? kPriorityAlphaCache : 0u;
}
// Two views whose formats store the same raw bytes (same texel block size and footprint, e.g. a
// BC7 UNORM and a BC7 SRGB view of one image): one transfer copy serves both.
inline bool raw_compatible(uint32_t a, uint32_t b) {
  uint32_t ba = 0, bb = 0;
  const uint32_t sa = block_bytes(a, ba), sb = block_bytes(b, bb);
  return sa && sa == sb && ba == bb;
}

// The budget (tools/frame_replay.md "Size"): `fixed` bytes are never dropped (set 0, geometry,
// records). The droppable images are kept highest `priority` first (none given: all equal),
// within one priority smallest first, each one that still fits; an image larger than what is
// left is dropped and the next ones still fill the rest (2026-11-01 S8: dropping lowest priority
// first, largest first, removed all 4098 table and cache elements to make room for one cache
// larger than the free space, which was then dropped too). Returns the indices dropped, sorted;
// `over` (all dropped) when even the fixed part does not fit (the pack is then not written).
inline std::vector<size_t> fill_budget(uint64_t fixed, const std::vector<uint64_t>& droppable, uint64_t budget, bool& over,
                                       const std::vector<uint32_t>& priority = {}) {
  std::vector<size_t> order(droppable.size()), dropped;
  for (size_t i = 0; i < order.size(); ++i) order[i] = i;
  auto pri = [&](size_t i) { return i < priority.size() ? priority[i] : 0u; };
  std::stable_sort(order.begin(), order.end(), [&](size_t a, size_t b) {
    return pri(a) != pri(b) ? pri(a) > pri(b) : droppable[a] < droppable[b];
  });
  over = fixed > budget;
  uint64_t total = fixed;
  for (size_t i : order) {
    if (!over && droppable[i] <= budget - total) total += droppable[i];
    else dropped.push_back(i);
  }
  std::sort(dropped.begin(), dropped.end());
  return dropped;
}
}  // namespace bg3::replay
