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
  // rt_omm_dyn (layer/bvhsplit.cpp bvh_omm_commit): micro-maps baked from immutable offline masks and checked per
  // triangle against the frame's uvs before the BLAS build; no content epoch can change them, so the proof is the
  // scene generation alone (magic 'OMM3', z = per-draw BLAS with a micro-map).
  if(proof.w==0x4f4d4d33u)return proof.x!=0u && proof.x==gSceneStamp.Load(0u) && proof.z!=0u;
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
// The guard's atomics run once per wave and invocation, not per query (rt_omm_scene_current
// was five per-lane atomics on every ray query): the first active lane reads the epochs, the
// wave shares the answer, and later queries of the same invocation reuse it.
static int g_omm_scene_state = -1;
bool rt_omm_scene_current_wave() {
  if (g_omm_scene_state < 0) {
    bool current = false;
    if (WaveIsFirstLane()) current = rt_omm_scene_current();
    g_omm_scene_state = WaveReadLaneFirst(current) ? 1 : 0;
  }
  return g_omm_scene_state == 1;
}
bool rt_omm_use_accelerated() {
  g_omm_traced = true;
  if (g_omm_reference_pass) return false;
  g_omm_current = rt_omm_scene_current_wave();
  return g_omm_current && (pc.ls_flags & (1u << 28)) == 0u;
}
// rtStats 443..447 (core/gi.cpp kOmmCheckFirst, rt_stats=1, logged as "omm check"):
// 0 queries compared (accelerated scene current, both answers known), 1 not compared (not current,
// or an unknown answer on either side), 2 answers that differ (violations, must be 0), 3/4
// ray-query candidates of the compared queries in the accelerated / reference trace.
static const uint kOmmCheckFirst = 443u;
void rt_omm_stat_sum(uint index, uint v, uint bin) {
  const uint s = WaveActiveSum(v);
  const uint b = WaveReadLaneFirst(bin) & 63u;
  if (WaveIsFirstLane() && s) rtStats.InterlockedAdd((index * 64u + b) * 4u, s);
}
void rt_omm_check(bool current, bool hit_query, bool unknown, bool differ, uint cand_a, uint cand_b, uint bin) {
  const bool compared = current && !unknown;
  stat_add(kOmmCheckFirst + 1u, !compared, bin);
  if (!compared) return;
  stat_add(kOmmCheckFirst, true, bin);
  stat_add(kOmmCheckFirst + 2u, differ, bin);
  rt_omm_stat_sum(kOmmCheckFirst + 3u, cand_a, bin);
  rt_omm_stat_sum(kOmmCheckFirst + 4u, cand_b, bin);
}
