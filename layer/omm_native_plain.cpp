#include "omm_native_plain.h"
#include "omm_submit.h"
#include <algorithm>
namespace bg3::omm {
namespace {
uint32_t view_image_id(VkImageView view) {
  std::shared_lock lock(tracker().mu);
  const auto v=tracker().views.find(view);if(v==tracker().views.end())return 0;
  const auto i=tracker().images.find(v->second.image);return i==tracker().images.end()?0:i->second.id;
}
bool same_source(const ContentSource& a,const ContentSource& b) {
  return a.buffer==b.buffer && a.bytes==b.bytes && a.imageOffset==b.imageOffset &&
    a.imageId==b.imageId && a.slot==b.slot && a.viewId==b.viewId &&
    a.baseMipLevel==b.baseMipLevel && a.levelCount==b.levelCount && a.format==b.format &&
    a.components.r==b.components.r && a.components.g==b.components.g &&
    a.components.b==b.components.b && a.components.a==b.components.a &&
    a.requiredSamplingCertificate==b.requiredSamplingCertificate;
}
bool admissible_source(const ContentSource& s) {
  return s.buffer && s.imageId && s.viewId && s.levelCount && s.levelCount<=17 &&
    s.imageOffset>=16 && s.imageOffset<=UINT32_MAX && s.imageOffset%4==0 &&
    s.imageOffset<=s.bytes && s.bytes-s.imageOffset>=4 &&
    (s.requiredSamplingCertificate==1 || s.requiredSamplingCertificate==2);
}
}
bool capture_native_plain(VkCommandBuffer cmd,const AlphaFs& fs,uint64_t vs,
    const AlphaSnapshot& alpha,NativePlainCapture* out) {
  if(!out)return false;*out={};
  if(!sampler_tracking_active() || !tracked_recording(cmd))return false;
  auto* state=cmd_state(cmd);const auto* recipe=native_plain_recipe(fs.hash,vs);
  if(!state || !state->recording || state->vs!=vs || state->fs!=fs.hash || !recipe ||
      recipe->vertexDeformation || fs.kind!=2 || fs.has_scale || fs.has_ubo || fs.k!=1.f ||
      !native_plain_record_matches(*recipe,fs.kind,uint32_t(fs.uv),fs.tex_set,fs.tex_bind,
        fs.channel,fs.cut,fs.cut_first,alpha.rec.scale[0],alpha.rec.scale[1]) ||
      alpha.rec.kind!=2 || alpha.rec.channel!=recipe->channel || alpha.rec.cut!=recipe->cutoff ||
      alpha.rec.cut_first!=recipe->cutoff ||
      alpha.rec.fs_lo!=uint32_t(fs.hash) || alpha.rec.fs_hi!=uint32_t(fs.hash>>32) ||
      !alpha.tex || alpha.cache || !alpha.tex_id || view_image_id(alpha.tex)!=alpha.tex_id ||
      (alpha.tex_layout!=VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL && alpha.tex_layout!=VK_IMAGE_LAYOUT_GENERAL))return false;
  Bg3Binding texture{};
  if(!ordinary_frozen_binding(cmd,VK_PIPELINE_BIND_POINT_GRAPHICS,recipe->textureSet,recipe->textureBinding,0) ||
      !query_binding(cmd,VK_PIPELINE_BIND_POINT_GRAPHICS,recipe->textureSet,recipe->textureBinding,0,&texture) ||
      texture.type!=VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE || texture.view!=alpha.tex || texture.layout!=alpha.tex_layout)return false;
  ContentSource source{};BoundSampler sampler{};
  if(!content_source_view(alpha.tex,source) || !admissible_source(source) ||
      !bound_sampler(cmd,VK_PIPELINE_BIND_POINT_GRAPHICS,recipe->samplerSet,recipe->samplerBinding,0,sampler) ||
      !sampler.immutable || !immutable_sampler_current(sampler) || !repeat_linear_sampler(sampler.sampler))return false;
  *out={cmd,state->recording,fs.hash,vs,alpha,source,sampler};
  life_note(cmd,uint64_t(alpha.tex));life_note(cmd,uint64_t(texture.image));
  life_note(cmd,uint64_t(sampler.sampler.sampler));life_note(cmd,uint64_t(sampler.descriptorSet));
  return true;
}
bool make_native_plain_input(VkCommandBuffer cmd,const NativePlainCapture& saved,VkBuffer vertices,
    VkDeviceSize bytes,uint32_t offset,uint32_t stride,uint32_t triangles,std::shared_ptr<void> owner,PlainInput* out) {
  if(!out)return false;*out={};
  const auto* recipe=native_plain_recipe(saved.fs,saved.vs);auto* state=cmd_state(cmd);
  ContentSource current{};
  if(!recipe || recipe->vertexDeformation || !state || !state->recording || saved.command!=cmd ||
      saved.recording!=state->recording || !tracked_recording(cmd) || !vertices || !owner ||
      bytes>UINT32_MAX || !saved.alpha.tex_id || view_image_id(saved.alpha.tex)!=saved.alpha.tex_id ||
      !content_source_view(saved.alpha.tex,current) || !admissible_source(current) ||
      !same_source(saved.source,current) || !immutable_sampler_current(saved.sampler) ||
      !repeat_linear_sampler(saved.sampler.sampler))return false;
  PlainInput input{};input.vertices=vertices;input.vertex_bytes=bytes;input.mask=saved.alpha.tex;
  input.layout=saved.alpha.tex_layout;input.push.vertex_offset=offset;input.push.vertex_stride=stride;
  input.push.triangles=triangles;input.push.channel=recipe->channel;input.push.source_bytes=uint32_t(bytes);
  input.push.max_lod=current.levelCount-1;input.push.sampling_certificate=uint32_t(SamplingCertificate::UniformDecodedEndpoints);
  input.push.source_epoch_offset=uint32_t(current.imageOffset);
  // All stochastic thresholds [0,1], including hash0 equality. Zero stays UNKNOWN.
  // No numeric interval/error certificate is granted for mixed decoded texels.
  if(!valid_plain_push(input.push))return false;
  input.sampled_owner=std::move(owner);
  life_note(cmd,uint64_t(vertices));life_note(cmd,uint64_t(saved.alpha.tex));
  life_note(cmd,uint64_t(saved.sampler.sampler.sampler));life_note(cmd,uint64_t(saved.sampler.descriptorSet));
  *out=std::move(input);return true;
}
}
