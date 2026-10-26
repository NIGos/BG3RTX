// SPIR-V patch for the Track D geometry capture: makes a vertex shader's
// Position output a transform feedback output (buffer 0, stride 16, offset 0).
// With stride 24 (Track D phase 3, rt_alpha=1) the shader's lowest-location
// 2 x float32 output (the texture coordinate in every BG3 depth-prepass VS)
// is captured as well, at offset 16, for ray-query alpha testing.
// Header-only so tools/ can test it against dumped game shaders.
#pragma once
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace bg3 {

// ---- SPIR-V patch ----
// Adds: OpCapability TransformFeedback; OpExecutionMode <vs> Xfb; and on the
// Position output XfbBuffer 0, XfbStride 16, Offset 0 (on the variable, or on
// the member of a gl_PerVertex block). Returns false (module untouched) for
// anything that is not a plain vertex shader with a Position output.
// `stride` is 16 (Position only) or 24 (Position + UV); `uv_location`
// receives the location of the captured UV output, or -1.
inline bool patch_vs(const uint32_t* code, size_t words, std::vector<uint32_t>& out, uint32_t stride = 16,
                     int* uv_location = nullptr) {
  if (uv_location) *uv_location = -1;
  if (words < 5 || code[0] != 0x07230203) return false;
  struct Ins { size_t pos; uint32_t op, n; };
  std::vector<Ins> ins;
  for (size_t p = 5; p < words;) {
    uint32_t n = code[p] >> 16, op = code[p] & 0xffff;
    if (!n || p + n > words) return false;
    ins.push_back({p, op, n});
    p += n;
  }
  size_t last_cap = 0, last_ep = 0, pos_dec = 0;
  std::vector<uint32_t> vs_eps;
  bool other_stage = false, already = false;
  uint32_t pos_var = 0, pos_struct = 0, pos_member = 0;
  for (const auto& i : ins) {
    const uint32_t* a = code + i.pos;
    switch (i.op) {
      case 17:  // OpCapability
        last_cap = i.pos + i.n;
        if (i.n >= 2 && a[1] == 53) already = true;
        break;
      case 15:  // OpEntryPoint
        last_ep = i.pos + i.n;
        if (i.n >= 3 && a[1] == 0) vs_eps.push_back(a[2]);
        else other_stage = true;
        break;
      case 71:  // OpDecorate
        if (i.n >= 4 && a[2] == 11 && a[3] == 0) { pos_var = a[1]; pos_dec = i.pos + i.n; }
        if (i.n >= 3 && (a[2] == 36 || a[2] == 37)) already = true;
        break;
      case 72:  // OpMemberDecorate
        if (i.n >= 5 && a[3] == 11 && a[4] == 0) { pos_struct = a[1]; pos_member = a[2]; pos_dec = i.pos + i.n; }
        break;
    }
  }
  if (vs_eps.empty() || other_stage || already || !last_cap || !last_ep || !pos_dec) return false;
  // The UV: an Output variable of type vec2 (32-bit float) with the lowest Location.
  uint32_t uv_var = 0;
  int uv_loc = -1;
  if (stride >= 24) {
    std::vector<uint32_t> f32, v2, p_out;   // type ids
    for (const auto& i : ins) {
      const uint32_t* a = code + i.pos;
      if (i.op == 22 && i.n >= 3 && a[2] == 32) f32.push_back(a[1]);   // OpTypeFloat 32
      if (i.op == 23 && i.n >= 4 && a[3] == 2 && std::find(f32.begin(), f32.end(), a[2]) != f32.end())
        v2.push_back(a[1]);   // OpTypeVector f32 2
      if (i.op == 32 && i.n >= 4 && a[2] == 3 && std::find(v2.begin(), v2.end(), a[3]) != v2.end())
        p_out.push_back(a[1]);   // OpTypePointer Output vec2
    }
    for (const auto& i : ins) {
      const uint32_t* a = code + i.pos;   // OpVariable <ptr> <id> Output
      if (i.op != 59 || i.n < 4 || a[3] != 3 || std::find(p_out.begin(), p_out.end(), a[1]) == p_out.end()) continue;
      for (const auto& d : ins)   // OpDecorate <var> Location L
        if (d.op == 71 && d.n >= 4 && code[d.pos + 1] == a[2] && code[d.pos + 2] == 30 &&
            (uv_loc < 0 || int(code[d.pos + 3]) < uv_loc)) {
          uv_loc = int(code[d.pos + 3]);
          uv_var = a[2];
        }
    }
    for (const auto& i : ins)   // a component or an XFB layout of its own: leave it out
      if (uv_var && i.op == 71 && i.n >= 3 && code[i.pos + 1] == uv_var &&
          (code[i.pos + 2] == 31 || code[i.pos + 2] == 35 || code[i.pos + 2] == 36)) {
        uv_var = 0;
        uv_loc = -1;
      }
  }
  if (!pos_var) {
    uint32_t ptr = 0;
    for (const auto& i : ins)
      if (i.op == 32 && i.n >= 4 && code[i.pos + 2] == 3 && code[i.pos + 3] == pos_struct) ptr = code[i.pos + 1];
    for (const auto& i : ins)
      if (ptr && i.op == 59 && i.n >= 4 && code[i.pos + 1] == ptr && code[i.pos + 3] == 3) pos_var = code[i.pos + 2];
    if (!pos_var) return false;
    // An Offset already on the block means somebody else's XFB layout.
    for (const auto& i : ins)
      if (i.op == 72 && i.n >= 4 && code[i.pos + 1] == pos_struct && code[i.pos + 3] == 35) return false;
  } else {
    for (const auto& i : ins)
      if (i.op == 71 && i.n >= 3 && code[i.pos + 1] == pos_var && code[i.pos + 2] == 35) return false;
  }
  std::vector<uint32_t> cap = {(2u << 16) | 17, 53};
  std::vector<uint32_t> modes;
  for (uint32_t ep : vs_eps) modes.insert(modes.end(), {(3u << 16) | 16, ep, 11});
  std::vector<uint32_t> dec = {(4u << 16) | 71, pos_var, 36, 0, (4u << 16) | 71, pos_var, 37, stride};
  if (uv_var)
    dec.insert(dec.end(), {(4u << 16) | 71, uv_var, 36, 0, (4u << 16) | 71, uv_var, 37, stride,
                           (4u << 16) | 71, uv_var, 35, 16});
  if (pos_struct) dec.insert(dec.end(), {(5u << 16) | 72, pos_struct, pos_member, 35, 0});
  else dec.insert(dec.end(), {(4u << 16) | 71, pos_var, 35, 0});
  out.clear();
  out.reserve(words + 32);
  out.insert(out.end(), code, code + last_cap);
  out.insert(out.end(), cap.begin(), cap.end());
  out.insert(out.end(), code + last_cap, code + last_ep);
  out.insert(out.end(), modes.begin(), modes.end());
  out.insert(out.end(), code + last_ep, code + pos_dec);
  out.insert(out.end(), dec.begin(), dec.end());
  out.insert(out.end(), code + pos_dec, code + words);
  if (uv_location) *uv_location = uv_loc;
  return true;
}

// ---- Instancing analysis (Track D BVH split, layer/bvhsplit.cpp) ----
// BG3's class-A vertex shaders (docs/TRACK_D_RT.md 1) place a mesh with a
// per-instance affine matrix whose three rows are vertex inputs:
//   W = vec4(P, 1) * mat4(R0, R1, R2, vec4(0, 0, 0, 1))      (P, R0..R2 inputs)
//   gl_Position = vec4(W.xyz, 1) * ViewProj                    (y negated)
// W is then exactly the 3x4 row-major transform [R0; R1; R2] of the vertex
// input P (the VkTransformMatrixKHR layout), and nothing displaces it after
// the transform (wind and cloth shaders add to W first and do not match).
// `uv_out_location`: the output captured as UV (patch_vs); the result says
// which input it is a plain copy of (-1: computed, or none).
struct VsInstancing {
  bool ok = false;
  int pos_location = -1;
  int row_location[3] = {-1, -1, -1};
  int uv_input_location = -1;
};

inline VsInstancing analyze_vs_instancing(const uint32_t* code, size_t words, int uv_out_location) {
  VsInstancing r;
  if (words < 5 || code[0] != 0x07230203) return r;
  struct Ins { size_t pos; uint32_t op, n; };
  std::vector<Ins> ins;
  uint32_t bound = code[3];
  if (bound == 0 || bound > (1u << 22)) return r;
  std::vector<int32_t> def(bound, -1);                 // result id -> instruction index
  std::vector<int32_t> loc(bound, -1);                 // Location decorations
  std::vector<uint8_t> builtin_pos(bound, 0);          // BuiltIn Position on a variable
  std::vector<uint32_t> storage(bound, UINT32_MAX);    // OpVariable storage class
  for (size_t p = 5; p < words;) {
    uint32_t n = code[p] >> 16, op = code[p] & 0xffff;
    if (!n || p + n > words) return r;
    const uint32_t* a = code + p;
    // Result id position: types (ops 19-39) have it in word 1, value ops in word 2.
    uint32_t res = 0;
    if (op >= 19 && op <= 39 && n >= 2) res = a[1];
    else if ((op == 43 || op == 44 || op == 59 || op == 61 || op == 65 || op == 79 || op == 80 || op == 81 ||
              op == 82 || op == 127 || op == 144 || op == 145) && n >= 3)
      res = a[2];
    if (res && res < bound) def[res] = int32_t(ins.size());
    if (op == 71 && n >= 4 && a[1] < bound) {          // OpDecorate
      if (a[2] == 30) loc[a[1]] = int32_t(a[3]);
      if (a[2] == 11 && a[3] == 0) builtin_pos[a[1]] = 1;
    }
    if (op == 59 && n >= 4 && a[2] < bound) storage[a[2]] = a[3];
    ins.push_back({p, op, n});
    p += n;
  }
  auto at = [&](uint32_t id) -> const uint32_t* {
    if (id >= bound || def[id] < 0) return nullptr;
    return code + ins[size_t(def[id])].pos;
  };
  auto op_of = [&](uint32_t id) -> uint32_t { const uint32_t* a = at(id); return a ? (a[0] & 0xffff) : 0; };
  auto len_of = [&](uint32_t id) -> uint32_t { const uint32_t* a = at(id); return a ? (a[0] >> 16) : 0; };
  // The location of the Input variable `id` is a plain OpLoad of, or -1.
  auto input_load = [&](uint32_t id) -> int {
    const uint32_t* a = at(id);
    if (!a || (a[0] & 0xffff) != 61) return -1;
    uint32_t var = a[3];
    if (var >= bound || storage[var] != 1) return -1;   // Input
    return loc[var];
  };
  auto const_float = [&](uint32_t id, uint32_t bits) {
    const uint32_t* a = at(id);
    return a && (a[0] & 0xffff) == 43 && (a[0] >> 16) == 4 && a[3] == bits;
  };
  const uint32_t kOne = 0x3f800000u;
  auto const_0001 = [&](uint32_t id) {
    const uint32_t* a = at(id);
    if (!a || (a[0] >> 16) != 7) return false;
    uint32_t op = a[0] & 0xffff;
    if (op != 44 && op != 80) return false;   // OpConstantComposite / OpCompositeConstruct of constants
    return const_float(a[3], 0) && const_float(a[4], 0) && const_float(a[5], 0) && const_float(a[6], kOne);
  };
  // vec4(S.xyz, 1): returns S (the vector whose xyz is taken), or 0.
  auto xyz1_source = [&](uint32_t id) -> uint32_t {
    const uint32_t* a = at(id);
    if (!a || (a[0] & 0xffff) != 80) return 0;
    uint32_t n = a[0] >> 16;
    if (n == 7) {   // four scalars: S.x, S.y, S.z, 1.0
      if (!const_float(a[6], kOne)) return 0;
      uint32_t src = 0;
      for (uint32_t k = 0; k < 3; ++k) {
        const uint32_t* e = at(a[3 + k]);
        if (!e || (e[0] & 0xffff) != 81 || (e[0] >> 16) != 5 || e[4] != k) return 0;
        if (k && e[3] != src) return 0;
        src = e[3];
      }
      return src;
    }
    if (n == 5) {   // a vec3 and 1.0: the vec3 itself, or a .xyz shuffle of a vec4
      if (!const_float(a[4], kOne)) return 0;
      const uint32_t* s = at(a[3]);
      if (s && (s[0] & 0xffff) == 79 && (s[0] >> 16) == 8 && s[3] == s[4] && s[5] == 0 && s[6] == 1 && s[7] == 2)
        return s[3];
      return a[3];
    }
    return 0;
  };
  // gl_Position's variable (or the gl_PerVertex block variable, member 0 checked below).
  uint32_t pos_var = 0;
  for (uint32_t id = 1; id < bound; ++id)
    if (builtin_pos[id] && storage[id] == 3) pos_var = id;
  // Values stored to gl_Position.
  std::vector<uint32_t> stored;
  for (const auto& i : ins) {
    if (i.op != 62 || i.n < 3) continue;   // OpStore ptr obj
    uint32_t ptr = code[i.pos + 1];
    bool to_pos = ptr == pos_var && pos_var;
    if (!to_pos) {   // OpAccessChain into a gl_PerVertex block, member 0 (Position)
      const uint32_t* ac = at(ptr);
      if (ac && (ac[0] & 0xffff) == 65 && (ac[0] >> 16) == 5) {
        uint32_t base = ac[3];
        if (base < bound && storage[base] == 3 && const_float(ac[4], 0)) to_pos = true;   // int 0 has bits 0 too
      }
    }
    if (to_pos) stored.push_back(code[i.pos + 2]);
  }
  if (stored.empty()) return r;
  // Peel the y negation: CompositeInsert(FNegate(...), T, 1) chains down to T.
  auto peel = [&](uint32_t v) {
    for (int k = 0; k < 4 && op_of(v) == 82 && len_of(v) >= 6; ++k) v = at(v)[4];
    return v;
  };
  for (const auto& i : ins) {
    if (i.op != 144 || i.n != 5) continue;   // OpVectorTimesMatrix W = V * M
    const uint32_t* a = code + i.pos;
    uint32_t w = a[2], v = a[3], m = a[4];
    const uint32_t* mc = at(m);
    if (!mc || (mc[0] & 0xffff) != 80 || (mc[0] >> 16) != 7) continue;   // mat4(R0, R1, R2, C)
    int rows[3];
    bool ok = const_0001(mc[6]);
    for (int k = 0; k < 3 && ok; ++k) ok = (rows[k] = input_load(mc[3 + k])) >= 0;
    if (!ok) continue;
    uint32_t ps = xyz1_source(v);
    int pl = ps ? input_load(ps) : -1;
    if (pl < 0) continue;
    // W must reach gl_Position only through vec4(W.xyz, 1) * <matrix>.
    bool reaches = false;
    for (const auto& j : ins) {
      if ((j.op != 144 && j.op != 145) || j.n != 5) continue;
      const uint32_t* b = code + j.pos;
      uint32_t vec = j.op == 144 ? b[3] : b[4];
      if (xyz1_source(vec) != w) continue;
      for (uint32_t s : stored)
        if (peel(s) == b[2]) reaches = true;
    }
    if (!reaches) continue;
    r.ok = true;
    r.pos_location = pl;
    for (int k = 0; k < 3; ++k) r.row_location[k] = rows[k];
    break;
  }
  if (!r.ok) return r;
  // The captured UV output: a plain copy of an input?
  if (uv_out_location >= 0)
    for (const auto& i : ins) {
      if (i.op != 62 || i.n < 3) continue;
      uint32_t ptr = code[i.pos + 1];
      if (ptr < bound && storage[ptr] == 3 && loc[ptr] == uv_out_location) r.uv_input_location = input_load(code[i.pos + 2]);
    }
  return r;
}

}  // namespace bg3
