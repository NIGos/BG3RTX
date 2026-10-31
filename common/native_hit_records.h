#pragma once
#ifdef __cplusplus
#include <cstdint>
namespace bg3::native_hit {
#define NH_UINT inline constexpr uint32_t
#else
#define NH_UINT static const uint
#endif
NH_UINT nh_alpha_rows=8192u;
NH_UINT nh_opaque_rows=128u;
NH_UINT nh_material_rows=nh_alpha_rows+nh_opaque_rows;
NH_UINT nh_opaque_meta=1u<<13;
#undef NH_UINT
#ifdef __cplusplus
inline bool opaque_material_record(uint32_t ordinal,uint32_t& record){if(ordinal>=nh_opaque_rows)return false;record=nh_alpha_rows+ordinal;return true;}
inline bool appended_table_range(uint32_t used,uint32_t count,uint32_t capacity,uint32_t& first){
 if(used>=capacity || count>capacity-used-1u)return false;first=used+1u;return true;
}
}
#endif
