#pragma once
// Included only by OMM-capable variants, after gSceneStamp is declared. The
// reference AS is the original BLAS/TLAS scene, with identical geometry/material
// records and no hardware micromap decisions. It must always be a valid binding.
[[vk::binding(10,1)]] RaytracingAccelerationStructure gOmmReferenceScene;
[[vk::binding(11,1)]] globallycoherent RWByteAddressBuffer gOmmContentEpochs;
uint rt_omm_atomic_epoch(uint offset) {
  uint value;gOmmContentEpochs.InterlockedOr(offset,0u,value);return value;
}
void rt_omm_feedback_lost() {
  // One atomic per active wave, only after a structurally valid scene proof.
  if(WaveIsFirstLane()){uint previous;gSceneStamp.InterlockedOr(144u,1u,previous);}
}
bool rt_omm_scene_current() {
  const uint4 proof=gSceneStamp.Load4(128u);
  if(proof.x==0u || proof.x!=gSceneStamp.Load(0u) || proof.z==0u || proof.w!=0x4f4d4d32u)return false;
  if(rt_omm_atomic_epoch(8u)!=0u){rt_omm_feedback_lost();return false;}
  DeviceMemoryBarrier();
  const bool current=rt_omm_atomic_epoch(4u)==0u && proof.y==rt_omm_atomic_epoch(0u);
  DeviceMemoryBarrier();
  const bool coherent=current && rt_omm_atomic_epoch(8u)==0u && proof.y==rt_omm_atomic_epoch(0u);
  if(!coherent)rt_omm_feedback_lost();
  return coherent;
}

// Which scene the last traced query used (the same-ray check, rt_common.hlsli wrappers).
static bool g_omm_traced = false, g_omm_current = false;
static uint g_omm_cand = 0u;
// rt_omm_reference=1 (ls_flags bit 28, live A/B): the guard still runs and costs the
// same, but every query traces the reference scene. The check's second trace forces it.
bool rt_omm_use_accelerated() {
  g_omm_traced = true;
  if (g_omm_reference_pass) return false;
  g_omm_current = rt_omm_scene_current();
  return g_omm_current && (pc.ls_flags & (1u << 28)) == 0u;
}
// rtStats 435..442 (core/gi.cpp kOmmCheckFirst, rt_stats=1, logged as "omm check"):
// 0/1 visibility / closest-hit queries traced in both scenes, 2 queries whose accelerated
// scene was not current (reference traced, nothing to compare), 3/4 visibility /
// closest-hit answers that differ (violations, must be 0), 5 pairs with an unknown
// (aborted) answer on either side (excluded), 6/7 ray-query candidates of the compared
// queries in the accelerated / reference trace (micromap-resolved candidates drop out).
static const uint kOmmCheckFirst = 435u;
void rt_omm_stat_sum(uint index, uint v, uint bin) {
  const uint s = WaveActiveSum(v);
  const uint b = WaveReadLaneFirst(bin) & 63u;
  if (WaveIsFirstLane() && s) rtStats.InterlockedAdd((index * 64u + b) * 4u, s);
}
void rt_omm_check(bool current, bool hit_query, bool unknown, bool differ, uint cand_a, uint cand_b, uint bin) {
  stat_add(kOmmCheckFirst + 2u, !current, bin);
  if (!current) return;
  stat_add(kOmmCheckFirst + (hit_query ? 1u : 0u), true, bin);
  stat_add(kOmmCheckFirst + 5u, unknown, bin);
  stat_add(kOmmCheckFirst + (hit_query ? 4u : 3u), !unknown && differ, bin);
  rt_omm_stat_sum(kOmmCheckFirst + 6u, cand_a, bin);
  rt_omm_stat_sum(kOmmCheckFirst + 7u, cand_b, bin);
}
