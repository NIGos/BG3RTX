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
  // M5.4 (record words 8-9 with `sun`): the FS's sun term in Lw = (sun_d + sun_s) * sun_m, the
  // operands of its OpFAdd chain before the probe term (spirv-dis --raw-id of the dumps):
  uint32_t sun_d;       // vec3: sun diffuse * N.L, added to the local lights' diffuse sum
  uint32_t sun_s;       // vec3: sun specular * N.L, added to the local lights' specular sum
  uint32_t sun_m;       // float: the factor of (diffuse + specular) sums, mix(1, 1, b26 _m1) = 1
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
// The other audited water FS are not in the table yet: their draws keep the game's pipeline
// (counted by the layer as "no variant").
// M5.4 sun ids (2026-11-01, spirv-dis --raw-id of the three dumps): in each FS Lw =
// ((Ld + Sd) + (Ls + Ss)) * m + (Kd + Ks) k with Sd = (Lsun * diffuse BRDF) * N.L and Ss = (Lsun *
// specular BRDF) * N.L, Lsun = sun colour (atmosphere) * cloud shadow * exposure: the sun has no
// cascade or other geometric visibility on the water (no shadow-map sample in the FS; its only
// textureGather is the local lights' atlas b28). 7b44a567: %3889 %3892 * %3898 (Lsun %3803 =
// %2064 * (%2236 clouds * b0)), 99c1fcef: %4087 %4090 * %4096, c4e92ccd: %3310 %3313 * %3319
// (Lsun %3224 = %1485 * (%1657 clouds * b0)).
inline constexpr FsIds kFsIds[] = {
    {0x7b44a567771ecbecull, 784, 4096, 3799, 2063, 3379, 3303, 3352, 1207, 4335, 1480, 5, 3889, 3892, 3898},
    {0x99c1fcef82b253c1ull, 809, 4294, 3997, 2261, 3577, 3501, 3550, 1294, 4533, 1678, 5, 4087, 4090, 4096},
    {0xc4e92ccdb08dd777ull, 425, 3510, 3220, 1484, 2800, 2724, 2773, 665, 3671, 899, 5, 3310, 3313, 3319},
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
  OpConstantNull = 46, OpFunction = 54, OpFunctionEnd = 56, OpVariable = 59, OpLoad = 61, OpStore = 62,
  OpAccessChain = 65, OpDecorate = 71, OpMemberDecorate = 72, OpCompositeConstruct = 80,
  OpCompositeExtract = 81, OpConvertFToU = 109, OpConvertUToPtr = 120, OpBitcast = 124, OpIAdd = 128, OpFAdd = 129,
  OpIMul = 132, OpFMul = 133, OpVectorTimesScalar = 142, OpLogicalAnd = 167, OpSelect = 169,
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
// kRecordWordsSun words with the sun term; false emits exactly the M5.3 module.
inline PatchStatus patch_fs(const uint32_t* code, size_t words, const FsIds& ids, uint64_t addr, uint32_t capacity,
                            std::vector<uint32_t>& out, bool sun = false) {
  using namespace fsp;
  out.clear();
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
  uint32_t t_bool = 0, t_uint = 0, t_u64 = 0, t_float = 0, t_v2 = 0, t_v3 = 0, t_v4 = 0;
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
  auto input_of = [&](uint32_t var, uint32_t pointee) {
    auto v = var_ptr.find(var);
    if (v == var_ptr.end()) return false;
    auto p = ptr_of.find(v->second);
    return p != ptr_of.end() && p->second.first == ScInput && p->second.second == pointee;
  };
  if (!input_of(ids.pos, t_v3) || !input_of(frag_coord, t_v4)) return PatchStatus::kIdMismatch;

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
  // M5.4: the record stride and word offsets 8, 9 (after every M5.3 declaration: sun = false is unchanged).
  uint32_t c_stride = c_u[kRecordWords], c_w8 = 0, c_w9 = 0;
  if (sun) {
    c_w9 = id(), c_stride = id();
    emit(types, OpConstant, {t_uint, c_w9, 9});
    emit(types, OpConstant, {t_uint, c_stride, kRecordWordsSun});
    c_w8 = c_u[kRecordWords];   // 8
  }
  std::vector<uint32_t> decos;
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
  const uint32_t aT = op2(OpFMul, t_float, {ids.a, T});
  const uint32_t G = op2(OpVectorTimesScalar, t_v3, {ids.X, aT});
  const uint32_t Gk = op2(OpVectorTimesScalar, t_v3, {ids.X, op2(OpFMul, t_float, {aT, ids.k})});
  const uint32_t E = ext(t_float, GlslFma, {op2(OpCompositeExtract, t_float, {ids.f0, 0}),
                                            op2(OpCompositeExtract, t_float, {ids.dfg, 0}),
                                            op2(OpCompositeExtract, t_float, {ids.dfg, 1})});
  const uint32_t A = op2(OpVectorTimesScalar, t_v3, {G, E});
  const uint32_t B = op2(OpFMul, t_v3, {Gk, ids.Ks});
  const uint32_t C = ext(t_v3, GlslNMax, {ids.C, c_zero3});
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
  v[7] = ext(t_uint, GlslPackUnorm2x16, {op2(OpCompositeConstruct, t_v2, {c_one, ids.rough})});
  for (uint32_t k = 0; k < kRecordWords; ++k) {
    const uint32_t at = op2(OpIAdd, t_uint, {w0, c_u[k]});
    const uint32_t p = op2(OpAccessChain, t_puint, {base, c_u[0], at});
    emit(body, OpStore, {p, v[k], MemAligned, 4});
  }
  if (sun) {   // D = G * (sun_d + sun_s) * sun_m: w8 half2(D.r, D.g), w9 half2(D.b, 1)
    const uint32_t S = op2(OpVectorTimesScalar, t_v3, {op2(OpFAdd, t_v3, {ids.sun_d, ids.sun_s}), ids.sun_m});
    const uint32_t D = op2(OpFMul, t_v3, {G, S});
    const uint32_t s8 = half2(c(D, 0), c(D, 1)), s9 = half2(c(D, 2), c_one);
    const uint32_t p8 = op2(OpAccessChain, t_puint, {base, c_u[0], op2(OpIAdd, t_uint, {w0, c_w8})});
    emit(body, OpStore, {p8, s8, MemAligned, 4});
    const uint32_t p9 = op2(OpAccessChain, t_puint, {base, c_u[0], op2(OpIAdd, t_uint, {w0, c_w9})});
    emit(body, OpStore, {p9, s9, MemAligned, 4});
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
  return PatchStatus::kApplied;
}

inline const FsIds* fs_ids(uint64_t fs) {
  for (const FsIds& f : kFsIds)
    if (f.fs == fs) return &f;
  return nullptr;
}

}  // namespace bg3::water
