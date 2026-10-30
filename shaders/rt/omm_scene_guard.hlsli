#pragma once
// Included only by OMM-capable variants, after gSceneStamp is declared. The
// reference AS is the original BLAS/TLAS scene, with identical geometry/material
// records and no hardware micromap decisions. It must always be a valid binding.
[[vk::binding(10,1)]] RaytracingAccelerationStructure gOmmReferenceScene;
[[vk::binding(11,1)]] globallycoherent RWByteAddressBuffer gOmmContentEpochs;
uint rt_omm_atomic_epoch(uint offset) {
  uint value;gOmmContentEpochs.InterlockedOr(offset,0u,value);return value;
}
bool rt_omm_scene_current() {
  const uint4 proof=gSceneStamp.Load4(128u);
  if(proof.x==0u || proof.x!=gSceneStamp.Load(0u) || proof.z==0u ||
     proof.w!=0x4f4d4d32u || rt_omm_atomic_epoch(8u)!=0u)return false;
  DeviceMemoryBarrier();
  const bool current=rt_omm_atomic_epoch(4u)==0u && proof.y==rt_omm_atomic_epoch(0u);
  DeviceMemoryBarrier();
  return current && rt_omm_atomic_epoch(8u)==0u && proof.y==rt_omm_atomic_epoch(0u);
}
