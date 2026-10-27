// D-EM: the last check of the emissive soup before its BLAS builds (layer/emissive.cpp),
// in our own command buffer. Building an acceleration structure from non-finite or
// unwritten vertex data is undefined (it lost the device in the BVH split). Every
// captured vertex went through xfb_to_world, which writes w = 1 (valid) or w = 0
// (collapsed); anything else was not converted this frame. A triangle with any vertex that
// is not finite, not within 1e6 of the camera, or not marked is collapsed to the origin
// (zero area, never hit, w = 0) and counted, so the build only ever reads finite data.
[[vk::binding(0, 0)]] RWByteAddressBuffer Soup;
[[vk::binding(1, 0)]] RWByteAddressBuffer Header;   // the build's header: word 12 collapsed triangles

struct Push { uint base; uint tris; uint stride; uint pad; };
[[vk::push_constant]] Push pc;

[numthreads(256, 1, 1)]
void main(uint3 id : SV_DispatchThreadID) {
  const uint tri = id.x;
  if (tri >= pc.tris) return;
  const uint stride = max(pc.stride, 16u);
  bool ok = true;
  [unroll] for (uint k = 0; k < 3; ++k) {
    float4 v = asfloat(Soup.Load4(pc.base + (tri * 3 + k) * stride));
    ok = ok && all(abs(v.xyz) < 1e6) && (v.w == 1.0 || v.w == 0.0);   // (w = 0: already collapsed by the conversion)
  }
  if (ok) return;
  [unroll] for (uint k = 0; k < 3; ++k) Soup.Store4(pc.base + (tri * 3 + k) * stride, asuint(float4(0, 0, 0, 0)));
  Header.InterlockedAdd(12 * 4, 1u);
}
