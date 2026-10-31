// The roof volumes' ray test (core/sun_trust.h volumes in sun_trust.hlsli), shared by the sun
// pass (rt_shadow.hlsl: trust) and gi_hit_sun (gi_trace_rt.hlsl RT_HIT_GI_SUN: R0 roof support).
// Needs sun_trust.hlsli first.
#pragma once
float3 qrot_inv(float4 q, float3 v) {   // rotate by the conjugate of q (world -> local)
  float3 u = -q.xyz;
  float3 t = 2.0 * cross(u, v);
  return v + q.w * t + cross(u, t);
}

// Does the ray o + t d, t >= 0, pass through a roof volume (camera-relative o)?
bool sun_under_roof(float3 o, float3 d, float3 cam_abs) {
  const uint n = min(st_u(0), 256u);
  for (uint i = 0; i < n; ++i) {
    const uint a = 192u + i * 48u;   // kSunHeader
    float4 c = st_f4(a);
    float4 q = st_f4(a + 16u);
    float3 h = st_f4(a + 32u).xyz;
    float3 lo = qrot_inv(q, o - (c.xyz - cam_abs));
    float3 ld = qrot_inv(q, d);
    float3 inv = 1.0 / select(abs(ld) > 1e-8, ld, select(ld >= 0.0, 1e-8, -1e-8));
    float3 t1 = (-h - lo) * inv, t2 = (h - lo) * inv;
    float3 tn = min(t1, t2), tf = max(t1, t2);
    float t0 = max(max(tn.x, tn.y), tn.z), t3 = min(min(tf.x, tf.y), tf.z);
    if (t3 >= max(t0, 0.0)) return true;
  }
  return false;
}
