// Is the G-buffer's linear depth that of the visible strand? (gi/hair_strand_pixel.hlsli, rt/hair_fibre_gi.hlsli; GPU
// check: tools/hair_dual_gpu_check.hlsl h = 5, tests/hair_primary_gpu_test.py depth_match_check.)
//   z   the strand's device depth (hair primary pixel entry e.z = c.z / c.w, rt/hair_primary_gbuffer.hlsl), what it
//       writes to the D24 depth buffer;
//   d   gDepth, the R32F linear depth a later pass derives from that buffer (P = ray_at(uv) d);
//   zr  the view depth of the frustum rays (ray_at's plane), so that w = d zr is the view depth.
// The game's projection is reversed Z with an infinite far plane, near 0.1 (docs/FRAME_MAP.md "m1"; rt/depth_tol.hlsli):
// z = n / w. Compared as device depths: the buffer rounds z to a D24 code (half a code, 2^-25) and the linear chain adds
// fp32 roundings, each half an ulp relative: e.z, the R32F storage, the linearisation (n / z, / zr) and this function's
// own (d zr, n / x): one code plus 6 half ulps relative.
#pragma once

bool hair_depth_match(float z, float d, float zr) {
  const float zd = 0.1 / max(d * zr, 1e-30);   // d as a device depth
  return abs(z - zd) <= 2.0 * 2.9802322e-8 + 6.0 * 5.9604645e-8 * zd;
}
