// Track D geometry capture (layer/geocapture.cpp): copies the G-buffer vertex
// shaders' camera UBO (set 0 / binding 2, 224 bytes: _m0, _m1, _m2 = ViewProj,
// row-major, then scalars) into a host-visible buffer. The game's buffer has
// no TRANSFER_SRC usage, so a shader read is the only way to get at it.
struct Camera { float4 v[14]; };
[[vk::binding(0, 0)]] ConstantBuffer<Camera> Cam;
[[vk::binding(1, 0)]] RWStructuredBuffer<float4> Out;

[numthreads(16, 1, 1)]
void main(uint i : SV_DispatchThreadID) {
  if (i < 14) Out[i] = Cam.v[i];
}
