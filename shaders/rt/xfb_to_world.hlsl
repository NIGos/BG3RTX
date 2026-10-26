// Track D phase 2 (layer/rtscene.cpp): turns the transform-feedback soup of the
// G-buffer pass (gl_Position per vertex, three per triangle) into
// camera-relative world positions, in place, for the BLAS build.
//
// Every G-buffer VS ends in gl_Position = vec4(W, 1) * cam._m2 with y negated,
// _m2 at offset 128 of the camera UBO (set 0 / binding 2), row-major as used
// from GLSL: clip_j = sum_k W_k * v[8+k][j] + v[11][j]. W is recovered from the
// x, y and w rows (exact; z is not used, see docs/TRACK_D_RT.md). The z row then
// flags vertices whose depth was overridden: the sky dome writes z = 0, and a
// shadow ray must never hit it. Such triangles, and those with non-finite
// vertices (the game parks culled vertices at NaN), collapse to one point:
// zero area, never hit, but still active, which keeps BLAS refits valid.
struct Camera { float4 v[14]; };
[[vk::binding(0, 0)]] ConstantBuffer<Camera> Cam;
[[vk::binding(1, 0)]] RWStructuredBuffer<float4> Verts;

struct Push { uint tris; uint stride; uint pad0, pad1; };
[[vk::push_constant]] Push pc;

bool finite4(float4 v) { return all(abs(v) < 1e30); }   // false for NaN and inf too

[numthreads(256, 1, 1)]
void main(uint3 id : SV_DispatchThreadID) {
  // Rows of the 3x3 system M W = b, one per used clip component (x, y, w).
  float3 r0 = float3(Cam.v[8].x, Cam.v[9].x, Cam.v[10].x);
  float3 r1 = float3(Cam.v[8].y, Cam.v[9].y, Cam.v[10].y);
  float3 r2 = float3(Cam.v[8].w, Cam.v[9].w, Cam.v[10].w);
  float3 zr = float3(Cam.v[8].z, Cam.v[9].z, Cam.v[10].z);
  float4 t = Cam.v[11];
  float3 c0 = cross(r1, r2), c1 = cross(r2, r0), c2 = cross(r0, r1);
  float det = dot(r0, c0);
  if (!(abs(det) > 1e-30)) det = 1e-30;
  float inv = 1.0 / det;

  for (uint tri = id.x; tri < pc.tris; tri += pc.stride) {
    float3 w[3];
    bool ok = true;
    [unroll] for (uint k = 0; k < 3; ++k) {
      float4 c = Verts[tri * 3 + k];
      bool vok = finite4(c);
      float3 b = float3(c.x - t.x, -c.y - t.y, c.w - t.w);
      float3 p = (c0 * b.x + c1 * b.y + c2 * b.z) * inv;
      float zc = dot(p, zr) + t.z;
      // Depth written by the shader must be what the matrix gives (sky: z = 0).
      vok = vok && all(abs(p) < 1e7) && abs(zc - c.z) <= 1e-3 * abs(zc) + 1e-7;
      w[k] = vok ? p : float3(0, 0, 0);
      ok = ok && vok;
    }
    if (!ok) w[1] = w[2] = w[0];
    [unroll] for (uint k = 0; k < 3; ++k) Verts[tri * 3 + k] = float4(w[k], 1.0);
  }
}
