// mesh_extract.hlsl's UV/basis mode for pooled meshes with a hit-material record
// (rt_hit_material; common/native_hit_pool.h, layer/hit_material.cpp pool_jobs_record).
// A separate entry so that mesh_extract.hlsl's own module is untouched.
//
// Input: one G-buffer transform-feedback capture of instance 0 of an STI draw (VS 2d2ac3c3,
// stride >= 64: clip position at 0, UV at 16, N/B/T = VS locations 1/2/3 at 24/36/48), the
// camera of that draw, instance 0's 3x4 row-major matrix M_0 and the mesh's pool vertices P
// (object space, from the depth prepass, rt/mesh_extract.hlsl mode 0).
// mode 0: per vertex the captured W (clip -> camera-relative world, as rt/xfb_to_world.hlsl) is
//   compared with M_0 P (normalised error, atomic max, the mesh cache tolerance), and UV plus
//   A_0^-1 N/B/T (A_0 the 3x3 part of M_0) are written to the basis range (44 B per vertex after
//   a 16 B header, whose magic is cleared first).
// mode 1 (one thread, after mode 0): the header {magic, generation, triangles, 1} only when every
//   vertex was valid and within tolerance; then the done word.
// Host status (64 B, zeroed by the host): matrix 0..47, max error 48, invalid 52, done 56, ran 60.
struct Camera { float4 v[14]; };
[[vk::binding(0, 0)]] ConstantBuffer<Camera> Cam;
[[vk::binding(1, 0)]] ByteAddressBuffer Xfb;
[[vk::binding(2, 0)]] RWByteAddressBuffer Pool;
[[vk::binding(3, 0)]] RWByteAddressBuffer Host;

struct Push {
  uint tris, step, xfb_stride, pool_stride;
  uint pool_base, basis_base, gen, mode;
  float tol_abs, tol_rel;
  uint pad0, pad1;
};
[[vk::push_constant]] Push pc;

static const uint kMagic = 0x53414250u, kDone = 0x454e4f44u, kRan = 0x42564853u;

float3x4 load_mat() {
  return float3x4(asfloat(Host.Load4(0)), asfloat(Host.Load4(16)), asfloat(Host.Load4(32)));
}
bool finite_mat(float3x4 m) { return all(abs(m[0]) < 1e7) && all(abs(m[1]) < 1e7) && all(abs(m[2]) < 1e7); }

[numthreads(256, 1, 1)]
void main(uint3 id : SV_DispatchThreadID) {
  if (pc.mode != 0) {
    if (id.x != 0) return;
    float err = asfloat(Host.Load(48));
    bool ok = Host.Load(60) == kRan && Host.Load(52) == 0u && err <= 1.0;
    if (ok) Pool.Store4(pc.basis_base, uint4(kMagic, pc.gen, pc.tris, 1u));
    Host.Store(56, kDone);
    return;
  }
  if (id.x == 0) {
    Host.Store(60, kRan);
    Pool.Store(pc.basis_base, 0u);
  }
  float3x4 m0 = load_mat();
  float3x3 a = float3x3(m0[0].xyz, m0[1].xyz, m0[2].xyz);
  float det = determinant(a);
  const bool bad = !finite_mat(m0) || !(abs(det) > 1e-12);
  float3x3 inv = bad ? (float3x3)0 : transpose(float3x3(cross(a[1], a[2]), cross(a[2], a[0]), cross(a[0], a[1]))) / det;
  // Camera rows of the clip transform (rt/xfb_to_world.hlsl, perspective G-buffer camera).
  float3 r0 = float3(Cam.v[8].x, Cam.v[9].x, Cam.v[10].x);
  float3 r1 = float3(Cam.v[8].y, Cam.v[9].y, Cam.v[10].y);
  float3 r2 = float3(Cam.v[8].w, Cam.v[9].w, Cam.v[10].w);
  float3 zr = float3(Cam.v[8].z, Cam.v[9].z, Cam.v[10].z);
  float4 t = Cam.v[11];
  float3 c0 = cross(r1, r2), c1 = cross(r2, r0), c2 = cross(r0, r1);
  float cdet = dot(r0, c0);
  const bool bad_cam = !(abs(cdet) > 1e-30);
  float cinv = bad_cam ? 0.0 : 1.0 / cdet;
  float emax = 0.0;
  uint invalid = (bad || bad_cam) && id.x == 0 ? 1u : 0u;
  for (uint tri = id.x; tri < pc.tris; tri += pc.step) {
    [unroll] for (uint k = 0; k < 3; ++k) {
      const uint v = tri * 3 + k;
      const uint x = v * pc.xfb_stride;
      float4 c = asfloat(Xfb.Load4(x));
      float3 b = float3(c.x - t.x, -c.y - t.y, c.w - t.w);
      float3 w = (c0 * b.x + c1 * b.y + c2 * b.z) * cinv;
      float zc = dot(w, zr) + t.z;
      float3 p = asfloat(Pool.Load3(pc.pool_base + v * pc.pool_stride));
      float3 e3 = w - mul(m0, float4(p, 1.0));
      float e = length(e3) / (pc.tol_abs + pc.tol_rel * length(w));
      bool ok = all(abs(c) < 1e30) && all(abs(w) < 1e7) && abs(zc - c.z) <= 1e-3 * abs(zc) + 1e-7 && isfinite(e);
      float2 uv = asfloat(Xfb.Load2(x + 16));
      float3 n[3];
      [unroll] for (uint j = 0; j < 3; ++j) {
        n[j] = mul(inv, asfloat(Xfb.Load3(x + 24 + 12 * j)));
        ok = ok && all(abs(n[j]) < 1e7) && dot(n[j], n[j]) > 0.0;
      }
      ok = ok && all(abs(uv) < 1e7);
      if (!ok) ++invalid;
      else emax = max(emax, e);
      const uint o = pc.basis_base + 16u + v * 44u;
      Pool.Store2(o, asuint(ok ? uv : float2(0, 0)));
      [unroll] for (uint j = 0; j < 3; ++j) Pool.Store3(o + 8 + 12 * j, asuint(ok ? n[j] : float3(0, 0, 0)));
    }
  }
  float wmax = WaveActiveMax(emax);
  uint winv = WaveActiveSum(invalid);
  if (WaveIsFirstLane()) {
    if (wmax > 0.0) Host.InterlockedMax(48, asuint(wmax));
    if (winv) Host.InterlockedAdd(52, winv);
  }
}
