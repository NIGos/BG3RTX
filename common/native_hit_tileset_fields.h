#ifndef BG3_NATIVE_HIT_TILESET_FIELDS_H
#define BG3_NATIVE_HIT_TILESET_FIELDS_H
#include "native_vt_fields.h"
#ifdef __cplusplus
namespace bg3::transport {
#endif
#ifdef __cplusplus
static constexpr NvUint NtsStatQualified=287u,NtsStatPrimaryMatch=288u,NtsStatsCount=289u;
#else
// Macro expansion leaves the already-qualified shader arithmetic byte-identical.
#define NtsStatQualified 287u
#define NtsStatPrimaryMatch 288u
#endif
static const NvUint NtsRecipe=2u,NtsColorOffset=24u,NtsBasisOffset=40u,NtsMinimumStride=80u;
struct NtsPlan {NvUint branch,status;float tiling;};
inline NtsPlan nts_plan(float red,float green,NvVec3 tilings){
 NtsPlan r;r.branch=0;r.status=1;r.tiling=0;
 if(!tb_finite(red)||!tb_finite(green))return r;
 r.branch=green>0?0u:red>0?1u:2u;
 r.tiling=r.branch==0?tilings.x:r.branch==1?tilings.y:tilings.z;
 if(!tb_finite(r.tiling))return r;r.status=0;return r;
}
#ifdef __cplusplus
}
#endif
#endif
