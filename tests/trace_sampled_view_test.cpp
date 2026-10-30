#include "../common/trace_sampled_view.h"
#include <cstdio>
#include <cstdlib>
#include <string>
using namespace bg3::trace_view;
static unsigned checks;
static void check(bool x){++checks;if(!x){std::fprintf(stderr,"view metadata check %u failed\n",checks);std::exit(1);}}
int main(int argc,char** argv){
 std::atomic<uint64_t> next{1};
 VkImageCreateInfo image{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};image.format=VK_FORMAT_BC3_UNORM_BLOCK;
 image.flags=VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT;image.mipLevels=4;image.arrayLayers=2;
 VkImageViewCreateInfo ci{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};ci.format=VK_FORMAT_BC3_SRGB_BLOCK;ci.viewType=VK_IMAGE_VIEW_TYPE_2D_ARRAY;
 ci.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,1,VK_REMAINING_MIP_LEVELS,0,VK_REMAINING_ARRAY_LAYERS};
 ci.components={VK_COMPONENT_SWIZZLE_B,VK_COMPONENT_SWIZZLE_G,VK_COMPONENT_SWIZZLE_R,VK_COMPONENT_SWIZZLE_A};
 auto first=capture(ci,allocate_id(next),17);
 auto serialize=[&](const Creation& c,uint32_t imageId=17){return json(ci.format,ci.subresourceRange,c,imageId,&image,VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);};
 const auto good=serialize(first);
 if(argc>1 && std::string(argv[1])=="--emit"){std::puts(good.c_str());return 0;}
 check(good.find("\"status\":\"known\"")!=std::string::npos);
 check(good.find("\"format\":138")!=std::string::npos);check(good.find("\"image_format\":137")!=std::string::npos);
 check(good.find("\"resolved_mips\":3")!=std::string::npos);check(good.find("\"resolved_layers\":2")!=std::string::npos);
 check(good.find("\"components\":[5,4,3,6]")!=std::string::npos);
 check(good.find("\"descriptor_type\":2")!=std::string::npos);check(good.find("\"descriptor_layout\":5")!=std::string::npos);
 // Same backing image and recycled external view handle still get distinct IDs.
 auto second=capture(ci,allocate_id(next),17);check(first.id!=second.id);check(serialize(second)!=good);
 check(serialize(first,18).find("stale_image_identity")!=std::string::npos);
 check(serialize(first,0).find("unavailable_image")!=std::string::npos);
 check(json(ci.format,ci.subresourceRange,first,17,nullptr,VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,VK_IMAGE_LAYOUT_GENERAL).find("unavailable_image")!=std::string::npos);
 check(serialize(Creation{}).find("unavailable_create_metadata")!=std::string::npos);
 VkImageViewMinLodCreateInfoEXT minLod{VK_STRUCTURE_TYPE_IMAGE_VIEW_MIN_LOD_CREATE_INFO_EXT};minLod.minLod=2.5f;ci.pNext=&minLod;
 check(serialize(capture(ci,allocate_id(next),17)).find("unsupported_extensions")!=std::string::npos);ci.pNext=nullptr;
 uint32_t resolved=99;
 check(resolve_count(1,UINT32_MAX,4,resolved) && resolved==3);
 check(resolve_count(1,2,4,resolved) && resolved==2);
 check(!resolve_count(4,1,4,resolved) && resolved==0);
 check(!resolve_count(0,0,4,resolved) && resolved==0);
 check(!resolve_count(1,4,4,resolved) && resolved==0);
 check(!resolve_count(UINT32_MAX,1,4,resolved) && resolved==0);
 ci.subresourceRange.baseMipLevel=4;check(serialize(first).find("invalid_range")!=std::string::npos);ci.subresourceRange.baseMipLevel=1;
 ci.subresourceRange.layerCount=3;check(serialize(first).find("invalid_range")!=std::string::npos);ci.subresourceRange.layerCount=VK_REMAINING_ARRAY_LAYERS;
 next=UINT64_MAX-1;check(allocate_id(next)==UINT64_MAX-1);check(allocate_id(next)==0);check(allocate_id(next)==0);check(next==UINT64_MAX);
 auto saturated=capture(ci,0,17);check(serialize(saturated).find("identity_saturated")!=std::string::npos);
 std::printf("sampled view metadata: %u checks; no device or GPU calls\n",checks);
}
