#pragma once
// Included only by physical-Hair-origin entries; requires rt_common.hlsli.
#ifdef RT_HAIR_PHYSICAL_ORIGIN
#include "../../common/native_hair_receiver.h"
// Search ONLY the existing raster-depth matching interval. Never commit hits:
// a closer non-Scalp triangle must not hide a competing Scalp candidate behind it.
bool hpo_competing_receiver(float3 origin,float3 direction,float t0,float t1){
  float length=t1-t0;
  if(!hpr_window(t0,t1) || !rt_scene_ok() || !rt_ray_valid(origin,direction,length))return true;
  RayDesc ray=(RayDesc)0;ray.Origin=origin;ray.Direction=direction;ray.TMin=0;ray.TMax=length;
#ifdef RT_SCENE_REBASE
  ray.Origin=rt_scene_ray_origin(ray.Origin);
#endif
  RayQuery<RAY_FLAG_FORCE_NON_OPAQUE | RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES> query;
#ifdef RT_OMM_SCENE
  // Receiver identification must not depend on micromap shortcuts.
  query.TraceRayInline(gOmmReferenceScene,0,RT_HIT_MASK,ray);
#else
  query.TraceRayInline(gScene,0,RT_HIT_MASK,ray);
#endif
  uint visited=0,cap=rt_max_candidates();
  while(query.Proceed()){
    if(++visited>cap){query.Abort();return true;}
    if(query.CandidateType()!=CANDIDATE_NON_OPAQUE_TRIANGLE)return true;
    InstInfo ii=inst_info(query.CandidateInstanceIndex());
    const uint primitive=query.CandidatePrimitiveIndex();
    bool readable=(ii.meta&kInfoLegacy)==0u && (ii.meta&kInfoAlpha)!=0u &&
        primitive<ii.tris && rec_in_range(ii.rec);
    bool scalp=false;
    if(readable){
      const uint kind=gAlphaRec.Load(ii.rec+8u);
      uint bytes;
      if((ii.meta&kInfoPool)!=0u)gMeshPool.GetDimensions(bytes);
      else gOpaqueSoup.GetDimensions(bytes);
      readable=hpr_readable_geometry(readable,ii.meta&255u,
          tri_in_range(ii.base,primitive,ii.meta&255u,bytes),kind);
      scalp=kind==3u && hpr_camera_scalp(gAlphaRec.Load(ii.rec+40u),gAlphaRec.Load(ii.rec+44u));
      // A kind3 record from an unrecognised writer is not an identity proof.
      if(kind==3u && !scalp)readable=false;
    }
    if(hpr_reject(readable,scalp,query.CandidateTriangleRayT(),ray.TMax,false)){query.Abort();return true;}
  }
  // Instance FORCE_OPAQUE can override FORCE_NON_OPAQUE and prune traversal;
  // its automatic committed hit cannot certify an absent competing surface.
  return query.CommittedStatus()!=COMMITTED_NOTHING;
}
#endif
