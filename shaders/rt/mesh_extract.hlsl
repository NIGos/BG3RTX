// Track D BVH split (layer/bvhsplit.cpp): the object-space mesh of an instanced
// draw, from its transform-feedback capture, and the checks that keep the mesh
// cache honest.
//
// The capture has been converted in place to camera-relative world positions
// (rt/xfb_to_world.hlsl: float4(W, 1), or w = 0 where the triangle was
// collapsed: a vertex non-finite or with its depth overridden). The draw's
// vertex shader computes W = M_i * (P, 1) with M_i the instance's 3x4 row-major
// matrix (spirv_xfb.h analyze_vs_instancing); the host copies M_i from the
// game's instance stream into Mats at submit.
//
// mode 0 (extract): P = M_0^-1 (W_0) for the first instance's triangles, written
//   to the mesh pool with the captured UV (stride 24). If a check matrix is
//   given, M_c P is compared with instance c's captured triangles.
// mode 1 (check / audit): P from the pool, M_0 P compared with the captured triangles.
// Status (16 B per job, zeroed by the host): [0] max error / tolerance (float,
// atomic max on the bits of a non-negative float), [1] invalid vertices or a
// singular matrix, [3] a marker that the job ran.
[[vk::binding(0, 0)]] ByteAddressBuffer Soup;
[[vk::binding(1, 0)]] RWByteAddressBuffer Pool;
[[vk::binding(2, 0)]] ByteAddressBuffer Mats;
[[vk::binding(3, 0)]] RWByteAddressBuffer Status;

struct Push {
  uint tris, step, stride, mode;
  uint soup_base, pool_base, mat0, matc;
  uint check_base, status_off;
  float tol_abs, tol_rel;
};
[[vk::push_constant]] Push pc;

static const uint kNone = 0xffffffffu;

float3x4 load_mat(uint off) {
  return float3x4(asfloat(Mats.Load4(off)), asfloat(Mats.Load4(off + 16)), asfloat(Mats.Load4(off + 32)));
}

bool finite_mat(float3x4 m) { return all(abs(m[0]) < 1e7) && all(abs(m[1]) < 1e7) && all(abs(m[2]) < 1e7); }

float3 xform(float3x4 m, float3 p) { return mul(m, float4(p, 1.0)); }

// Normalised error of `w` against the captured vertex `c` (xyz, w = validity).
float err_of(float3 w, float4 c) {
  if (c.w == 0.0 || !all(isfinite(c.xyz))) return 0.0;   // collapsed there: no information
  float e = length(w - c.xyz) / (pc.tol_abs + pc.tol_rel * length(c.xyz));
  return isfinite(e) ? e : 1e30;
}

[numthreads(256, 1, 1)]
void main(uint3 id : SV_DispatchThreadID) {
  if (id.x == 0) Status.Store(pc.status_off + 12, 0x42564853u);
  float3x4 m0 = load_mat(pc.mat0);
  float3x3 a = float3x3(m0[0].xyz, m0[1].xyz, m0[2].xyz);
  float det = determinant(a);
  if (!finite_mat(m0) || !(abs(det) > 1e-12)) {
    if (id.x == 0) Status.InterlockedAdd(pc.status_off + 4, 1);
    return;
  }
  // Inverse of the 3x3 part (adjugate / determinant).
  float3x3 inv = transpose(float3x3(cross(a[1], a[2]), cross(a[2], a[0]), cross(a[0], a[1]))) / det;
  float3 t = float3(m0[0].w, m0[1].w, m0[2].w);
  const bool cross_check = pc.mode == 0 && pc.matc != kNone;
  float3x4 mc = cross_check ? load_mat(pc.matc) : m0;
  if (cross_check && !finite_mat(mc)) {
    if (id.x == 0) Status.InterlockedAdd(pc.status_off + 4, 1);
    return;
  }
  float emax = 0.0;
  uint invalid = 0;
  for (uint tri = id.x; tri < pc.tris; tri += pc.step) {
    [unroll] for (uint k = 0; k < 3; ++k) {
      uint v = (tri * 3 + k) * pc.stride;
      float3 p;
      if (pc.mode == 0) {
        float4 c = asfloat(Soup.Load4(pc.soup_base + v));
        if (c.w == 0.0 || !all(isfinite(c.xyz))) ++invalid;   // a collapsed triangle cannot be cached
        p = mul(inv, c.xyz - t);
        Pool.Store4(pc.pool_base + v, asuint(float4(p, 1.0)));
        if (pc.stride >= 24) Pool.Store2(pc.pool_base + v + 16, Soup.Load2(pc.soup_base + v + 16));
        if (cross_check) emax = max(emax, err_of(xform(mc, p), asfloat(Soup.Load4(pc.check_base + v))));
      } else {
        p = asfloat(Pool.Load3(pc.pool_base + v));
        emax = max(emax, err_of(xform(m0, p), asfloat(Soup.Load4(pc.soup_base + v))));
      }
    }
  }
  float wmax = WaveActiveMax(emax);
  uint winv = WaveActiveSum(invalid);
  if (WaveIsFirstLane()) {
    if (wmax > 0.0) Status.InterlockedMax(pc.status_off, asuint(wmax));
    if (winv) Status.InterlockedAdd(pc.status_off + 4, winv);
  }
}
