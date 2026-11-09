// DDS mip 0 -> RGBA8 for the hair masks read from the paks (docs/RTX_HAIR_ROADMAP.md 2f). Decodes like Pillow's
// BcnDecode (the Python batch's Image.open(dds).convert("RGBA")), so the native "all styles" path sees the same mask
// bytes as the dev reference: 5:6:5 endpoints widened by bit replication, BC2/BC3 colour always in 4-colour mode, BC3
// alpha (a0 * (7 - i) + a1 * i) / 7 truncated. Masks in BG3 are DXT5 (all 32 masks of the 2026-11-07 catalogue).
#include "hairconv.h"

#include <cstring>
#include <string>

namespace hairconv {

namespace {
template <class T> T rd(const uint8_t* p) { T v; std::memcpy(&v, p, sizeof v); return v; }

void rgb565(uint16_t v, uint8_t o[3]) {
  uint8_t r = uint8_t((v & 0xF800) >> 8), g = uint8_t((v & 0x07E0) >> 3), b = uint8_t((v & 0x001F) << 3);
  o[0] = uint8_t(r | r >> 5);
  o[1] = uint8_t(g | g >> 6);
  o[2] = uint8_t(b | b >> 5);
}

// One 4x4 colour block into rgba (alpha untouched unless the BC1 3-colour mode makes index 3 transparent).
void colour_block(const uint8_t* blk, bool separate_alpha, uint8_t px[16][4]) {
  const uint16_t c0 = rd<uint16_t>(blk), c1 = rd<uint16_t>(blk + 2);
  uint8_t p[4][4] = {};
  rgb565(c0, p[0]);
  rgb565(c1, p[1]);
  p[0][3] = p[1][3] = p[2][3] = p[3][3] = 255;
  if (c0 > c1 || separate_alpha) {
    for (int k = 0; k < 3; ++k) {
      p[2][k] = uint8_t((2 * p[0][k] + p[1][k]) / 3);
      p[3][k] = uint8_t((p[0][k] + 2 * p[1][k]) / 3);
    }
  } else {
    for (int k = 0; k < 3; ++k) p[2][k] = uint8_t((p[0][k] + p[1][k]) / 2);
    p[3][0] = p[3][1] = p[3][2] = p[3][3] = 0;
  }
  const uint32_t bits = rd<uint32_t>(blk + 4);
  for (int i = 0; i < 16; ++i) {
    const int ci = (bits >> (2 * i)) & 3;
    for (int k = 0; k < 3; ++k) px[i][k] = p[ci][k];
    if (!separate_alpha) px[i][3] = p[ci][3];
  }
}

void bc3_alpha(const uint8_t* blk, uint8_t px[16][4]) {
  const int a0 = blk[0], a1 = blk[1];
  int a[8] = {a0, a1};
  if (a0 > a1) for (int i = 1; i < 7; ++i) a[i + 1] = ((7 - i) * a0 + i * a1) / 7;
  else {
    for (int i = 1; i < 5; ++i) a[i + 1] = ((5 - i) * a0 + i * a1) / 5;
    a[6] = 0;
    a[7] = 255;
  }
  uint64_t bits = 0;
  for (int i = 0; i < 6; ++i) bits |= uint64_t(blk[2 + i]) << (8 * i);
  for (int i = 0; i < 16; ++i) px[i][3] = uint8_t(a[(bits >> (3 * i)) & 7]);
}
}  // namespace

bool decode_dds(const uint8_t* d, size_t n, Mask& out, std::string* error) {
  auto fail = [&](const char* why) { if (error) *error = why; return false; };
  if (n < 128 || std::memcmp(d, "DDS ", 4) != 0) return fail("not a DDS file");
  const uint32_t h = rd<uint32_t>(d + 12), w = rd<uint32_t>(d + 16);
  const uint32_t pf_flags = rd<uint32_t>(d + 80);
  const char* four = reinterpret_cast<const char*>(d + 84);
  size_t at = 128;
  int kind = 0;   // 1 BC1, 2 BC2, 3 BC3, 4 RGBA8 (A8R8G8B8 / B8G8R8A8), 5 R8G8B8A8
  if (pf_flags & 4) {
    if (!std::memcmp(four, "DXT1", 4)) kind = 1;
    else if (!std::memcmp(four, "DXT3", 4)) kind = 2;
    else if (!std::memcmp(four, "DXT5", 4)) kind = 3;
    else if (!std::memcmp(four, "DX10", 4)) {
      if (n < 148) return fail("truncated DDS DX10 header");
      const uint32_t fmt = rd<uint32_t>(d + 128);
      at = 148;
      if (fmt == 71 || fmt == 72) kind = 1;
      else if (fmt == 74 || fmt == 75) kind = 2;
      else if (fmt == 77 || fmt == 78) kind = 3;
      else if (fmt == 28 || fmt == 29) kind = 5;
      else if (fmt == 87 || fmt == 91) kind = 4;
      else return fail("DDS DX10 format not supported for hair masks");
    } else {
      return fail("DDS compression not supported for hair masks");
    }
  } else if ((pf_flags & 0x40) && rd<uint32_t>(d + 88) == 32 && rd<uint32_t>(d + 92) == 0x00FF0000u) {
    kind = 4;   // A8R8G8B8 (B, G, R, A in memory)
  } else {
    return fail("DDS pixel format not supported for hair masks");
  }
  if (!w || !h || w > 16384 || h > 16384) return fail("DDS size out of range");
  out.w = int(w);
  out.h = int(h);
  out.rgba.assign(size_t(w) * h * 4, 0);
  if (kind >= 4) {
    if (at + size_t(w) * h * 4 > n) return fail("truncated DDS");
    for (size_t i = 0; i < size_t(w) * h; ++i) {
      const uint8_t* s = d + at + i * 4;
      uint8_t* o = &out.rgba[i * 4];
      if (kind == 4) { o[0] = s[2]; o[1] = s[1]; o[2] = s[0]; o[3] = s[3]; }
      else std::memcpy(o, s, 4);
    }
    return true;
  }
  const uint32_t bw = (w + 3) / 4, bh = (h + 3) / 4;
  const size_t bsize = kind == 1 ? 8 : 16;
  if (at + size_t(bw) * bh * bsize > n) return fail("truncated DDS");
  for (uint32_t by = 0; by < bh; ++by)
    for (uint32_t bx = 0; bx < bw; ++bx) {
      const uint8_t* blk = d + at + (size_t(by) * bw + bx) * bsize;
      uint8_t px[16][4];
      if (kind == 1) colour_block(blk, false, px);
      else {
        colour_block(blk + 8, true, px);
        if (kind == 3) bc3_alpha(blk, px);
        else
          for (int i = 0; i < 16; ++i) { const int a = (rd<uint64_t>(blk) >> (4 * i)) & 15; px[i][3] = uint8_t(a * 17); }
      }
      for (int py = 0; py < 4; ++py)
        for (int pxi = 0; pxi < 4; ++pxi) {
          const uint32_t x = bx * 4 + pxi, y = by * 4 + py;
          if (x < w && y < h) std::memcpy(&out.rgba[(size_t(y) * w + x) * 4], px[py * 4 + pxi], 4);
        }
    }
  return true;
}

}  // namespace hairconv
