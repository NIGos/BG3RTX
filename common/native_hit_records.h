#pragma once
#ifdef __cplusplus
#include <cstdint>
namespace bg3::native_hit {
#define NH_UINT inline constexpr uint32_t
#else
#define NH_UINT static const uint
#endif
NH_UINT nh_alpha_rows=8192u;
// Capacities of the hit-material capture (rt_hit_material). Opaque prepass rows follow the
// 8192 alpha rows in the query rows buffer; deferred rows are per deferred packet. The cache
// view and sampler counts are the AlphaDSL array sizes of bindings 14/15 (layer and shader).
// Before 2026-10-31: 128 opaque rows, 128 deferred rows, 64 cache views, 16 samplers.
NH_UINT nh_opaque_rows=4096u;
NH_UINT nh_material_rows=nh_alpha_rows+nh_opaque_rows;
NH_UINT nh_deferred_rows=4096u;
NH_UINT nh_cache_slots=256u;
NH_UINT nh_sampler_slots=64u;
NH_UINT nh_opaque_meta=1u<<13;
#undef NH_UINT
#ifdef __cplusplus
inline bool opaque_material_record(uint32_t ordinal,uint32_t& record){if(ordinal>=nh_opaque_rows)return false;record=nh_alpha_rows+ordinal;return true;}
inline bool appended_table_range(uint32_t used,uint32_t count,uint32_t capacity,uint32_t& first){
 if(used>=capacity || count>capacity-used-1u)return false;first=used+1u;return true;
}
}
#endif
