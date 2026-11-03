// Included inside hit_material.cpp's private namespace, after Buffer/create and
// the recording-owner registry. Uses the existing one-time submit ownership.
using PakUvKey=std::array<uint64_t,16+CmdState::kMaxVertexBindings*3>;
struct PakUvEntry {
 mesh_paks::uv_join::Readback result;Buffer xfb,host;
 uint64_t recording{},command{};uint32_t count{},stride{};bool recorded{},invalid{};
};
std::map<PakUvKey,PakUvEntry> pak_uv_cache; // guarded by mutex
PakUvKey pak_uv_key(const CmdState& s,const DrawArgs& a,const InstPipe& ip){
 const auto& p=s.vb[ip.pos_binding];
 PakUvKey key{uint64_t(s.ib),s.ib_offset,uint64_t(s.ib_type),a.count,a.first,uint64_t(int64_t(a.vertex_offset)),
  uint64_t(p.buffer),p.offset+ip.pos_offset,0,0,0,ip.vtx_sig,s.vs,s.fs,
  ip.pos_stride?ip.pos_stride:uint32_t(p.stride),uint64_t(s.pipe[0])};
 // XFB already contains decoded UVs. A raw float2 binding may not exist;
 // retain every per-vertex input instead of indexing the 0xff sentinel.
 for(uint32_t b=0;b<CmdState::kMaxVertexBindings;++b)if(ip.vtx_bindings&(1u<<b)){
  key[16+b*3]=uint64_t(s.vb[b].buffer);key[17+b*3]=s.vb[b].offset;key[18+b*3]=s.vb[b].stride;
 }
 return key;
}
void pak_uv_live_owners(std::set<uint64_t>& live){for(const auto& [key,e]:pak_uv_cache)if(e.xfb.b)live.insert(e.recording);}
void pak_uv_poll(){
 for(auto it=pak_uv_cache.begin();it!=pak_uv_cache.end();){auto& e=it->second;
  if(!e.xfb.b){++it;continue;}
  const auto owner=owners.find(e.recording);
  if(owner==owners.end() || !owner->second.retired(current_recording(e.command),life_gpu_done())){++it;continue;}
  const bool discarded=!owner->second.submitted || !e.recorded;
  e.result.finish(true,!discarded && !e.invalid,static_cast<const float*>(e.host.mapped));
  if(e.result.state==mesh_paks::uv_join::State::Ready){
   const auto match=mesh_paks::lookup(pak_meshes,e.count,nullptr,e.result.samples.data());
   log("[hit material paks UV] %u corners: %s candidate; full geometry verification still required",e.count,
    match.status==mesh_paks::Match::Unique?"unique":match.status==mesh_paks::Match::Ambiguous?"ambiguous":"missing");
  }
  destroy(e.xfb);destroy(e.host);
  // A reset/dropped command produced no evidence: allow a new one-time capture.
  if(e.invalid || discarded)it=pak_uv_cache.erase(it);else ++it;
 }
}
bool pak_uv_get(const CmdState& s,const DrawArgs& a,const InstPipe& ip,std::array<float,64>& out){
 std::lock_guard lock(mutex);pak_uv_poll();const auto it=pak_uv_cache.find(pak_uv_key(s,a,ip));
 if(it==pak_uv_cache.end() || it->second.invalid || it->second.result.state!=mesh_paks::uv_join::State::Ready)return false;
 out=it->second.result.samples;return true;
}
bool pak_uv_capture(VkCommandBuffer cmd,CmdState* s,const DrawArgs& a,const InstPipe& ip,uint32_t stride,
                    VkBuffer* xfb,VkDeviceSize* offset,VkDeviceSize* bytes){
 if(!paks_rows() || !one_time(s) || !ip.ok || ip.pos_binding>=CmdState::kMaxVertexBindings ||
    a.kind!=1 || !a.instances || s->vb[ip.pos_binding].offset>UINT64_MAX-ip.pos_offset)return false;
 const bool candidate=mesh_paks::has_candidates(pak_meshes,a.count);
 const uint64_t size=uint64_t(a.count)*stride;std::array<uint64_t,32> copies{};
 if(!candidate || size>pool::kJobXfbBytes || !mesh_paks::uv_join::offsets(a.count,stride,size,copies))return false;
 std::lock_guard lock(mutex);pak_uv_poll();const auto key=pak_uv_key(*s,a,ip);
 if(pak_uv_cache.contains(key) || pak_uv_cache.size()>=8192)return false;
 uint64_t pending=0;uint32_t jobs=0;for(const auto& [k,e]:pak_uv_cache)if(e.xfb.b){pending+=e.xfb.bytes;++jobs;}
 if(jobs>=16 || pending+size>pool::kJobXfbBytes || !own(cmd))return false;
 PakUvEntry e;e.count=a.count;e.stride=stride;e.recording=s->recording;e.command=handle(cmd);
 if(!create(e.xfb,size,VK_BUFFER_USAGE_TRANSFORM_FEEDBACK_BUFFER_BIT_EXT|VK_BUFFER_USAGE_TRANSFER_SRC_BIT) ||
    !create(e.host,mesh_paks::uv_join::kBytes,VK_BUFFER_USAGE_TRANSFER_DST_BIT,true)){
  destroy(e.xfb);destroy(e.host);return false;
 }
 std::memset(e.host.mapped,0xff,mesh_paks::uv_join::kBytes);e.result.begin();
 *xfb=e.xfb.b;*offset=0;*bytes=size;pak_uv_cache.emplace(key,std::move(e));return true;
}
// EndRenderPass, before any deferred packet early return. Only the current
// primary one-time recording can populate its retained readback.
void pak_uv_record(VkCommandBuffer cmd,CmdState* s){
 bool work=false;for(const auto& [k,e]:pak_uv_cache)work|=e.xfb.b && !e.recorded && e.recording==s->recording;
 if(!work)return;
 barrier(cmd,VK_PIPELINE_STAGE_TRANSFORM_FEEDBACK_BIT_EXT,VK_ACCESS_TRANSFORM_FEEDBACK_WRITE_BIT_EXT,
  VK_PIPELINE_STAGE_TRANSFER_BIT,VK_ACCESS_TRANSFER_READ_BIT);
 for(auto& [key,e]:pak_uv_cache){
  if(!e.xfb.b || e.recorded || e.recording!=s->recording)continue;
  std::array<uint64_t,32> offsets{};if(!mesh_paks::uv_join::offsets(e.count,e.stride,uint64_t(e.count)*e.stride,offsets))continue;
  VkBufferCopy copies[32];for(uint32_t i=0;i<32;++i)copies[i]={offsets[i],uint64_t(i)*8,8};
  dd->vk.CmdCopyBuffer(cmd,e.xfb.b,e.host.b,32,copies);e.recorded=true;
 }
 barrier(cmd,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_ACCESS_TRANSFER_WRITE_BIT,VK_PIPELINE_STAGE_HOST_BIT,VK_ACCESS_HOST_READ_BIT);
}
void pak_uv_destroy(){for(auto& [key,e]:pak_uv_cache){destroy(e.xfb);destroy(e.host);}pak_uv_cache.clear();}
