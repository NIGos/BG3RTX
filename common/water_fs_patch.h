#pragma once
// M5.3 water composition: the SPIR-V patch of an audited forward water FS (common/
// water_compose.h has the record it writes and why). Pure: no Vulkan; header-only so the
// test (tests/water_compose_test.cpp) runs it on the dumped game module.
//
// What the patch adds to the module (and nothing else; every original instruction is kept):
//   - OpCapability Int64 and PhysicalStorageBufferAddresses, OpExtension
//     "SPV_KHR_physical_storage_buffer", the memory model's addressing Logical ->
//     PhysicalStorageBuffer64 (the module has no pointers of that class before: no effect);
//   - OpExecutionMode EarlyFragmentTests: with side effects, fragments that fail the depth test
//     would otherwise still run and store. Valid only because the FS neither discards nor
//     writes depth (checked: refused otherwise), so the colour result is unchanged;
//   - types it lacks (uint64, the buffer's runtime array / block / pointers) and constants;
//   - before main's single OpReturn: the record of common/water_compose.h, stored at
//     kHeaderBytes + (y * pitch + x) * kRecordBytes through the baked buffer address.
// The values are the FS's own SSA results, named by a table per audited FS (FsIds), derived
// from the module's disassembly (captures/alpha_cache/<hash>.glsl): each id is checked to be a
// result of main with the expected type, so a table that does not fit the module is refused
// (never a wrong store). Dominance of the end of main is the table's claim: every value is
// defined at the top level of main or is the phi of a top-level merge / loop header (checked
// offline with spirv-val on the patched dump).
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <map>
#include <vector>

#include "water_compose.h"
#include "shader_variant.h"

namespace bg3::water {

// SSA ids of one audited FS (spirv-cross names them _<id>).
struct FsIds {
  uint64_t fs;          // the module hash (layer hash64 of the SPIR-V)
  uint32_t a;           // float: opacity _784 (foam/depth fade * _48._m0)
  uint32_t fog;         // vec4: froxel fog _4096 (w transmittance, xyz in-scatter)
  uint32_t k;           // float: exposure _3799 = set 2 b0 _m0[0] * 100
  uint32_t X;           // vec3: atmosphere transmittance _2063
  uint32_t Ks;          // vec3: the probe term _3379 after the local-probe loop
  uint32_t f0;          // vec3: F0 _3303
  uint32_t dfg;         // vec4: the DFG LUT sample _3352 at (N.V, roughness)
  uint32_t N;           // vec3: the shading normal _1207 (camera-relative world)
  uint32_t C;           // vec3: the colour _4335 before max(0)
  uint32_t rough;       // float: roughness _1480 (with the FS's specular AA)
  uint32_t pos;         // Input variable vec3: camera-relative world position _5
  // M5.4 (record words 8-9 with `sun`: C without the sun term, and lum of the term): the FS's sun
  // term in Lw = (sun_d + sun_s) * sun_m, the
  // operands of its OpFAdd chain before the probe term (spirv-dis --raw-id of the dumps):
  uint32_t sun_d;       // vec3: sun diffuse * N.L, added to the local lights' diffuse sum
  uint32_t sun_s;       // vec3: sun specular * N.L, added to the local lights' specular sum
  uint32_t sun_m;       // float: the factor of (diffuse + specular) sums, mix(1, 1, b26 _m1) = 1
  // M5.4b (record words 10-23 with `ls`): the local lights. The FS walks three loops over its froxel's
  // list (points, spots: b12 records; projectors: b16), each carrying (specular, diffuse) sums as loop
  // header phis; the points + spots sums are the spot loop's header phis (they dominate the end of
  // main), the totals the projector loop's (the operands of the sun's OpFAdd).
  uint32_t ls_d12;      // vec3: diffuse sum after the point and spot loops
  uint32_t ls_s12;      // vec3: specular sum after the point and spot loops
  uint32_t ls_d;        // vec3: diffuse sum after the projector loop (Ld of Lw)
  uint32_t ls_s;        // vec3: specular sum after the projector loop (Ls of Lw)
  uint32_t ls_dif;      // vec3: the diffuse colour of the loops' Burley term
  uint32_t ls_f0;       // vec3: the F0 of the loops' Schlick term (vec3(0.16 spec^2): grey)
  uint32_t ls_chan;     // uint: set 1 b0 _m1, the light channel (loaded at the top level of main)
  uint32_t ls_z;        // Input variable float: location 0, the froxel depth (log2-sliced)
  // M5.5 step 1 (record words 24-25 with `refr`): the FS's refraction sample, the vec4 texture() of its
  // colour copy (set 2 b4) that the end of main mixes: C = mix(Refr.xyz, surface, a). Defined at the top
  // level of main just before C (it is C's operand), so it dominates the stores.
  uint32_t refr;        // vec4: _4332 / _4530 / _3668
  // M5.4c (record words +0..+1 after the form's last with `gsh`): the game's own sun visibility Vg, the
  // scalar factor of Lsun of the FS that sample the game's cascade shadow map (tools/water_audit.py
  // game_sun_shadow derives and checks it: every Dref sample reaches C only through Vg, and Vg only
  // through Lsun's products, so the sun term D = Vg D_u exactly). 0: the FS has no game sun shadow (Vg 1).
  uint32_t vg = 0;      // float: the phi(1, max(0, pow(PCF, 2.2))) of the cascade block (_1365 / _1869)
};

// The audited table. 7b44a567 FLUID_Water_Plane_flowmap_huge_PixelLighting (S1 docks), from
// captures/alpha_cache/7b44a567771ecbec.glsl lines 1443-1975 (2026-11-01): the final colour
// _4335 = mix(_4332 (refraction copy), fma(fma(_3901, _2063, _2062 e) + foam, _4096.www,
// _4096.xyz), _784) with _3901 = direct + (_3382 + _3379) * _3799: dC/dKs = _784 _4096.w _2063 _3799.
// The S8 Water Queens House FS (alpha_cache glsl = spirv-cross of the dumps, read 2026-11-01) end
// the same way, the same terms under other ids:
//   99c1fcef ExtraWaves: _4533 = mix(_4530, fma(fma(_4099, _2261, _2260 e) + foam, _4294.www,
//     _4294.xyz), _809), _4099 = direct + (_3580 + _3577) * _3997, Ks _3577 = probes * (_3501 _3550.x
//     + _3550.y) after the loop, N.V = dot(normalize(-_5), _1294), roughness _1678;
//   c4e92ccd NoCaustics_Waves: _3671 = mix(_3668, fma(fma(_3322, _1484, _1483 e), _3510.www,
//     _3510.xyz), _425) (no foam term), _3322 = direct + (_2803 + _2800) * _3220, F0 _2724, DFG _2773,
//     N _665, roughness _899.
// Audited water FS outside the table keep the game's pipeline (counted by the layer as "no variant").
// M5.4 sun ids (2026-11-01, spirv-dis --raw-id of the three dumps): in each FS Lw =
// ((Ld + Sd) + (Ls + Ss)) * m + (Kd + Ks) k with Sd = (Lsun * diffuse BRDF) * N.L and Ss = (Lsun *
// specular BRDF) * N.L, Lsun = sun colour (atmosphere) * cloud shadow * exposure: the sun has no
// cascade or other geometric visibility on the water (no shadow-map sample in the FS; its only
// textureGather is the local lights' atlas b28). 7b44a567: %3889 %3892 * %3898 (Lsun %3803 =
// %2064 * (%2236 clouds * b0)), 99c1fcef: %4087 %4090 * %4096, c4e92ccd: %3310 %3313 * %3319
// (Lsun %3224 = %1485 * (%1657 clouds * b0)).
// M5.4b local ids (2026-11-01, spirv-dis --raw-id of the three dumps; glsl = spirv-cross of them):
//   c4e92ccd: point loop header phis %1696 (spec) %1698 (diff), spot loop %1993 %1995 = phi(%1696,
//     %1698, ...), projector loop %2278 %2280 = phi(%1993, %1995, ...), Lw's `%3311 = %2280 + sun_d`;
//     diffuse colour %886 and F0 %889 (the Burley / Schlick operands in all three loops), channel
//     %885 = OpLoad (OpAccessChain %48 (set 1 b0) %int_1), z = Input %4 (log2(_4) * b26 _m21 + _m22);
//   99c1fcef: spot %2770 %2772, projector %3055 %3057, dif %1665, F0 %1668, channel %1664 (%49 set 1 b0);
//   7b44a567: spot %2572 %2574, projector %2857 %2859, dif %1467, F0 %1470, channel %1466.
// The loops' N, V and roughness are the record's (N, normalize(-_5), roughness ids above).
// M5.5 refraction sample (glsl of the dumps, 2026-11-01): 7b44a567 `vec4 _4332 = texture(sampler2D(_22
// (set 2 b4), _51), clamp(uv + wave offset ...))`, 99c1fcef `_4530` (_23 = set 2 b4), c4e92ccd `_3668`
// (_22 = set 2 b4); C = mix(_4332.xyz, ..., vec3(_784)) etc.: the a of the table is the mix weight.
inline constexpr FsIds kFsIds[] = {
    {0x7b44a567771ecbecull, 784, 4096, 3799, 2063, 3379, 3303, 3352, 1207, 4335, 1480, 5, 3889, 3892, 3898,
     2574, 2572, 2859, 2857, 1467, 1470, 1466, 4, 4332},
    {0x99c1fcef82b253c1ull, 809, 4294, 3997, 2261, 3577, 3501, 3550, 1294, 4533, 1678, 5, 4087, 4090, 4096,
     2772, 2770, 3057, 3055, 1665, 1668, 1664, 4, 4530},
    {0xc4e92ccdb08dd777ull, 425, 3510, 3220, 1484, 2800, 2724, 2773, 665, 3671, 899, 5, 3310, 3313, 3319,
     1995, 1993, 2280, 2278, 886, 889, 885, 4, 3668},
    // Pre-audit (2026-11-01, offline): derived by tools/water_audit.py from the raw-id disassembly of each dump
    // (the structure above, step by step; it reproduces the three tables above id for id, and water_audit_test
    // keeps every row equal to its derivation). flowmap_PixelLighting and huge_PL_V2 (audited for capture),
    // flowmap_Brine, huge_PL_SphereMask, and PL_noCaustics (Flymm Cargos basement): its instanced and static
    // FS have the same ids (one body, another engine CB layout: the channel at +4 / +72). No shadow-map sample in
    // any of them (the sun term has no game visibility, as above). PL_noCaustics_Shadows 06e66a6f and huge_PL_Shadow
    // ced49ee3 (the same structure, but Lsun carries the game's cascade shadow Vg) are mapped below with their Vg
    // (M5.4c). Not mapped: Debris 7813e701 (a debris mask scales the probe term and the direct factor),
    // flowmap 1d4bff2e, flowmap_noCaustics 857ccb90, Water_Plane 68068cab, OnlyNormals 4af3450e (no light loops).
    {0xfda628a5f89e5b6eull, 724, 3890, 3593, 1857, 3173, 3097, 3146, 1043, 4084, 1274, 5, 3683, 3686, 3692,
     2368, 2366, 2653, 2651, 1261, 1264, 1260, 4, 4081},
    {0x64d9cb772d595ce4ull, 776, 4128, 3831, 2095, 3411, 3335, 3384, 1211, 4368, 1512, 5, 3921, 3924, 3930,
     2606, 2604, 2891, 2889, 1499, 1502, 1498, 4, 4365},
    {0xd44c390bcbb9eea5ull, 430, 3688, 3398, 1662, 2978, 2902, 2951, 682, 3850, 1079, 9, 3488, 3491, 3497,
     2173, 2171, 2458, 2456, 1066, 1069, 1065, 4, 3847},
    {0xfb7cc2ea451d3e08ull, 795, 4043, 3746, 2010, 3326, 3250, 3299, 1196, 4280, 1427, 5, 3836, 3839, 3845,
     2521, 2519, 2806, 2804, 1414, 1417, 1413, 4, 4277},
    {0x2bd6cb9de954c2f9ull, 418, 3512, 3222, 1486, 2802, 2726, 2775, 670, 3674, 901, 9, 3312, 3315, 3321,
     1997, 1995, 2282, 2280, 888, 891, 887, 4, 3671},
    {0x269de9047e587ea2ull, 418, 3512, 3222, 1486, 2802, 2726, 2775, 670, 3674, 901, 9, 3312, 3315, 3321,
     1997, 1995, 2282, 2280, 888, 891, 887, 4, 3671},
    // M5.4c (2026-11-01, offline, water_audit game_sun_shadow): the two families that sample the game's cascade
    // shadow map, with their Vg. PL_noCaustics_Shadows 06e66a6f (drawn at the S3 camp): Lsun _3707 = _1967 *
    // (_2140 * b0), _2140 = _1365 * cloud layers (phi(p, p * (1 - c)) twice), _1365 = _38._m3 > 0 ? max(0,
    // pow(PCF of 7 Dref samples over the cascades, 2.2)) : 1. huge_PL_Shadow ced49ee3: Lsun _4209 = _2469 *
    // (_2642 * b0), _2642 = _1869 * clouds, _1869 the same cascade block. Everything else as the rows above.
    {0x06e66a6fbd88a6b3ull, 569, 3993, 3703, 1966, 3283, 3207, 3256, 756, 4155, 1381, 6, 3793, 3796, 3802,
     2478, 2476, 2763, 2761, 1368, 1371, 1367, 4, 4152, 1365},
    {0xced49ee370940201ull, 791, 4502, 4205, 2468, 3785, 3709, 3758, 1214, 4741, 1885, 5, 4295, 4298, 4304,
     2980, 2978, 3265, 3263, 1872, 1875, 1871, 4, 4738, 1869},
};
inline constexpr uint32_t kFsIdsCount = uint32_t(sizeof kFsIds / sizeof kFsIds[0]);

enum class PatchStatus : uint32_t {
  kApplied = 0,
  kNotSpirv,       // bad header or instruction stream
  kNotFragment,    // not exactly one Fragment entry point
  kAlreadyPatched, // has the capability or the addressing already
  kSideEffects,    // discards, demotes, writes depth, or already has EarlyFragmentTests mode
  kIdMismatch,     // a table id is missing or of another type
  kNoReturn,       // main does not end with a single OpReturn
};
inline const char* patch_status_name(PatchStatus s) {
  static const char* k[] = {"applied", "not SPIR-V", "not a fragment shader", "already patched",
                            "side effects (discard/depth)", "id mismatch", "no single return"};
  return k[uint32_t(s) < 7 ? uint32_t(s) : 0];
}

namespace fsp {
// Opcodes and enums (SPIR-V 1.3 unified1 headers).
enum : uint32_t {
  OpName = 5, OpMemberName = 6, OpExtension = 10, OpExtInstImport = 11, OpExtInst = 12, OpMemoryModel = 14,
  OpEntryPoint = 15, OpExecutionMode = 16, OpCapability = 17, OpTypeBool = 20, OpTypeInt = 21, OpTypeFloat = 22,
  OpTypeVector = 23, OpTypeRuntimeArray = 29, OpTypeStruct = 30, OpTypePointer = 32, OpConstant = 43,
  OpConstantComposite = 44, OpConstantNull = 46, OpFunction = 54, OpFunctionEnd = 56, OpVariable = 59, OpLoad = 61, OpStore = 62,
  OpAccessChain = 65, OpDecorate = 71, OpMemberDecorate = 72, OpCompositeConstruct = 80,
  OpCompositeExtract = 81, OpConvertFToU = 109, OpConvertUToF = 112, OpConvertUToPtr = 120, OpBitcast = 124, OpIAdd = 128,
  OpFAdd = 129, OpFSub = 131, OpIMul = 132, OpFMul = 133, OpFDiv = 136, OpVectorTimesScalar = 142, OpDot = 148, OpLogicalOr = 166, OpLogicalAnd = 167,
  OpSelect = 169, OpBitwiseOr = 197, OpBitwiseAnd = 199,
  OpULessThan = 176, OpPhi = 245, OpLabel = 248, OpKill = 252, OpReturn = 253, OpTerminateInvocation = 4416,
  OpDemoteToHelperInvocation = 5380,
  CapInt64 = 11, CapPsb = 5347, AddrPsb64 = 5348, ScInput = 1, ScPsb = 5349, ModeEarlyTests = 9,
  DecBlock = 2, DecArrayStride = 6, DecBuiltIn = 11, DecOffset = 35, BuiltInFragCoord = 15, BuiltInFragDepth = 22,
  MemAligned = 2, GlslFma = 50, GlslPackUnorm2x16 = 57, GlslPackHalf2x16 = 58, GlslLength = 66, GlslNMax = 80,
};
inline uint32_t op_word(uint32_t op, uint32_t n) { return (n << 16) | op; }
inline void emit(std::vector<uint32_t>& v, uint32_t op, std::initializer_list<uint32_t> args) {
  v.push_back(op_word(op, uint32_t(args.size() + 1)));
  v.insert(v.end(), args.begin(), args.end());
}
inline void emit_string(std::vector<uint32_t>& v, uint32_t op, const char* s) {
  const size_t len = std::strlen(s) + 1, words = (len + 3) / 4;
  v.push_back(op_word(op, uint32_t(words + 1)));
  const size_t at = v.size();
  v.resize(at + words, 0u);
  std::memcpy(v.data() + at, s, len);
}
}  // namespace fsp

// Patches `code` (words) into `out`. addr: the device address of the record buffer, capacity
// its records (common/water_compose.h buffer_bytes): both baked in, so every store stays inside
// the buffer whatever the header holds (pitch is read from it: uninitialised memory can only
// misplace records, which the ownership check rejects). sun (M5.4): the record of
// kRecordWordsSun words with the sun term; false emits exactly the M5.3 module. ls (M5.4b): the
// record of kRecordWordsLs words, the sun's plus the local-light words (implies sun); false emits
// exactly the M5.4 module. refr (M5.5 step 1): the record of kRecordWordsRefr words, the local-light
// record's plus the refraction words (implies ls); false emits exactly the M5.4b module. gsh (M5.4c, implies
// sun): two more words after the form's last, the sun light the game's shadow map removed Dg = D_u - D and Vg
// (common/water_compose.h kGshBytes); false emits exactly the module without them, except that an FS with a
// Vg then stores lum(D) = 0 (the sun step skips it: its game shadow stays, never shadowed twice).
inline PatchStatus patch_fs(const uint32_t* code, size_t words, const FsIds& ids, uint64_t addr, uint32_t capacity,
                            std::vector<uint32_t>& out, bool sun = false, bool ls = false, bool refr = false,
                            bool gsh = false, bool surface = false) {
  // Only this audited pool family has the clean-water mask and column inputs below.
  surface = surface && ids.fs == 0xc4e92ccdb08dd777ull;
  using namespace fsp;
  out.clear();
  if (refr) ls = true;
  if (ls || gsh) sun = true;
  if (!code || words < 5 || code[0] != 0x07230203u) return PatchStatus::kNotSpirv;
  struct Ins { size_t at; uint32_t op, n; };
  std::vector<Ins> ins;
  for (size_t p = 5; p < words;) {
    const uint32_t n = code[p] >> 16, op = code[p] & 0xffffu;
    if (!n || p + n > words) return PatchStatus::kNotSpirv;
    ins.push_back({p, op, n});
    p += n;
  }
  uint32_t bound = code[3];
  uint32_t entry = 0, entries = 0, glsl = 0, frag_coord = 0;
  size_t last_cap = 0, last_ext = 0, mem_model = 0, last_mode = 0, first_func = 0, ret = 0;
  uint32_t t_bool = 0, t_uint = 0, t_u64 = 0, t_float = 0, t_v2 = 0, t_v3 = 0, t_v4 = 0, t_bool3 = 0;
  std::map<uint32_t, uint32_t> result_type;   // result id -> type id, inside main
  std::map<uint32_t, uint32_t> var_ptr;       // Input variables -> pointer type
  std::map<uint32_t, std::pair<uint32_t, uint32_t>> ptr_of;   // pointer type -> (storage class, pointee)
  bool in_main = false, side = false, returns_ok = true;
  uint32_t main_returns = 0;
  for (size_t i = 0; i < ins.size(); ++i) {
    const Ins& x = ins[i];
    const uint32_t* a = code + x.at;
    switch (x.op) {
      case OpCapability:
        if (a[1] == CapPsb) return PatchStatus::kAlreadyPatched;
        last_cap = x.at + x.n;
        break;
      case OpExtension: last_ext = x.at + x.n; break;
      case OpExtInstImport:
        if (x.n >= 3 && std::strncmp(reinterpret_cast<const char*>(a + 2), "GLSL.std.450", 12) == 0) glsl = a[1];
        break;
      case OpMemoryModel:
        if (a[1] != 0) return PatchStatus::kAlreadyPatched;   // Logical only
        mem_model = x.at;
        break;
      case OpEntryPoint:
        ++entries;
        if (x.n >= 3 && a[1] == 4) entry = a[2];   // Fragment
        else return PatchStatus::kNotFragment;
        break;
      case OpExecutionMode:
        if (x.n >= 3 && a[2] == ModeEarlyTests) side = true;
        last_mode = x.at + x.n;
        break;
      case OpDecorate:
        if (x.n >= 4 && a[2] == DecBuiltIn && a[3] == BuiltInFragCoord) frag_coord = a[1];
        if (x.n >= 4 && a[2] == DecBuiltIn && a[3] == BuiltInFragDepth) side = true;
        break;
      case OpTypeBool: t_bool = a[1]; break;
      case OpTypeInt:
        if (a[2] == 32 && a[3] == 0) t_uint = a[1];
        if (a[2] == 64 && a[3] == 0) t_u64 = a[1];
        break;
      case OpTypeFloat:
        if (a[2] == 32) t_float = a[1];
        break;
      case OpTypeVector:
        if (a[2] == t_bool && a[3] == 3) t_bool3 = a[1];
        if (t_float && a[2] == t_float) {
          if (a[3] == 2) t_v2 = a[1];
          if (a[3] == 3) t_v3 = a[1];
          if (a[3] == 4) t_v4 = a[1];
        }
        break;
      case OpTypePointer: ptr_of[a[1]] = {a[2], a[3]}; break;
      case OpVariable:
        if (!in_main && x.n >= 4 && a[3] == ScInput) var_ptr[a[2]] = a[1];
        break;
      case OpFunction:
        if (!first_func) first_func = x.at;
        in_main = a[2] == entry;
        break;
      case OpFunctionEnd: in_main = false; break;
      case OpKill: case OpTerminateInvocation: case OpDemoteToHelperInvocation: side = true; break;
      case OpReturn:
        if (in_main) ++main_returns, ret = x.at;
        break;
      default: break;
    }
    // Result ids with a result type inside main: (type, id) as words 1 and 2 of value ops. Ops
    // without a result are skipped; the first definition wins (a later operand in word 2 of an
    // op not listed here could only turn a match into a refusal, never the reverse).
    static constexpr uint32_t kNoResult[] = {OpFunction, OpLabel, OpStore, OpReturn, OpVariable, 8 /*OpLine*/,
                                             63 /*OpCopyMemory*/, 99 /*OpImageWrite*/, 224, 225, 228,
                                             246 /*OpLoopMerge*/, 247 /*OpSelectionMerge*/, 249 /*OpBranch*/,
                                             250 /*OpBranchConditional*/, 251 /*OpSwitch*/, 254, 317};
    bool value = in_main && x.n >= 3;
    for (uint32_t no : kNoResult) value = value && x.op != no;
    if (value) result_type.emplace(a[2], a[1]);
  }
  (void)returns_ok;
  if (entries != 1 || !entry) return PatchStatus::kNotFragment;
  if (side) return PatchStatus::kSideEffects;
  if (main_returns != 1 || !ret || !first_func || !mem_model || !last_cap) return PatchStatus::kNoReturn;
  // The instruction before the return of main must not be a terminator (it is a store).
  if (!glsl || !frag_coord || !t_uint || !t_float || !t_v2 || !t_v3 || !t_v4 || !t_bool) return PatchStatus::kIdMismatch;
  auto is = [&](uint32_t id, uint32_t type) { auto it = result_type.find(id); return it != result_type.end() && it->second == type; };
  if (!is(ids.a, t_float) || !is(ids.fog, t_v4) || !is(ids.k, t_float) || !is(ids.X, t_v3) || !is(ids.Ks, t_v3) ||
      !is(ids.f0, t_v3) || !is(ids.dfg, t_v4) || !is(ids.N, t_v3) || !is(ids.C, t_v3) || !is(ids.rough, t_float))
    return PatchStatus::kIdMismatch;
  if (sun && (!is(ids.sun_d, t_v3) || !is(ids.sun_s, t_v3) || !is(ids.sun_m, t_float))) return PatchStatus::kIdMismatch;
  if (ls && (!is(ids.ls_d12, t_v3) || !is(ids.ls_s12, t_v3) || !is(ids.ls_d, t_v3) || !is(ids.ls_s, t_v3) ||
             !is(ids.ls_dif, t_v3) || !is(ids.ls_f0, t_v3) || !is(ids.ls_chan, t_uint)))
    return PatchStatus::kIdMismatch;
  if (refr && !is(ids.refr, t_v4)) return PatchStatus::kIdMismatch;
  if (sun && ids.vg && !is(ids.vg, t_float)) return PatchStatus::kIdMismatch;
  auto input_of = [&](uint32_t var, uint32_t pointee) {
    auto v = var_ptr.find(var);
    if (v == var_ptr.end()) return false;
    auto p = ptr_of.find(v->second);
    return p != ptr_of.end() && p->second.first == ScInput && p->second.second == pointee;
  };
  if (!input_of(ids.pos, t_v3) || !input_of(frag_coord, t_v4)) return PatchStatus::kIdMismatch;
  if (ls && !input_of(ids.ls_z, t_float)) return PatchStatus::kIdMismatch;

  // ---- new declarations ----
  auto id = [&]() { return bound++; };
  std::vector<uint32_t> types;   // inserted before the first function
  if (!t_u64) t_u64 = id(), emit(types, OpTypeInt, {t_u64, 64, 0});
  const uint32_t t_arr = id(), t_blk = id(), t_pblk = id(), t_puint = id();
  emit(types, OpTypeRuntimeArray, {t_arr, t_uint});
  emit(types, OpTypeStruct, {t_blk, t_arr});
  emit(types, OpTypePointer, {t_pblk, ScPsb, t_blk});
  emit(types, OpTypePointer, {t_puint, ScPsb, t_uint});
  const uint32_t c_addr = id();
  emit(types, OpConstant, {t_u64, c_addr, uint32_t(addr), uint32_t(addr >> 32)});
  uint32_t c_u[kRecordWords + 1];   // uint constants 0 .. 8
  for (uint32_t k = 0; k <= kRecordWords; ++k) c_u[k] = id(), emit(types, OpConstant, {t_uint, c_u[k], k});
  const uint32_t c_hdr = id(), c_one = id(), c_zero3 = id(), c_cap = id();
  emit(types, OpConstant, {t_uint, c_hdr, kHeaderBytes / 4});
  emit(types, OpConstant, {t_uint, c_cap, capacity});
  emit(types, OpConstant, {t_float, c_one, 0x3f800000u});
  emit(types, OpConstantNull, {t_v3, c_zero3});
  // M5.4: the record stride, word offsets 8, 9 and the luminance weights (after every M5.3
  // declaration: sun = false is unchanged).
  uint32_t c_stride = c_u[kRecordWords], c_w8 = 0, c_w9 = 0, c_lum = 0;
  if (sun) {
    c_w9 = id(), c_stride = id();
    emit(types, OpConstant, {t_uint, c_w9, 9});
    emit(types, OpConstant, {t_uint, c_stride, (refr ? kRecordWordsRefr : ls ? kRecordWordsLs : kRecordWordsSun) +
                                                   (gsh ? kGshWords : 0u)});
    c_w8 = c_u[kRecordWords];   // 8
    const uint32_t lr = id(), lg = id(), lb = id();
    emit(types, OpConstant, {t_float, lr, 0x3e59b3d0u});   // 0.2126
    emit(types, OpConstant, {t_float, lg, 0x3f371759u});   // 0.7152
    emit(types, OpConstant, {t_float, lb, 0x3d93dd98u});   // 0.0722
    c_lum = id();
    emit(types, OpConstantComposite, {t_v3, c_lum, lr, lg, lb});
  }
  // M5.4b: word offsets 10..23 and the channel mask 31 (after every M5.4 declaration: ls = false
  // is unchanged).
  uint32_t c_wl[kRecordWordsLs] = {}, c_31 = 0;
  if (ls) {
    for (uint32_t k = kRecordWordsSun; k < kRecordWordsLs; ++k) c_wl[k] = id(), emit(types, OpConstant, {t_uint, c_wl[k], k});
    c_31 = id();
    emit(types, OpConstant, {t_uint, c_31, 31});
  }
  // M5.5: word offsets 24, 25 (after every M5.4b declaration: refr = false is unchanged).
  uint32_t c_w24 = 0, c_w25 = 0;
  if (refr) {
    c_w24 = id(), c_w25 = id();
    emit(types, OpConstant, {t_uint, c_w24, 24});
    emit(types, OpConstant, {t_uint, c_w25, 25});
  }
  // M5.4c (after every M5.5 declaration: an FS without Vg and gsh = false are unchanged): a game-shadowed
  // FS without the M5.4c words stores lum(D) = 0 (no sun term: the sun step skips it and its game shadow
  // stays, never shadowed twice); with them, the word offsets of the two words after the form's last.
  uint32_t c_f0 = 0, c_wg0 = 0, c_wg1 = 0;
  if (sun && ids.vg && !gsh) c_f0 = id(), emit(types, OpConstantNull, {t_float, c_f0});
  if (gsh) {
    const uint32_t wg = refr ? kRecordWordsRefr : ls ? kRecordWordsLs : kRecordWordsSun;
    c_wg0 = id(), c_wg1 = id();
    emit(types, OpConstant, {t_uint, c_wg0, wg});
    emit(types, OpConstant, {t_uint, c_wg1, wg + 1});
  }
  uint32_t surface_spec=0, surface_zero=0, surface_one3=0, surface_half=0, surface_bool3=0;
  if (surface) {
    surface_spec=id(); surface_zero=id(); surface_one3=id(); surface_half=id();
    surface_bool3=t_bool3;
    if (!surface_bool3) surface_bool3=id(),emit(types,OpTypeVector,{surface_bool3,t_bool,3});
    emit(types, 49 /*OpSpecConstantFalse*/, {t_bool,surface_spec});
    emit(types, OpConstantNull, {t_float,surface_zero});
    emit(types, OpConstantComposite, {t_v3,surface_one3,c_one,c_one,c_one});
    emit(types, OpConstant, {t_float,surface_half,0x3f000000u});
  }
  std::vector<uint32_t> decos;
  if(surface) emit(decos,OpDecorate,{surface_spec,1 /*SpecId*/,shader_variant::kSpecIdWaterSurfaceFresnel});
  emit(decos, OpDecorate, {t_arr, DecArrayStride, 4});
  emit(decos, OpDecorate, {t_blk, DecBlock});
  emit(decos, OpMemberDecorate, {t_blk, 0, DecOffset, 0});

  // ---- the record, before main's return ----
  std::vector<uint32_t> body;
  auto ext = [&](uint32_t type, uint32_t inst, std::initializer_list<uint32_t> args) {
    const uint32_t r = id();
    std::vector<uint32_t> w{type, r, glsl, inst};
    w.insert(w.end(), args.begin(), args.end());
    body.push_back(op_word(OpExtInst, uint32_t(w.size() + 1)));
    body.insert(body.end(), w.begin(), w.end());
    return r;
  };
  auto op2 = [&](uint32_t op, uint32_t type, std::initializer_list<uint32_t> args) {
    const uint32_t r = id();
    std::vector<uint32_t> w{type, r};
    w.insert(w.end(), args.begin(), args.end());
    body.push_back(op_word(op, uint32_t(w.size() + 1)));
    body.insert(body.end(), w.begin(), w.end());
    return r;
  };
  // Exact audited diffuse products and their matching Schlick F(L.H). The loop's
  // diffuse accumulation is postponed until F exists, within the same basic block.
  struct DirectSplit { uint32_t product, accum, fresnel; };
  const DirectSplit splits[]={{1947,1948,1958},{2232,2233,2243},{2614,2615,2625},
                              {2679,2680,2690},{3258,0,3287}};
  std::map<size_t,std::vector<uint32_t>> surface_insert;
  std::map<uint32_t,uint32_t> surface_remap;
  uint32_t clean=0, clean3=0, surface_diagnostic=0, coverage=0, clean_weight=0;
  std::map<uint32_t,uint32_t> foam_mix_remap;
  std::vector<uint32_t> native_function;
  uint32_t native_entry=0;
  if(surface) {
    // A second, unmodified evaluation is the authored foam scattering layer.
    // Only this audited module is cloned. No capture/record side effect is cloned.
    std::map<uint32_t,uint32_t> remap;
    remap[entry]=native_entry=id();
    for(const auto& r:result_type) remap[r.first]=id();
    bool main=false;
    for(const Ins& x:ins) {
      const uint32_t* w=code+x.at;
      if(x.op==OpFunction) main=w[2]==entry;
      if(main && (x.op==OpLabel || x.op==OpVariable)) remap[w[x.op==OpLabel?1:2]]=id();
      if(x.op==OpFunctionEnd)main=false;
    }
    // Literal operand positions must never be remapped as ids.
    auto operand_id=[](uint32_t op,size_t j) {
      if(op==OpFunction)return j==1||j==2||j==4;
      if(op==OpVariable)return j==1||j==2||j==4;
      if(op==OpLoad)return j<=3;
      if(op==OpStore)return j<=2;
      if(op==OpExtInst)return j!=4;
      if(op==79)return j<=4; // VectorShuffle
      if(op==81)return j<=3; // CompositeExtract
      if(op==82)return j<=4; // CompositeInsert
      if(op==87||op==88)return j!=5; // image operands mask
      if(op==96)return j!=6; // gather operands mask
      if(op==246)return j<=2;
      if(op==247)return j==1;
      if(op==251)return j<=2||(j%2)==0;
      return true;
    };
    main=false;
    for(const Ins& x:ins) {
      const uint32_t* w=code+x.at;
      if(x.op==OpFunction)main=w[2]==entry;
      if(main) {
        native_function.push_back(w[0]);
        for(size_t j=1;j<x.n;++j) {
          auto r=remap.find(w[j]);
          native_function.push_back(operand_id(x.op,j)&&r!=remap.end()?r->second:w[j]);
        }
      }
      if(x.op==OpFunctionEnd)main=false;
      if(x.op==OpDecorate && remap.count(w[1])) {
        std::vector<uint32_t> d(w+1,w+x.n);d[0]=remap[w[1]];
        decos.push_back(w[0]);decos.insert(decos.end(),d.begin(),d.end());
      }
    }
  }
  if(surface) {
    for(uint32_t v: {724u,741u,742u,751u,791u,808u,809u,814u,855u,872u,873u,878u,2803u,3323u})
      if(!is(v,v<2803u?t_float:t_v3)) return PatchStatus::kIdMismatch;
    for(const auto& q:splits)
      if(!is(q.product,t_v3)||!is(q.fresnel,t_v3)||(q.accum&&!is(q.accum,t_v3))) return PatchStatus::kIdMismatch;
    for(const Ins& x:ins) {
      const uint32_t* w=code+x.at;
      if(x.n<3) continue;
      if(w[2]==751 || w[2]==814 || w[2]==878) {
        foam_mix_remap[w[2]]=op2(OpSelect,t_float,{surface_spec,surface_zero,w[2]});
        if(w[2]!=878) {surface_insert[x.at+x.n]=body;body.clear();}
      }
      if(w[2]==878) {
        // Native colour weight is coverage. Keep it before zeroing the three
        // independent material mixes for the clean evaluation (no unblending).
        clean=surface_spec;
        clean3=op2(OpCompositeConstruct,surface_bool3,{clean,clean,clean});
        coverage=751;
        clean_weight=op2(OpFSub,t_float,{c_one,coverage});
        auto uint_constant = [&](uint32_t value) {
          const uint32_t r=id(); emit(types,OpConstant,{t_uint,r,value}); return r;
        };
        // Diagnostic bits occupy only the existing validity half-word. The
        // marker distinguishes this provisioned family from native FS stores.
        surface_diagnostic=uint_constant(kSurfaceMarker);
        auto reason = [&](uint32_t condition, uint32_t bit) {
          surface_diagnostic=op2(OpBitwiseOr,t_uint,{surface_diagnostic,
              op2(OpSelect,t_uint,{condition,uint_constant(bit),c_u[0]})});
        };
        reason(op2(168 /*OpLogicalNot*/,t_bool,{surface_spec}),kSurfaceFsOff);
        // Eight upward-rounded bits are telemetry only. All radiance and
        // replacement coefficients use the original full precision coverage.
        const uint32_t c255=id();emit(types,OpConstant,{t_float,c255,0x437f0000u});
        const uint32_t bin=op2(OpConvertFToU,t_uint,{ext(t_float,9 /*Ceil*/,{op2(OpFMul,t_float,{coverage,c255})})});
        surface_diagnostic=op2(OpBitwiseOr,t_uint,{surface_diagnostic,
            op2(196 /*ShiftLeftLogical*/,t_uint,{bin,uint_constant(6u)})});
        // Factors of the exact pre-floor products: raw = flow * edge/depth.
        // These are source observations, not additional rejection predicates;
        // a zero multiplier may still make the final foam contribution absent.
        const uint32_t factor_sources[][4]={{724,791,855,kSurfaceFlow},{741,808,872,kSurfaceEdge}};
        for(const auto& factors : factor_sources) {
          uint32_t nonzero=op2(182 /*OpFOrdNotEqual*/,t_bool,{factors[0],surface_zero});
          for(uint32_t source : {factors[1],factors[2]})
            nonzero=op2(OpLogicalOr,t_bool,{nonzero,op2(182,t_bool,{source,surface_zero})});
          reason(nonzero,factors[3]);
        }
        surface_insert[x.at+x.n]=body; body.clear();
      }
      for(const auto& q:splits) if(w[2]==q.fresnel) {
        if(!clean) return PatchStatus::kIdMismatch;
        const uint32_t col=op2(OpVectorTimesScalar,t_v3,{op2(OpFSub,t_v3,{surface_one3,q.fresnel}),ids.a});
        const uint32_t factor=op2(OpSelect,t_v3,{clean3,col,surface_one3});
        const uint32_t weighted=op2(OpFMul,t_v3,{q.product,factor});
        if(q.accum) {
          for(const Ins& y:ins) if(y.n>=3 && code[y.at+2]==q.accum && code[y.at]==op_word(OpExtInst,8)) {
            std::vector<uint32_t> a(code+y.at,code+y.at+y.n);
            for(size_t j=5;j<a.size();++j) if(a[j]==q.product)a[j]=weighted;
            body.insert(body.end(),a.begin(),a.end());
          }
        } else surface_remap[q.product]=weighted;
        surface_insert[x.at+x.n]=body; body.clear();
      }
    }
    if(!clean || surface_insert.size()!=8) return PatchStatus::kIdMismatch;
  }
  const uint32_t fc = op2(OpLoad, t_v4, {frag_coord});
  const uint32_t ux = op2(OpConvertFToU, t_uint, {op2(OpCompositeExtract, t_float, {fc, 0})});
  const uint32_t uy = op2(OpConvertFToU, t_uint, {op2(OpCompositeExtract, t_float, {fc, 1})});
  const uint32_t base = op2(OpConvertUToPtr, t_pblk, {c_addr});
  const uint32_t pitch = op2(OpLoad, t_uint, {op2(OpAccessChain, t_puint, {base, c_u[0], c_u[0]}), MemAligned, 4});
  const uint32_t idx = op2(OpIAdd, t_uint, {op2(OpIMul, t_uint, {uy, pitch}), ux});
  const uint32_t ok = op2(OpLogicalAnd, t_bool, {op2(OpULessThan, t_bool, {ux, pitch}), op2(OpULessThan, t_bool, {idx, c_cap})});
  const uint32_t slot = op2(OpSelect, t_uint, {ok, idx, c_cap});
  const uint32_t w0 = op2(OpIAdd, t_uint, {op2(OpIMul, t_uint, {slot, c_stride}), c_hdr});
  // G = X * (a * T), E = F0.x * DFG.x + DFG.y, A = G * E, B = G * k * Ks (common/water_compose.h:
  // the traced radiance is in the lighting's units, already the probe's times k).
  const uint32_t T = op2(OpCompositeExtract, t_float, {ids.fog, 3});
  const uint32_t aT0 = op2(OpFMul, t_float, {surface ? op2(OpSelect,t_float,{clean,c_one,ids.a}):ids.a, T});
  const uint32_t aT = surface ? op2(OpFMul,t_float,{aT0,op2(OpSelect,t_float,{clean,clean_weight,c_one})}) : aT0;
  const uint32_t G = op2(OpVectorTimesScalar, t_v3, {ids.X, aT});
  const uint32_t Gk = op2(OpVectorTimesScalar, t_v3, {ids.X, op2(OpFMul, t_float, {aT, ids.k})});
  const uint32_t E = ext(t_float, GlslFma, {op2(OpCompositeExtract, t_float, {ids.f0, 0}),
                                            op2(OpCompositeExtract, t_float, {ids.dfg, 0}),
                                            op2(OpCompositeExtract, t_float, {ids.dfg, 1})});
  const uint32_t A = op2(OpVectorTimesScalar, t_v3, {G, E});
  const uint32_t B = op2(OpFMul, t_v3, {Gk, ids.Ks});
  uint32_t effectiveC=ids.C, surfaceRest=0;
  if(surface) {
    // DFG E stays the probe's sole interface weight. Air additive terms retain
    // their native coverage and ordering; only the water column is partitioned.
    const uint32_t trans=op2(OpFSub,t_float,{c_one,E});
    const uint32_t column=op2(OpFMul,t_float,{ids.a,trans});
    const uint32_t rw=op2(OpFMul,t_float,{op2(OpFSub,t_float,{c_one,ids.a}),trans});
    const uint32_t refr3=op2(79 /*OpVectorShuffle*/,t_v3,{ids.refr,ids.refr,0,1,2});
    const uint32_t fog3=op2(79,t_v3,{ids.fog,ids.fog,0,1,2});
    const uint32_t air=op2(OpVectorTimesScalar,t_v3,{op2(OpFAdd,t_v3,{op2(OpVectorTimesScalar,t_v3,{3323,T}),fog3}),ids.a});
    const uint32_t env=op2(OpVectorTimesScalar,t_v3,{op2(OpFAdd,t_v3,{op2(OpVectorTimesScalar,t_v3,{2803,column}),ids.Ks}),ids.k});
    const uint32_t local=op2(OpVectorTimesScalar,t_v3,{op2(OpFAdd,t_v3,{ids.ls_d,ids.ls_s}),ids.sun_m});
    const uint32_t gx=op2(OpVectorTimesScalar,t_v3,{ids.X,T});
    surfaceRest=op2(OpFAdd,t_v3,{op2(OpFAdd,t_v3,{op2(OpFMul,t_v3,{op2(OpFAdd,t_v3,{local,env}),gx}),air}),op2(OpVectorTimesScalar,t_v3,{refr3,rw})});
    const uint32_t sunlit=op2(OpVectorTimesScalar,t_v3,{op2(OpFAdd,t_v3,{ids.sun_d,ids.sun_s}),ids.sun_m});
    const uint32_t colour=op2(OpFAdd,t_v3,{surfaceRest,op2(OpFMul,t_v3,{sunlit,gx})});
    // Uniform specialization branch: helper retains all native foam shading.
    // Its sole side effect is the original colour output, read before overwrite.
    const uint32_t native_label=id(), merge_label=id();
    emit(body,247 /*SelectionMerge*/,{merge_label,0});
    emit(body,250 /*BranchConditional*/,{surface_spec,native_label,merge_label});
    emit(body,OpLabel,{native_label});
    uint32_t void_type=0;
    for(const Ins& x:ins)if(x.op==OpFunction&&code[x.at+2]==entry)void_type=code[x.at+1];
    op2(57 /*FunctionCall*/,void_type,{native_entry});
    emit(body,249 /*Branch*/,{merge_label});emit(body,OpLabel,{merge_label});
    const uint32_t native4=op2(OpLoad,t_v4,{11});
    const uint32_t native3=op2(79,t_v3,{native4,native4,0,1,2});
    const uint32_t mixed=op2(OpFAdd,t_v3,{op2(OpVectorTimesScalar,t_v3,{native3,coverage}),op2(OpVectorTimesScalar,t_v3,{colour,clean_weight})});
    // Exact endpoints avoid 0*Inf and preserve native foam at c=1.
    const uint32_t full=op2(180,t_bool,{coverage,c_one});
    const uint32_t full3=op2(OpCompositeConstruct,surface_bool3,{full,full,full});
    const uint32_t empty=op2(180,t_bool,{coverage,surface_zero});
    const uint32_t empty3=op2(OpCompositeConstruct,surface_bool3,{empty,empty,empty});
    effectiveC=op2(OpSelect,t_v3,{clean3,op2(OpSelect,t_v3,{full3,native3,op2(OpSelect,t_v3,{empty3,colour,mixed})}),ids.C});
    surfaceRest=op2(OpSelect,t_v3,{full3,native3,op2(OpSelect,t_v3,{empty3,surfaceRest,op2(OpFAdd,t_v3,{op2(OpVectorTimesScalar,t_v3,{native3,coverage}),op2(OpVectorTimesScalar,t_v3,{surfaceRest,clean_weight})})})});
    // Keep the original alpha. The record and attachment use exactly this same C.
    const uint32_t final=op2(OpCompositeConstruct,t_v4,{op2(OpCompositeExtract,t_float,{effectiveC,0}),op2(OpCompositeExtract,t_float,{effectiveC,1}),op2(OpCompositeExtract,t_float,{effectiveC,2}),3675});
    const uint32_t zero4=id();emit(types,OpConstantNull,{t_v4,zero4});
    emit(body,OpStore,{11,ext(t_v4,GlslNMax,{final,zero4})});
  }
  const uint32_t C = ext(t_v3, GlslNMax, {effectiveC, c_zero3});
  const uint32_t P = op2(OpLoad, t_v3, {ids.pos});
  const uint32_t t = ext(t_float, GlslLength, {P});
  auto c = [&](uint32_t v3, uint32_t i) { return op2(OpCompositeExtract, t_float, {v3, i}); };
  auto half2 = [&](uint32_t x, uint32_t y) { return ext(t_uint, GlslPackHalf2x16, {op2(OpCompositeConstruct, t_v2, {x, y})}); };
  uint32_t v[kRecordWords];
  v[0] = half2(c(A, 0), c(A, 1));
  v[1] = half2(c(A, 2), c(B, 0));
  v[2] = half2(c(B, 1), c(B, 2));
  v[3] = half2(c(ids.N, 0), c(ids.N, 1));
  v[4] = half2(c(ids.N, 2), c(C, 0));
  v[5] = half2(c(C, 1), c(C, 2));
  v[6] = op2(OpBitcast, t_uint, {t});
  v[7] = ext(t_uint, GlslPackUnorm2x16, {op2(OpCompositeConstruct, t_v2, {surface ? op2(OpSelect,t_float,{clean,surface_half,c_one}):c_one, ids.rough})});
  if (surface) {
    // White-furnace coefficients, including the independent foam layer.
    const uint32_t tr=op2(OpFSub,t_float,{c_one,E});
    const uint32_t part=op2(OpFAdd,t_float,{coverage,op2(OpFMul,t_float,{clean_weight,
      op2(OpFAdd,t_float,{E,op2(OpFAdd,t_float,{op2(OpFMul,t_float,{tr,ids.a}),op2(OpFMul,t_float,{tr,op2(OpFSub,t_float,{c_one,ids.a})})})})})});
    const uint32_t err=ext(t_float,4 /*FAbs*/,{op2(OpFSub,t_float,{part,c_one})});
    const uint32_t tol=id();emit(types,OpConstant,{t_float,tol,0x35800000u}); // 8 float eps
    uint32_t bad=op2(186 /*FOrdGreaterThan*/,t_bool,{err,tol});
    for(uint32_t value : {part,E,coverage,ids.a}) {
      const uint32_t nonfinite=op2(OpLogicalOr,t_bool,{op2(156 /*IsNan*/,t_bool,{value}),op2(157 /*IsInf*/,t_bool,{value})});
      bad=op2(OpLogicalOr,t_bool,{bad,nonfinite});
      if(value!=part)bad=op2(OpLogicalOr,t_bool,{bad,op2(OpLogicalOr,t_bool,{op2(184 /*FOrdLessThan*/,t_bool,{value,surface_zero}),op2(186,t_bool,{value,c_one})})});
    }
    const uint32_t badbit=id();emit(types,OpConstant,{t_uint,badbit,1u});
    surface_diagnostic=op2(OpBitwiseOr,t_uint,{surface_diagnostic,op2(OpSelect,t_uint,{bad,badbit,c_u[0]})});
    const uint32_t high_mask=id(); emit(types,OpConstant,{t_uint,high_mask,0xffff0000u});
    v[7]=op2(OpBitwiseOr,t_uint,{op2(OpBitwiseAnd,t_uint,{v[7],high_mask}),surface_diagnostic});
  }
  for (uint32_t k = 0; k < kRecordWords; ++k) {
    const uint32_t at = op2(OpIAdd, t_uint, {w0, c_u[k]});
    const uint32_t p = op2(OpAccessChain, t_puint, {base, c_u[0], at});
    emit(body, OpStore, {p, v[k], MemAligned, 4});
  }
  if (sun) {
    // D = G * (sun_d + sun_s) * sun_m, R = C - D in f32 (the FS's colour with its sun term removed):
    // w8 half2(R.r, R.g), w9 half2(R.b, lum(D)). R, not D: where the sun dominates C (the glint, up
    // to ~1000), C - D taken in the compose from the B10G11R11 target (6/5-bit mantissa) and a half D
    // lost the rest (refraction, probe, ambient: a few % of C) in the target's rounding (m5w4: black
    // and bright specks in the shadowed pool, 1213 clamped records per frame). R is exact to half.
    const uint32_t S = op2(OpVectorTimesScalar, t_v3, {op2(OpFAdd, t_v3, {ids.sun_d, ids.sun_s}), ids.sun_m});
    const uint32_t D = op2(OpFMul, t_v3, {G, S});
    const uint32_t R0 = op2(OpFSub, t_v3, {ids.C, D});
    const uint32_t R = surface ? op2(OpSelect,t_v3,{clean3,surfaceRest,R0}) : R0;
    const uint32_t lumD = op2(OpDot, t_float, {D, c_lum});
    const uint32_t s8 = half2(c(R, 0), c(R, 1)), s9 = half2(c(R, 2), c_f0 ? c_f0 : lumD);
    const uint32_t p8 = op2(OpAccessChain, t_puint, {base, c_u[0], op2(OpIAdd, t_uint, {w0, c_w8})});
    emit(body, OpStore, {p8, s8, MemAligned, 4});
    const uint32_t p9 = op2(OpAccessChain, t_puint, {base, c_u[0], op2(OpIAdd, t_uint, {w0, c_w9})});
    emit(body, OpStore, {p9, s9, MemAligned, 4});
    if (ls) {   // M5.4b words 10..23 (common/water_compose.h kRecordBytesLs)
      const uint32_t F = op2(OpVectorTimesScalar, t_v3, {G, ids.sun_m});
      const uint32_t L12 = op2(OpFAdd, t_v3, {ids.ls_d12, ids.ls_s12});
      const uint32_t Lt = op2(OpFAdd, t_v3, {ids.ls_d, ids.ls_s});
      const uint32_t box = op2(OpDot, t_float, {op2(OpFMul, t_v3, {F, op2(OpFSub, t_v3, {Lt, L12})}), c_lum});
      const uint32_t ch = op2(OpFAdd, t_float, {op2(OpConvertUToF, t_float, {op2(OpBitwiseAnd, t_uint, {ids.ls_chan, c_31})}), c_one});
      const uint32_t z = op2(OpLoad, t_float, {ids.ls_z});
      uint32_t lw[kRecordWordsLs] = {};
      lw[10] = half2(c(F, 0), c(F, 1));
      lw[11] = half2(c(F, 2), c(ids.ls_f0, 0));
      lw[12] = half2(c(L12, 0), c(L12, 1));
      lw[13] = half2(c(L12, 2), ch);
      const uint32_t dif = surface ? op2(OpVectorTimesScalar,t_v3,{ids.ls_dif,op2(OpSelect,t_float,{clean,ids.a,c_one})}) : ids.ls_dif;
      lw[14] = half2(c(dif, 0), c(dif, 1));
      lw[15] = half2(c(dif, 2), box);
      lw[16] = op2(OpBitcast, t_uint, {z});
      for (uint32_t i = 0; i < 3; ++i) {
        lw[17 + i] = op2(OpBitcast, t_uint, {c(P, i)});
        lw[20 + i] = op2(OpBitcast, t_uint, {c(ids.N, i)});
      }
      lw[23] = op2(OpBitcast, t_uint, {ids.rough});
      for (uint32_t k = kRecordWordsSun; k < kRecordWordsLs; ++k) {
        const uint32_t p = op2(OpAccessChain, t_puint, {base, c_u[0], op2(OpIAdd, t_uint, {w0, c_wl[k]})});
        emit(body, OpStore, {p, lw[k], MemAligned, 4});
      }
      if (refr) {   // M5.5 words 24, 25: w24 half2(Refr.r, Refr.g), w25 half2(Refr.b, 1 - a)
        const uint32_t rw0 = op2(OpFSub, t_float, {c_one, ids.a});
        const uint32_t rw = surface ? op2(OpFMul,t_float,{rw0,op2(OpSelect,t_float,{clean,op2(OpFSub,t_float,{c_one,E}),c_one})}) : rw0;
        const uint32_t wr = surface ? op2(OpFMul,t_float,{rw,op2(OpSelect,t_float,{clean,clean_weight,c_one})}) : rw;
        const uint32_t s24 = half2(c(ids.refr, 0), c(ids.refr, 1)), s25 = half2(c(ids.refr, 2), wr);
        const uint32_t p24 = op2(OpAccessChain, t_puint, {base, c_u[0], op2(OpIAdd, t_uint, {w0, c_w24})});
        emit(body, OpStore, {p24, s24, MemAligned, 4});
        const uint32_t p25 = op2(OpAccessChain, t_puint, {base, c_u[0], op2(OpIAdd, t_uint, {w0, c_w25})});
        emit(body, OpStore, {p25, s25, MemAligned, 4});
      }
    }
    if (gsh) {   // M5.4c: wG0 half2(Dg.r, Dg.g), wG1 half2(Dg.b, Vg) with Dg = D (1 - Vg) / Vg = D_u - D
      uint32_t sg0, sg1;
      if (ids.vg) {
        const uint32_t q = op2(OpFDiv, t_float, {op2(OpFSub, t_float, {c_one, ids.vg}), ids.vg});
        const uint32_t Dg = op2(OpVectorTimesScalar, t_v3, {D, q});
        sg0 = half2(c(Dg, 0), c(Dg, 1)), sg1 = half2(c(Dg, 2), ids.vg);
      } else {     // no game sun shadow: Vg = 1, nothing removed
        sg0 = half2(c(c_zero3, 0), c(c_zero3, 1)), sg1 = half2(c(c_zero3, 2), c_one);
      }
      const uint32_t pg0 = op2(OpAccessChain, t_puint, {base, c_u[0], op2(OpIAdd, t_uint, {w0, c_wg0})});
      emit(body, OpStore, {pg0, sg0, MemAligned, 4});
      const uint32_t pg1 = op2(OpAccessChain, t_puint, {base, c_u[0], op2(OpIAdd, t_uint, {w0, c_wg1})});
      emit(body, OpStore, {pg1, sg1, MemAligned, 4});
    }
  }

  // ---- assemble ----
  std::vector<uint32_t> caps, exts, modes;
  emit(caps, OpCapability, {CapInt64});
  emit(caps, OpCapability, {CapPsb});
  emit_string(exts, OpExtension, "SPV_KHR_physical_storage_buffer");
  emit(modes, OpExecutionMode, {entry, ModeEarlyTests});
  // Decorations go after the last annotation: before the first type/constant/variable, i.e.
  // after the last OpDecorate/OpMemberDecorate/OpDecorate* of the module.
  size_t last_deco = 0;
  for (const Ins& x : ins)
    if (x.op == OpDecorate || x.op == OpMemberDecorate || x.op == 332 /*OpDecorateId*/ || x.op == 5632 || x.op == 5633)
      last_deco = x.at + x.n;
  if (!last_deco) last_deco = last_mode ? last_mode : mem_model;
  if (!last_ext) last_ext = last_cap;
  if (!last_mode) return PatchStatus::kNoReturn;   // OriginUpperLeft is required for fragment shaders
  struct Ins2 { size_t at; const std::vector<uint32_t>* w; };
  const Ins2 inserts[] = {{last_cap, &caps}, {last_ext, &exts}, {last_mode, &modes}, {last_deco, &decos},
                          {first_func, &types}, {ret, &body}};
  out.reserve(words + caps.size() + exts.size() + modes.size() + decos.size() + types.size() + body.size());
  out.insert(out.end(), code, code + 5);
  size_t p = 5;
  for (const Ins2& in : inserts) {   // positions are ascending (layout order of a module)
    if (in.at < p) return out.clear(), PatchStatus::kNotSpirv;
    out.insert(out.end(), code + p, code + in.at);
    out.insert(out.end(), in.w->begin(), in.w->end());
    p = in.at;
  }
  out.insert(out.end(), code + p, code + words);
  out[3] = bound;
  // The addressing model of OpMemoryModel (its first operand), at its shifted position.
  for (size_t q = 5; q < out.size();) {
    const uint32_t n = out[q] >> 16, op = out[q] & 0xffffu;
    if (!n) return out.clear(), PatchStatus::kNotSpirv;
    if (op == OpMemoryModel) {
      out[q + 1] = AddrPsb64;
      break;
    }
    q += n;
  }
  if(surface) {
    // Insert early arithmetic without perturbing any ordinary patch word.
    std::vector<uint32_t> rewritten(out.begin(),out.begin()+5);
    for(size_t q=5;q<out.size();) {
      const uint32_t n=out[q]>>16, op=out[q]&0xffffu;
      bool skip=false;
      if(n>=3 && op==OpExtInst) for(const auto& d:splits)skip|=d.accum && out[q+2]==d.accum;
      if(!skip) {
        const size_t start=rewritten.size();rewritten.insert(rewritten.end(),out.begin()+q,out.begin()+q+n);
        // Clean material uses the base operands of all three native mixes.
        // Generated coverage code above intentionally still reads native 751.
        if(n>=3 && result_type.count(out[q+2])) for(size_t j=start+3;j<rewritten.size();++j) {
          auto m=foam_mix_remap.find(rewritten[j]);if(m!=foam_mix_remap.end())rewritten[j]=m->second;
        }
        // The sun's diffuse product is used after its F; only replace value operands.
        if(op==OpFMul) for(size_t j=start+3;j<rewritten.size();++j) {
          auto m=surface_remap.find(rewritten[j]);if(m!=surface_remap.end())rewritten[j]=m->second;
        }
      }
      if(n>=3) for(const Ins& original:ins) if(original.n==n && original.op==op && n>=3 && code[original.at+1]==out[q+1] && code[original.at+2]==out[q+2]) {
        auto add=surface_insert.find(original.at+original.n);
        if(add!=surface_insert.end()) rewritten.insert(rewritten.end(),add->second.begin(),add->second.end());
      }
      q+=n;
    }
    rewritten.insert(rewritten.end(),native_function.begin(),native_function.end());
    out.swap(rewritten);
  }
  return PatchStatus::kApplied;
}

inline const FsIds* fs_ids(uint64_t fs) {
  for (const FsIds& f : kFsIds)
    if (f.fs == fs) return &f;
  return nullptr;
}

}  // namespace bg3::water
