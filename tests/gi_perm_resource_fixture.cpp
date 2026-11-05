#include <cstdint>
#include <cassert>
#include <vector>
using VkImageUsageFlags=uint32_t;
enum { VK_IMAGE_USAGE_STORAGE_BIT=1, VK_IMAGE_USAGE_TRANSFER_SRC_BIT=2,
 VK_IMAGE_USAGE_TRANSFER_DST_BIT=4, VK_IMAGE_USAGE_SAMPLED_BIT=8,
 VK_FORMAT_R16_SFLOAT=10, VK_FORMAT_R16G16B16A16_SFLOAT,
 VK_FORMAT_R32_SFLOAT, VK_FORMAT_R8G8B8A8_UNORM, VK_FORMAT_R32G32B32A32_UINT,
 VK_FORMAT_R32G32B32A32_SFLOAT, BG3_DEFER_IMAGE_VIEW, BG3_DEFER_IMAGE, BG3_DEFER_MEMORY };
struct Bg3Image { uint64_t image=0,view=0,memory=0; };
Bg3Image g_raw,g_hist[2],g_raw_depth,g_hist_depth[2],g_prev_radiance,g_rt_vis,
 g_origin,g_aux,g_aux_ao,g_ls_nodata,g_rf_hit,g_hair_origin;
uint32_t g_w=32,g_h=16,g_hw=16,g_hh=8,g_shift=1;
bool g_aux_ao_full=false,g_aux_ao_ready=true,g_hair_origin_cap=false;
uint32_t gi_res_shift(){return 1;}
void release_images(){g_w=g_h=0;g_raw={};g_aux_ao={};}
struct Host {
 bool narrow=true,fail=false; unsigned allocations=0;uint64_t serial=1000;
 std::vector<uint64_t> retired;
 int cfg_int(const char* key,int){return key[0]=='d'?1:narrow;}
 bool alloc_image(uint32_t w,uint32_t h,int,VkImageUsageFlags,Bg3Image* out){
  ++allocations;if(fail)return false;assert(w && h);
  *out={++serial,++serial,++serial};return true;
 }
 void defer_destroy(int,uint64_t handle){retired.push_back(handle);}
 template<class... T>void log(const char*,T...){}
} state,*host=&state;
// INSERT ensure_images
int main(){
 g_raw.image=100;g_aux_ao={200,201,202};
 state.fail=true;
 assert(!ensure_images(32,16));
 assert(g_raw.image==100 && g_aux_ao.image==200 && !g_aux_ao_full);
 assert(state.retired.empty() && g_aux_ao_ready);
 state.fail=false;
 assert(ensure_images(32,16));
 assert(g_raw.image==100 && g_aux_ao.image!=200 && g_aux_ao_full && !g_aux_ao_ready);
 assert((state.retired==std::vector<uint64_t>{201,200,202}));
 unsigned count=state.allocations;
 state.narrow=false;
 assert(ensure_images(32,16) && state.allocations==count && g_aux_ao_full);
 state.narrow=true;
 assert(ensure_images(32,16) && state.allocations==count && g_raw.image==100);
 // Resize takes the ordinary image path and refreshes scalar state.
 state.narrow=false;
 assert(ensure_images(64,32) && g_w==64 && g_h==32 && !g_aux_ao_full && !g_aux_ao_ready);
}
