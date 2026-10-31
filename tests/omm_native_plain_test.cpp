#include "../layer/omm_native_plain.h"
#include "../layer/omm_submit.h"
#include <cstdio>
#include <cstdlib>
namespace bg3 {
Tracker tracking;CmdState state;Bg3Binding texture;unsigned notes;
bool recording=true,frozen=true,currentSampler=true,sourceAvailable=true;
omm::ContentSource source;omm::BoundSampler sampler;
Tracker& tracker(){return tracking;}
CmdState* cmd_state(VkCommandBuffer,bool){return &state;}
bool query_binding(VkCommandBuffer,VkPipelineBindPoint,uint32_t,uint32_t,uint32_t,Bg3Binding* out){*out=texture;return true;}
void life_note(VkCommandBuffer,uint64_t h){if(h)++notes;}
namespace omm {
bool tracked_recording(VkCommandBuffer){return recording;}
bool sampler_tracking_active(){return true;}
bool ordinary_frozen_binding(VkCommandBuffer,VkPipelineBindPoint,uint32_t,uint32_t,uint32_t,OrdinaryBindingFacts*){return frozen;}
bool content_source_view(VkImageView,ContentSource& out){out=source;return sourceAvailable;}
bool bound_sampler(VkCommandBuffer,VkPipelineBindPoint,uint32_t,uint32_t,uint32_t,BoundSampler& out){out=sampler;return true;}
bool immutable_sampler_current(const BoundSampler&){return currentSampler;}
}
}
using namespace bg3;using namespace bg3::omm;
static unsigned checks;
static void check(bool value){++checks;if(!value){std::fprintf(stderr,"check %u failed\n",checks);std::exit(1);}}
template<class T>static T h(uintptr_t n){return reinterpret_cast<T>(n);}
int main(){
 const auto& recipe=native_plain_recipes[0];auto cmd=h<VkCommandBuffer>(1);
 const auto image=h<VkImage>(2);const auto view=h<VkImageView>(3);const auto buffer=h<VkBuffer>(4);
 state.recording=10;state.vs=recipe.vs;state.fs=recipe.fs;
 ImageRec im{};im.id=17;tracking.images[image]=im;tracking.views[view]={image,VK_FORMAT_R8G8B8A8_UNORM,{}};
 texture.type=VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;texture.view=view;texture.image=image;texture.layout=VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
 source.buffer=buffer;source.bytes=1024;source.imageOffset=16;source.imageId=21;source.viewId=22;source.slot=1;source.levelCount=4;source.requiredSamplingCertificate=1;
 VkSamplerCreateInfo ci{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};ci.magFilter=ci.minFilter=VK_FILTER_LINEAR;ci.mipmapMode=VK_SAMPLER_MIPMAP_MODE_LINEAR;
 ci.addressModeU=ci.addressModeV=ci.addressModeW=VK_SAMPLER_ADDRESS_MODE_REPEAT;ci.maxLod=16;ci.maxAnisotropy=1;
 sampler.sampler=sampler_metadata(1,h<VkSampler>(5),ci);sampler.immutable=true;sampler.descriptorSet=h<VkDescriptorSet>(6);
 AlphaFs fs{};fs.kind=2;fs.hash=recipe.fs;fs.uv=0;fs.cut=fs.cut_first=.5f;fs.tex_set=1;fs.tex_bind=1;fs.channel=3;fs.k=1;
 AlphaSnapshot alpha{};alpha.tex=view;alpha.tex_id=17;alpha.tex_layout=texture.layout;alpha.rec.kind=2;alpha.rec.channel=3;alpha.rec.cut=alpha.rec.cut_first=.5f;
 alpha.rec.scale[0]=alpha.rec.scale[1]=1;alpha.rec.fs_lo=uint32_t(fs.hash);alpha.rec.fs_hi=uint32_t(fs.hash>>32);
 NativePlainCapture capture;auto take=[&]{return capture_native_plain(cmd,fs,recipe.vs,alpha,&capture);};
 check(take());check(notes==4);const auto good=capture;PlainInput input;auto owner=std::make_shared<int>(42);
 auto build=[&]{return make_native_plain_input(cmd,good,buffer,1024,0,24,8,owner,&input);};
 check(build());check(input.push.sampling_certificate==2 && input.push.threshold_lo==0 && input.push.threshold_hi==1);
 check(input.push.max_lod==3 && input.push.channel==3 && input.sampled_owner==owner);
 check(!make_native_plain_input(cmd,good,buffer,1024,0,24,8,{},&input));check(!input.vertices);
 check(!make_native_plain_input(cmd,good,buffer,1024,0,20,8,owner,&input));
 check(!make_native_plain_input(cmd,good,buffer,1024,0,24,100,owner,&input));
 check(!make_native_plain_input(cmd,good,buffer,UINT64_MAX,0,24,8,owner,&input));
 state.recording++;check(!build());state.recording--;
 check(!make_native_plain_input(h<VkCommandBuffer>(99),good,buffer,1024,0,24,8,owner,&input));
 source.viewId++;check(!build());source.viewId--;source.imageId++;check(!build());source.imageId--;
 source.baseMipLevel++;check(!build());source.baseMipLevel--;source.imageOffset+=4;check(!build());source.imageOffset-=4;
 currentSampler=false;check(!build());currentSampler=true;
 tracking.images[image].id++;check(!build());tracking.images[image].id--;
 recording=false;check(!take());check(!build());recording=true;
 frozen=false;check(!take());frozen=true;
 sampler.immutable=false;check(!take());sampler.immutable=true;
 sampler.sampler.info.addressModeU=VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;check(!take());sampler.sampler.info.addressModeU=VK_SAMPLER_ADDRESS_MODE_REPEAT;
 sourceAvailable=false;check(!take());sourceAvailable=true;
 source.imageOffset=8;check(!take());source.imageOffset=16;
 source.levelCount=18;check(!take());source.levelCount=4;
 source.requiredSamplingCertificate=0;check(!take());source.requiredSamplingCertificate=2;check(take());source.requiredSamplingCertificate=1;
 state.fs++;check(!take());state.fs--;state.vs++;check(!take());state.vs--;
 fs.has_scale=true;check(!take());fs.has_scale=false;fs.has_ubo=true;check(!take());fs.has_ubo=false;
 fs.cut_first=0;check(!take());fs.cut_first=.5f;alpha.rec.scale[0]=2;check(!take());alpha.rec.scale[0]=1;
 texture.view=h<VkImageView>(99);check(!take());texture.view=view;texture.layout=VK_IMAGE_LAYOUT_GENERAL;check(!take());texture.layout=alpha.tex_layout;
 alpha.rec.fs_lo++;check(!take());alpha.rec.fs_lo--;alpha.rec.cut=.6f;check(!take());alpha.rec.cut=.5f;
 alpha.cache=view;check(!take());alpha.cache={};check(take());
 const auto wind=native_plain_recipes[1];state.fs=wind.fs;state.vs=wind.vs;fs.hash=wind.fs;check(!capture_native_plain(cmd,fs,wind.vs,alpha,&capture));
 std::printf("native plain factory: %u checks, no Vulkan calls\n",checks);
}
