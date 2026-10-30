#include "../common/omm_sampler_metadata.h"
#include <cstdio>
#include <cstdlib>
using namespace bg3::omm;
static unsigned checks;
static void check(bool v){++checks;if(!v){std::fprintf(stderr,"check %u failed\n",checks);std::exit(1);}}
int main(){
  const auto sampler=reinterpret_cast<VkSampler>(uintptr_t(1));
  VkSamplerCreateInfo ci{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};ci.minFilter=ci.magFilter=VK_FILTER_LINEAR;
  ci.mipmapMode=VK_SAMPLER_MIPMAP_MODE_LINEAR;ci.addressModeU=ci.addressModeV=ci.addressModeW=VK_SAMPLER_ADDRESS_MODE_REPEAT;ci.maxLod=16;ci.maxAnisotropy=1;
  auto capture=[&]{return sampler_metadata(1,sampler,ci);};
  check(capture().complete);check(repeat_linear_sampler(capture()));
  ci.minLod=2;ci.maxLod=7;ci.mipLodBias=.25f;
  auto copy=capture();check(copy.info.minLod==2 && copy.info.maxLod==7 && copy.info.mipLodBias==.25f);
  ci.minLod=0;ci.maxLod=16;ci.mipLodBias=0;
  ci.addressModeU=VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;ci.borderColor=VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
  check(!repeat_linear_sampler(capture()));check(capture().info.borderColor==VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE);ci.addressModeU=VK_SAMPLER_ADDRESS_MODE_REPEAT;
  ci.minFilter=VK_FILTER_NEAREST;check(!repeat_linear_sampler(capture()));ci.minFilter=VK_FILTER_LINEAR;
  ci.magFilter=VK_FILTER_NEAREST;check(!repeat_linear_sampler(capture()));ci.magFilter=VK_FILTER_LINEAR;
  ci.mipmapMode=VK_SAMPLER_MIPMAP_MODE_NEAREST;check(!repeat_linear_sampler(capture()));ci.mipmapMode=VK_SAMPLER_MIPMAP_MODE_LINEAR;
  ci.compareEnable=VK_TRUE;ci.compareOp=VK_COMPARE_OP_GREATER;check(!repeat_linear_sampler(capture()));check(capture().info.compareOp==VK_COMPARE_OP_GREATER);ci.compareEnable=VK_FALSE;
  ci.anisotropyEnable=VK_TRUE;ci.maxAnisotropy=16;check(!repeat_linear_sampler(capture()));check(capture().info.maxAnisotropy==16);ci.anisotropyEnable=VK_FALSE;
  ci.unnormalizedCoordinates=VK_TRUE;check(!repeat_linear_sampler(capture()));ci.unnormalizedCoordinates=VK_FALSE;
  VkSamplerReductionModeCreateInfo reduction{VK_STRUCTURE_TYPE_SAMPLER_REDUCTION_MODE_CREATE_INFO};reduction.reductionMode=VK_SAMPLER_REDUCTION_MODE_MIN;ci.pNext=&reduction;
  check(capture().complete);check(!repeat_linear_sampler(capture()));check(capture().reduction==VK_SAMPLER_REDUCTION_MODE_MIN);
  reduction.reductionMode=VK_SAMPLER_REDUCTION_MODE_WEIGHTED_AVERAGE;check(repeat_linear_sampler(capture()));check(capture().info.pNext==nullptr);
  VkSamplerCustomBorderColorCreateInfoEXT custom{VK_STRUCTURE_TYPE_SAMPLER_CUSTOM_BORDER_COLOR_CREATE_INFO_EXT};ci.pNext=&custom;
  check(!capture().complete);check(capture().unsupportedStructure==custom.sType);check(!repeat_linear_sampler(capture()));
  VkSamplerYcbcrConversionInfo ycbcr{VK_STRUCTURE_TYPE_SAMPLER_YCBCR_CONVERSION_INFO};ci.pNext=&ycbcr;check(!capture().complete);check(capture().unsupportedStructure==ycbcr.sType);
  ci.pNext=nullptr;ci.minLod=20;check(!capture().complete);ci.minLod=0;
  ci.mipLodBias=NAN;check(!capture().complete);ci.mipLodBias=0;
  ci.maxLod=INFINITY;check(!capture().complete);ci.maxLod=16;
  SamplerRegistry registry;check(registry.create(1,sampler,ci));const auto old=*registry.lookup(1);
  registry.destroy(1);check(!registry.lookup(1));check(registry.create(1,sampler,ci));check(registry.lookup(1)->id!=old.id);
  registry.nextId=UINT64_MAX;check(!registry.create(2,reinterpret_cast<VkSampler>(uintptr_t(2)),ci));check(!registry.lookup(2));
  std::printf("Sampler metadata checks %u; no device calls\n",checks);
}
