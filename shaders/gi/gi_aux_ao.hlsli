// Scalar AO has the same half rounding as gAux.x; RGBA diagnostics keep binding 44.
[[vk::binding(51, 0)]] [[vk::image_format("r16f")]] RWTexture2D<float> gAuxAo;
[[vk::constant_id(1033)]] const uint kGiAoR16f = 0u;
float aux_ao_at(uint2 fp) { return kGiAoR16f != 0u ? gAuxAo[fp] : gAux[fp].x; }
void aux_ao_store(uint2 fp, float ao) {
  if (kGiAoR16f != 0u) {
    gAuxAo[fp] = ao;
    // AO-only push marker: preserve the complete local pan-capture contract.
    if ((pc.ls_pass & 0x80000000u) != 0u) gAux[fp] = float4(ao, 0.0, 0.0, 0.0);
  }
  else gAux[fp] = float4(ao, 0.0, 0.0, 0.0);
}
