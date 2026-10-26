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

}  // namespace bg3
