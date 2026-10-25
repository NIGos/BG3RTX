// Shared helpers for BG3RTX shaders.
#pragma once

// BG3 stores G-buffer normals octahedral-encoded in .xy (per REST's BG3 config;
// verify against a capture before relying on it).
float3 oct_decode(float2 e) {
  e = e * 2.0 - 1.0;
  float3 n = float3(e.xy, 1.0 - abs(e.x) - abs(e.y));
  float t = saturate(-n.z);
  n.xy += select(n.xy >= 0.0, -t, t);
  return normalize(n);
}
