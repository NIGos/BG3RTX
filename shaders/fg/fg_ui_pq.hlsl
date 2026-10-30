// DLSS-G UI tag consistent with BG3's HDR10 UI composite (fg_ui_pq=1, layer/streamline.cpp).
// The game writes final = PQ(sRGB_to_linear(ui) * paper_white + PQ_inverse(hudless) * (1 - a)).
// DLSS-G composites a tagged UI as ui.rgb + frame.rgb * (1 - ui.a) in the backbuffer's encoding.
// With ui.rgb = final - hudless * (1 - a) that reproduces the game's final exactly on real frames
// (the generated ones get the interpolated hudless in its place). No paper white needed.
Texture2D<float4> gFinal : register(t0);    // the composite's output (swapchain, PQ)
Texture2D<float4> gHudless : register(t1);  // the scene before the UI (PQ)
Texture2D<float4> gUi : register(t2);       // the game's UI layer (sRGB-encoded, alpha)
RWTexture2D<float4> gOut : register(u3);

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID) {
  uint w, h;
  gOut.GetDimensions(w, h);
  if (id.x >= w || id.y >= h) return;
  const float a = gUi.Load(int3(id.xy, 0)).a;
  if (a < 0.001) {   // the game shows the hudless image unchanged there
    gOut[id.xy] = 0.0;
    return;
  }
  const float3 f = gFinal.Load(int3(id.xy, 0)).rgb;
  const float3 s = gHudless.Load(int3(id.xy, 0)).rgb;
  gOut[id.xy] = float4(max(f - s * (1.0 - a), 0.0), a);
}
