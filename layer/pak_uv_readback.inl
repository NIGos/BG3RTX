// Included inside hit_material.cpp's private namespace, after Buffer/create and
// the recording-owner registry. Uses the existing one-time submit ownership.
using PakUvKey=std::array<uint64_t,16+CmdState::kMaxVertexBindings*3>;
struct PakUvEntry {
 mesh_paks::uv_join::Readback result;Buffer xfb,host;
 uint64_t recording{},command{};uint32_t count{},stride{};bool recorded{},invalid{};
 mesh_paks::WitnessLayout layout;const mesh_paks::Row* witness{};
 // 1 needs indices, 2 indices in flight, 3 needs raw range, 4 range in flight,
 // 5 verified, 6 rejected. Raw host storage is never recycled before retirement.
 uint32_t stage{},first{},last{};uint64_t index_offset{},pos_offset{},uv_offset{},pos_bytes{},uv_bytes{};
 VkBuffer index_buffer{},pos_buffer{},uv_buffer{};
 uint64_t copied{},chunk{};
 uint64_t index_storage{};uint32_t index_prefix{},pos_prefix{},uv_prefix{};
 std::shared_ptr<std::atomic<bool>> valid=std::make_shared<std::atomic<bool>>(true);
};
std::map<PakUvKey,PakUvEntry> pak_uv_cache; // guarded by mutex
mesh_paks::uv_join::Budget pak_budget;
uint64_t pak_cpu_frame=UINT64_MAX;uint32_t pak_cpu_jobs{};
bool pak_active(const PakUvEntry& e){return e.xfb.b || (e.host.b && (e.stage==2||e.stage==4));}
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
void pak_uv_live_owners(std::set<uint64_t>& live){for(const auto& [key,e]:pak_uv_cache)if(pak_active(e))live.insert(e.recording);}
void pak_uv_poll(){
 if(pak_cpu_frame!=frame_index()){pak_cpu_frame=frame_index();pak_cpu_jobs=0;}
 for(auto it=pak_uv_cache.begin();it!=pak_uv_cache.end();){auto& e=it->second;
  if(!pak_active(e)){++it;continue;}
  const auto owner=owners.find(e.recording);
  if(owner==owners.end() || !owner->second.retired(current_recording(e.command),life_gpu_done())){++it;continue;}
  const bool discarded=!owner->second.submitted || !e.recorded;
  if(e.stage && !discarded && !e.invalid){
   if(pak_cpu_jobs>=2){++it;continue;}++pak_cpu_jobs;
   const auto* data=static_cast<const uint8_t*>(e.host.mapped);const uint64_t ib=uint64_t(e.count)*e.layout.index_bytes;
   if(e.stage==2){
    e.stage=mesh_paks::witness_span(e.layout,{data+e.index_prefix,ib},e.first,e.last)?3:6;
    if(e.stage==6)++pak_witness_mismatch;
   }else if(e.stage==4 && e.copied+e.chunk<e.host.bytes){e.copied+=e.chunk;e.stage=3;}
   else if(e.stage==4){
    std::vector<float> corners;
    const bool ok=mesh_paks::decode_witness(e.layout,{data+e.index_prefix,ib},{data+e.index_storage+e.pos_prefix,e.pos_bytes-e.pos_prefix},{data+e.index_storage+e.pos_bytes+e.uv_prefix,e.uv_bytes-e.uv_prefix},e.first,corners) &&
     mesh_paks::witness_match(*e.witness,corners,shader_cache::sha256_hex);
    e.stage=ok?5:6;if(ok)++pak_witness_ok;else ++pak_witness_mismatch;
   }
  }else if(!e.stage)e.result.finish(true,!discarded && !e.invalid,static_cast<const float*>(e.host.mapped));
  if(!e.stage && e.result.state==mesh_paks::uv_join::State::Ready){
   const auto match=mesh_paks::lookup(pak_meshes,e.count,nullptr,e.result.samples.data());
   log("[hit material paks UV] %u corners: %s candidate; full geometry verification still required",e.count,
    match.status==mesh_paks::Match::Unique?"unique":match.status==mesh_paks::Match::Ambiguous?"ambiguous":"missing");
   if(match.status==mesh_paks::Match::Unique){e.witness=match.row;e.stage=1;}
  }
  destroy(e.xfb);if(e.stage!=3 || e.copied==0)destroy(e.host);
  // A reset/dropped command produced no evidence: allow a new one-time capture.
  if(e.invalid || discarded){destroy(e.xfb);destroy(e.host);it=pak_uv_cache.erase(it);}else ++it;
 }
}
bool pak_witness_get(const CmdState& s,const DrawArgs& a,const InstPipe& ip,const mesh_paks::Row* row,std::shared_ptr<std::atomic<bool>>* proof=nullptr){
 std::lock_guard lock(mutex);pak_uv_poll();const auto it=pak_uv_cache.find(pak_uv_key(s,a,ip));
 const bool ok=it!=pak_uv_cache.end() && !it->second.invalid && it->second.stage==5 && it->second.witness==row && it->second.valid->load(std::memory_order_relaxed);
 if(ok && proof)*proof=it->second.valid;return ok;
}
std::shared_ptr<std::atomic<bool>> pak_witness_token(const CmdState& s,const DrawArgs& a,const InstPipe& ip){
 std::lock_guard lock(mutex);const auto i=pak_uv_cache.find(pak_uv_key(s,a,ip));
 return i!=pak_uv_cache.end()?i->second.valid:nullptr;
}
bool pak_uv_forget_cached_buffer(VkBuffer b){
 bool changed=false;
 for(auto it=pak_uv_cache.begin();it!=pak_uv_cache.end();){const auto& key=it->first;auto& e=it->second;
  bool uses=key[0]==uint64_t(b)||key[6]==uint64_t(b);
  for(uint32_t binding=0;binding<CmdState::kMaxVertexBindings;++binding)uses|=key[16+binding*3]==uint64_t(b);
  if(!uses){++it;continue;}changed=true;e.valid->store(false,std::memory_order_relaxed);
  if(pak_active(e)){e.invalid=true;++it;}else{destroy(e.xfb);destroy(e.host);it=pak_uv_cache.erase(it);}
 }return changed;
}
void pak_witness_reject(PakUvEntry& e){destroy(e.host);e.stage=6;++pak_witness_mismatch;}
// No waits: both raw phases use the same recording-owner retirement as XFB.
void pak_witness_capture(VkCommandBuffer cmd,CmdState* s,const DrawArgs& a,const InstPipe& ip,PakUvEntry& e){
 if(pak_active(e) || (e.stage!=1 && e.stage!=3))return;
 // Discovery also sees ordinary pool recipes. Do not spend raw transfer or
 // decode budget on a candidate that cannot pass the publication contract.
 // Keep stage 1 retryable: a later compatible draw must still be able to run.
 if(!e.witness || !mesh_paks::hlod_compatible(*e.witness,s->vs,s->fs,ip.paks_cull_known,ip.paks_two_sided))return;
 if(!ip.pos_float3 || ip.paks_uv_binding>=CmdState::kMaxVertexBindings){pak_witness_reject(e);return;}
 const auto& p=s->vb[ip.pos_binding];const auto& u=s->vb[ip.paks_uv_binding];
 e.layout={a.count,s->ib_type==VK_INDEX_TYPE_UINT32?4u:s->ib_type==VK_INDEX_TYPE_UINT16?2u:s->ib_type==VK_INDEX_TYPE_UINT8_EXT?1u:0u,
  a.vertex_offset,ip.pos_stride?ip.pos_stride:uint32_t(p.stride),ip.paks_uv_stride?ip.paks_uv_stride:uint32_t(u.stride),ip.paks_uv_half};
 const uint64_t ib=uint64_t(a.count)*e.layout.index_bytes;
 e.index_buffer=s->ib;e.pos_buffer=p.buffer;e.uv_buffer=u.buffer;
 if(pak_raw_mutable(e.index_buffer)||pak_raw_mutable(e.pos_buffer)||pak_raw_mutable(e.uv_buffer)){pak_witness_reject(e);return;}
 const uint64_t delta=uint64_t(a.first)*e.layout.index_bytes;
 if(s->ib_offset>UINT64_MAX-delta || p.offset>UINT64_MAX-ip.pos_offset || u.offset>UINT64_MAX-ip.paks_uv_offset){pak_witness_reject(e);return;}
 e.index_offset=s->ib_offset+delta;
 e.pos_offset=p.offset+ip.pos_offset;e.uv_offset=u.offset+ip.paks_uv_offset;
 auto fits=[](VkBuffer b,uint64_t at,uint64_t n){const auto size=geo_buffer_size(b);return n && at<=size && n<=size-at && (geo_buffer_usage(b)&VK_BUFFER_USAGE_TRANSFER_SRC_BIT);};
 if(e.stage==3){const uint64_t span=uint64_t(e.last)-e.first;
  if(e.pos_offset>UINT64_MAX-uint64_t(e.first)*e.layout.pos_stride || e.uv_offset>UINT64_MAX-uint64_t(e.first)*e.layout.uv_stride){pak_witness_reject(e);return;}
  e.pos_offset+=uint64_t(e.first)*e.layout.pos_stride;e.uv_offset+=uint64_t(e.first)*e.layout.uv_stride;
  e.pos_bytes=span*e.layout.pos_stride+12;e.uv_bytes=span*e.layout.uv_stride+(e.layout.uv_half?4:8);
 }
 e.index_storage=ib;
 if(e.pos_bytes>mesh_paks::uv_join::Budget::kFlightBytes || e.uv_bytes>mesh_paks::uv_join::Budget::kFlightBytes){pak_witness_reject(e);return;}
 const uint64_t size=e.index_storage+(e.stage==3?e.pos_bytes+e.uv_bytes:0);
 if(!e.layout.index_bytes || !fits(e.index_buffer,e.index_offset,e.index_storage) ||
    (e.stage==3 && (!fits(e.pos_buffer,e.pos_offset,e.pos_bytes)||!fits(e.uv_buffer,e.uv_offset,e.uv_bytes))) || size>mesh_paks::uv_join::Budget::kFlightBytes || (e.stage==1 && size>mesh_paks::uv_join::Budget::kFrameBytes)){pak_witness_reject(e);return;}
 uint64_t pending=0;uint32_t jobs=0;for(const auto& [k,x]:pak_uv_cache){pending+=x.xfb.bytes+x.host.bytes;if(pak_active(x))++jobs;}
 const uint64_t chunk=(std::min)(size-e.copied,mesh_paks::uv_join::Budget::kFrameBytes);
 const uint64_t allocation=e.host.b?0:size;
 if(pending>mesh_paks::uv_join::Budget::kFlightBytes-allocation || !pak_budget.take(frame_index(),chunk,jobs,0)){++pak_budget_deferred;return;}
 if(!own(cmd)||(!e.host.b && !create(e.host,size,VK_BUFFER_USAGE_TRANSFER_DST_BIT,true)))return;
 e.chunk=chunk;
 e.recording=s->recording;e.command=handle(cmd);e.recorded=false;e.stage=e.stage==1?2:4;
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
 if(auto it=pak_uv_cache.find(key);it!=pak_uv_cache.end()){pak_witness_capture(cmd,s,a,ip,it->second);return false;}
 if(pak_uv_cache.size()>=8192)return false;
 uint64_t pending=0;uint32_t jobs=0;for(const auto& [k,e]:pak_uv_cache){pending+=e.xfb.bytes+e.host.bytes;if(pak_active(e))++jobs;}
 if(size+mesh_paks::uv_join::kBytes>mesh_paks::uv_join::Budget::kFlightBytes ||
    pending>mesh_paks::uv_join::Budget::kFlightBytes-size-mesh_paks::uv_join::kBytes ||
    !pak_budget.take(frame_index(),mesh_paks::uv_join::kBytes,jobs,0)){++pak_budget_deferred;return false;}
 if(!own(cmd))return false;
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
 bool work=false;for(const auto& [k,e]:pak_uv_cache)work|=pak_active(e) && !e.recorded && e.recording==s->recording;
 if(!work)return;
 barrier(cmd,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,VK_ACCESS_MEMORY_WRITE_BIT,
  VK_PIPELINE_STAGE_TRANSFER_BIT,VK_ACCESS_TRANSFER_READ_BIT);
 for(auto& [key,e]:pak_uv_cache){
  if(!pak_active(e) || e.recorded || e.recording!=s->recording)continue;
  if(e.stage){
   const uint64_t ib=e.index_storage;
   auto copy=[&](VkBuffer source,uint64_t offset,uint64_t dest,uint64_t bytes){
    const auto lo=(std::max)(dest,e.copied),hi=(std::min)(dest+bytes,e.copied+e.chunk);
    if(lo<hi){VkBufferCopy c{offset+lo-dest,lo,hi-lo};dd->vk.CmdCopyBuffer(cmd,source,e.host.b,1,&c);}};
   copy(e.index_buffer,e.index_offset,0,ib);
   if(e.stage==4){copy(e.pos_buffer,e.pos_offset,ib,e.pos_bytes);copy(e.uv_buffer,e.uv_offset,ib+e.pos_bytes,e.uv_bytes);}
   e.recorded=true;continue;
  }
  std::array<uint64_t,32> offsets{};if(!mesh_paks::uv_join::offsets(e.count,e.stride,uint64_t(e.count)*e.stride,offsets))continue;
  VkBufferCopy copies[32];for(uint32_t i=0;i<32;++i)copies[i]={offsets[i],uint64_t(i)*8,8};
  dd->vk.CmdCopyBuffer(cmd,e.xfb.b,e.host.b,32,copies);e.recorded=true;
 }
 barrier(cmd,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_ACCESS_TRANSFER_WRITE_BIT,VK_PIPELINE_STAGE_HOST_BIT,VK_ACCESS_HOST_READ_BIT);
}
void pak_uv_destroy(){for(auto& [key,e]:pak_uv_cache){destroy(e.xfb);destroy(e.host);}pak_uv_cache.clear();}
