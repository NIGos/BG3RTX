#pragma once
// The bound alpha-record descriptor and its header cannot change during a
// dispatch. Cache only these integer values, privately to this invocation.
// No sampled alpha, footprint, texture LOD, cutoff or stochastic threshold is
// cached. A fresh invocation starts empty, including after a frame/scene change.
#ifdef __cplusplus
static uint kSpecAlphaMetadataCache = 0u;
#elif defined(BG3RTX_GPU_OPT_LEGACY)
static const uint kSpecAlphaMetadataCache = 0u;
#else
[[vk::constant_id(1022)]] const uint kSpecAlphaMetadataCache = 0u;
#endif
#define RT_ALPHA_METADATA_CACHE_ON (kSpecAlphaMetadataCache != 0u)
static uint gAlphaMetadataValid = 0u;
static uint gAlphaMetadataBytes = 0u;
static uint gAlphaMetadataCount = 0u;
static uint gAlphaMetadataStride = 0u;
static uint gAlphaMetadataPolicy = 0u;

uint alpha_record_bytes() {
  uint value;
  if (!RT_ALPHA_METADATA_CACHE_ON) { gAlphaRec.GetDimensions(value); return value; }
  if ((gAlphaMetadataValid & 1u) == 0u) {
    gAlphaRec.GetDimensions(gAlphaMetadataBytes);
    gAlphaMetadataValid |= 1u;
  }
  return gAlphaMetadataBytes;
}
uint alpha_record_count() {
  if (!RT_ALPHA_METADATA_CACHE_ON) return gAlphaRec.Load(0u);
  if ((gAlphaMetadataValid & 2u) == 0u) {
    gAlphaMetadataCount = gAlphaRec.Load(0u);
    gAlphaMetadataValid |= 2u;
  }
  return gAlphaMetadataCount;
}
uint alpha_record_stride() {
  if (!RT_ALPHA_METADATA_CACHE_ON) return gAlphaRec.Load(4u);
  if ((gAlphaMetadataValid & 4u) == 0u) {
    gAlphaMetadataStride = gAlphaRec.Load(4u);
    gAlphaMetadataValid |= 4u;
  }
  return gAlphaMetadataStride;
}
uint alpha_record_policy() {
  if (!RT_ALPHA_METADATA_CACHE_ON) return gAlphaRec.Load(8u);
  if ((gAlphaMetadataValid & 8u) == 0u) {
    gAlphaMetadataPolicy = gAlphaRec.Load(8u);
    gAlphaMetadataValid |= 8u;
  }
  return gAlphaMetadataPolicy;
}
