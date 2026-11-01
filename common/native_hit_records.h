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
// Pool instances with a material row (native_hit_pool.h steps 2-4): InstInfo meta bit 15 on a
// non-alpha kInfoPool instance; rec = nh_pool_rec_flag | slot (never a valid alpha record
// address). The slot's 256 B row follows the query's control word at nh_pool_row_base; its
// UV/basis lives in the mesh pool (binding 8): a 16 B header {magic, mesh generation low 32,
// triangles, 1} and per vertex UV (8 B) + object-space N/B/T (VS locations 1/2/3, 36 B).
NH_UINT nh_pool_meta=1u<<15;
NH_UINT nh_pool_rec_flag=1u<<31;
NH_UINT nh_pool_rows=4096u;
NH_UINT nh_pool_row_base=32u+nh_material_rows*256u;
NH_UINT nh_pool_basis_header=16u;
NH_UINT nh_pool_basis_vertex=44u;
NH_UINT nh_pool_basis_magic=0x53414250u; // "PBAS"
// Pool row words 224..255 (Row::padding, LayoutDetail recipes only): 224 layout (1 = detail),
// 228/232 the detail normal/mask views (texture-table indices), 236 the detail sampler slot,
// 240..252 strength, tiling, mask power, roughness add (floats; native_vt_fields.h nv_detail_fields).
// LayoutIridescent rows (224 = 2): 228..244 the five 2D views 1:2..1:6 (texture-table indices),
// sampled with the row sampler (word 24); the 1:1 floats at 48..60 (strength, tiling, iridescence,
// reflectance lo) and 64 (hi) in place of p0/p1 (no VT; native_vt_fields.h nv_iridescent_fields).
NH_UINT nh_pool_row_detail=224u;
#undef NH_UINT
#ifdef __cplusplus
inline bool opaque_material_record(uint32_t ordinal,uint32_t& record){if(ordinal>=nh_opaque_rows)return false;record=nh_alpha_rows+ordinal;return true;}
inline bool appended_table_range(uint32_t used,uint32_t count,uint32_t capacity,uint32_t& first){
 if(used>=capacity || count>capacity-used-1u)return false;first=used+1u;return true;
}
}
#endif
