// Exercise the production enqueue/copy/retire/cache path with a tiny fake device.
#include "../common/native_hit_join.h"
#include "../common/native_hit_pool.h"
#include "../common/native_hit_order.h"
#include "../layer/shader_cache.h"
#include <atomic>
#include <memory>
#include <shared_mutex>
#include <map>
#include <mutex>
#include <set>
#include <cstdarg>
namespace readback_fixture {
namespace mesh_paks=bg3::mesh_paks;
namespace pool {constexpr uint64_t kJobXfbBytes=16ull<<20;}
using VkBuffer=uint64_t;using VkDeviceSize=uint64_t;using VkCommandBuffer=uint64_t;
constexpr unsigned VK_BUFFER_USAGE_TRANSFORM_FEEDBACK_BUFFER_BIT_EXT=1,VK_BUFFER_USAGE_TRANSFER_SRC_BIT=2,
 VK_BUFFER_USAGE_TRANSFER_DST_BIT=4,VK_PIPELINE_STAGE_TRANSFORM_FEEDBACK_BIT_EXT=1,
 VK_ACCESS_TRANSFORM_FEEDBACK_WRITE_BIT_EXT=1,VK_PIPELINE_STAGE_TRANSFER_BIT=2,VK_ACCESS_TRANSFER_READ_BIT=2,
 VK_ACCESS_TRANSFER_WRITE_BIT=4,VK_PIPELINE_STAGE_HOST_BIT=4,VK_ACCESS_HOST_READ_BIT=8;
constexpr unsigned VK_PIPELINE_STAGE_ALL_COMMANDS_BIT=8,VK_ACCESS_MEMORY_WRITE_BIT=16,
 VK_INDEX_TYPE_UINT32=2,VK_INDEX_TYPE_UINT16=1,VK_INDEX_TYPE_UINT8_EXT=0;
struct VkBufferCopy {uint64_t srcOffset,dstOffset,size;};
struct Buffer {uint64_t b{},bytes{};void* mapped{};};
struct CmdState {
 static constexpr uint32_t kMaxVertexBindings=4;
 struct Binding {uint64_t buffer{},offset{},stride{};} vb[4];
 uint64_t ib=1,ib_offset{},ib_type=2,vs=3,fs=4,pipe[1]{5},recording=7;
};
struct DrawArgs {uint32_t count=3,first{},kind=1,instances=1;int32_t vertex_offset{};};
struct InstPipe {
 bool ok=true,uv_ok=true,pos_float3=true;uint32_t pos_binding=0,uv_binding=0xff,
 pos_offset{},uv_offset{},uv_stride{},pos_stride=12,vtx_bindings=3;uint64_t vtx_sig=8;
 uint32_t paks_uv_binding=1,paks_uv_offset{},paks_uv_stride=4;bool paks_uv_half=true;
};
std::map<uint64_t,std::vector<uint8_t>> buffers;
struct Tracker {std::shared_mutex mu;std::map<uint64_t,std::pair<uint64_t,uint64_t>> buffer_mem;std::map<uint64_t,uint8_t*> mapped;} tracking;
Tracker& tracker(){return tracking;}
bool pak_raw_mutable(VkBuffer b){std::shared_lock lock(tracker().mu);const auto i=tracker().buffer_mem.find(b);
 return i!=tracker().buffer_mem.end()&&tracker().mapped.contains(i->second.first);}
std::map<uint64_t,bg3::native_hit::RecordingOwner> owners;
std::vector<mesh_paks::Row> pak_meshes;
std::mutex mutex;
uint64_t next_buffer=100,done=0,recording=7;
uint64_t frame=1;
std::atomic<uint64_t> pak_witness_ok{},pak_witness_mismatch{},pak_budget_deferred{};
namespace shader_cache=bg3::shader_cache;
uint64_t frame_index(){return frame;}
uint64_t geo_buffer_size(uint64_t b){auto i=buffers.find(b);return i==buffers.end()?0:i->second.size();}
unsigned geo_buffer_usage(uint64_t){return VK_BUFFER_USAGE_TRANSFER_SRC_BIT;}
bool enabled=true;
bool paks_rows(){return enabled;}
bool one_time(CmdState* s){return s && s->recording;}
uint64_t handle(uint64_t cmd){return cmd;}
uint64_t current_recording(uint64_t){return recording;}
uint64_t life_gpu_done(){return done;}
bool own(uint64_t cmd){auto& o=owners[recording];o.command=cmd;o.recording=recording;return true;}
bool create(Buffer& b,uint64_t size,unsigned,bool=false){
 b.b=next_buffer++;b.bytes=size;auto& data=buffers[b.b];data.resize(size);b.mapped=data.data();return true;
}
void destroy(Buffer& b){buffers.erase(b.b);b={};}
void log(const char*,...){}
void barrier(uint64_t,unsigned,unsigned,unsigned,unsigned){}
struct FakeDevice {
 void CmdCopyBuffer(uint64_t,uint64_t src,uint64_t dst,unsigned count,const VkBufferCopy* copies){
  for(unsigned i=0;i<count;++i){const auto& c=copies[i];CHECK(c.size>0);
   CHECK(c.srcOffset<=buffers[src].size()&&c.size<=buffers[src].size()-c.srcOffset);
   CHECK(c.dstOffset<=buffers[dst].size()&&c.size<=buffers[dst].size()-c.dstOffset);
   std::memcpy(buffers[dst].data()+c.dstOffset,buffers[src].data()+c.srcOffset,c.size);}
 }
};
struct Device {FakeDevice vk;} device;
Device* dd=&device;
#include "../layer/pak_uv_readback.inl"
void replay(){
 CmdState s;s.vb[0]={11,0,12};s.vb[1]={12,0,4};DrawArgs a;InstPipe ip;
 mesh_paks::Row row;row.index_count=3;pak_meshes={row};
 std::array<float,64> got{};CHECK(mesh_paks::has_candidates(pak_meshes,a.count));
 CHECK(!pak_uv_get(s,a,ip,got)); // discovery, packed UV: no raw float2 binding
 uint64_t xfb{},offset{},bytes{};
 CHECK(pak_uv_capture(9,&s,a,ip,64,&xfb,&offset,&bytes));
 CHECK(!pak_uv_capture(9,&s,a,ip,64,&xfb,&offset,&bytes)); // one pending job
 std::fill(buffers[xfb].begin(),buffers[xfb].end(),0);pak_uv_record(9,&s);
 CHECK(!pak_uv_get(s,a,ip,got)); // recorded is not retired
 auto& owner=owners[s.recording];CHECK(owner.reserve());owner.finish(true,10);
 done=11;CHECK(!pak_uv_get(s,a,ip,got));
 done=12;recording=8;s.recording=8;
 CHECK(pak_uv_get(s,a,ip,got)); // a later recording finds the retired entry
 CHECK(pak_uv_cache.begin()->second.xfb.b==0);
 CHECK(mesh_paks::lookup(pak_meshes,a.count,nullptr,got.data()).status==mesh_paks::Match::Unique);
 got[0]=1;CHECK(mesh_paks::lookup(pak_meshes,a.count,nullptr,got.data()).status==mesh_paks::Match::Missing);
 pak_meshes.push_back(row);pak_meshes.back().flags=4;
 got[0]=0;CHECK(mesh_paks::lookup(pak_meshes,a.count,nullptr,got.data()).status==mesh_paks::Match::Ambiguous);
 s.vb[1].offset=4;CHECK(!pak_uv_get(s,a,ip,got)); // packed UV input changed
 pak_uv_destroy();owners.clear();
}
void publication_replay(bool mismatch){
 namespace p=bg3::native_hit::pool;
 pak_uv_destroy();owners.clear();pak_budget={};recording=100;done=0;frame=100;
 CmdState s;s.recording=recording;s.ib=1;s.ib_type=VK_INDEX_TYPE_UINT16;
 s.vs=0x2d2ac3c38992b7f0ull;s.fs=0xb0ecc292e8661561ull;
 s.vb[0]={11,0,12};s.vb[1]={12,0,4};InstPipe ip;DrawArgs a;a.count=99;
 buffers[1].resize((a.count*2+3)&~3u);buffers[11].resize(a.count*12);buffers[12].resize(a.count*4);
 std::vector<float> expected;
 for(uint32_t c=0;c<a.count;++c){const uint16_t ix=uint16_t(c),uv[2]={0x3800,0x3400};
  const float pos[3]={float(c),float(c%3),-float(c)};
  std::memcpy(buffers[1].data()+c*2,&ix,2);std::memcpy(buffers[11].data()+c*12,pos,12);
  std::memcpy(buffers[12].data()+c*4,uv,4);expected.insert(expected.end(),{pos[0],pos[1],pos[2],0.5f,0.25f});
 }
 mesh_paks::Row row;row.index_count=a.count;row.flags=4;row.compact=true;row.material="00000000-0000-0000-0000-000000000001";
 row.geometry_digest=bg3::shader_cache::sha256_hex(expected.data(),expected.size()*4);
 for(unsigned c=0;c<32;++c){row.uv[c*2]=0.5f;row.uv[c*2+1]=0.25f;}pak_meshes={row};
 std::array<float,64> got{};uint64_t xfb{},off{},bytes{};
 CHECK(!pak_witness_get(s,a,ip,&pak_meshes[0]));
 CHECK(pak_uv_capture(9,&s,a,ip,88,&xfb,&off,&bytes));CHECK(bytes==99*88);
 // An 88-byte post-VS capture carries decoded UV, but deliberately unrelated
 // transformed positions. Only the device-local raw streams may prove SHA.
 for(unsigned c=0;c<a.count;++c){const float pos[4]={1e6f,2e6f,3e6f,1},uv[2]={0.5f,0.25f};
  std::memcpy(buffers[xfb].data()+c*88,pos,16);std::memcpy(buffers[xfb].data()+c*88+16,uv,8);}
 auto retire=[&](){auto& owner=owners[s.recording];CHECK(owner.reserve());owner.finish(true,frame);
  done=frame+2;recording=++s.recording;++frame;};
 pak_uv_record(9,&s);CHECK(!pak_uv_get(s,a,ip,got));retire();
 CHECK(pak_uv_get(s,a,ip,got));CHECK(mesh_paks::lookup(pak_meshes,a.count,nullptr,got.data()).status==mesh_paks::Match::Unique);
 CHECK(!pak_witness_get(s,a,ip,&pak_meshes[0])); // candidate alone cannot publish
 CHECK(!pak_uv_capture(9,&s,a,ip,88,&xfb,&off,&bytes));pak_uv_record(9,&s);retire();pak_uv_poll();
 CHECK(!pak_witness_get(s,a,ip,&pak_meshes[0])); // indices alone cannot publish
 if(mismatch){float changed=1234;std::memcpy(buffers[11].data()+12,&changed,4);} // corner 1 is absent from 32-corner key
 CHECK(!pak_uv_capture(9,&s,a,ip,88,&xfb,&off,&bytes));pak_uv_record(9,&s);retire();pak_uv_poll();
 const bool joined=pak_witness_get(s,a,ip,&pak_meshes[0]);if(joined==mismatch)std::printf("witness stage %u ok %llu mismatch %llu\n",pak_uv_cache.begin()->second.stage,(unsigned long long)pak_witness_ok.load(),(unsigned long long)pak_witness_mismatch.load());CHECK(joined==!mismatch);
 struct Payload {p::BasisTrack basis;bool current=true;std::string material;std::shared_ptr<std::atomic<bool>> proof;};p::Table<Payload> table;
 p::Geometry g;g.indexed=true;g.count=a.count;g.ib=s.ib;g.ib_type=uint32_t(s.ib_type);g.position_buffer=11;
 const auto geometry=p::geometry_key(g),position=p::position_key(g);
 if(joined&&mesh_paks::hlod_compatible(pak_meshes[0],s.vs,s.fs,true,false)){
  CHECK(table.observe(geometry,frame,{0x123,position,p::Status::Qualified})==p::Table<Payload>::Store::Fresh);
  table.find(geometry)->payload.material=row.material;
  table.find(geometry)->payload.proof=pak_witness_token(s,a,ip);
 }
 auto current=[](const Payload& x){return x.current&&x.proof&&x.proof->load(std::memory_order_relaxed);};
 if(mismatch){CHECK(table.lookup(geometry,7,position,frame,1,current)==p::Status::NoDeferred);return;}
 CHECK(table.lookup(geometry,7,position,frame,2,current)==p::Status::Qualified);
 auto& payload=table.find(geometry)->payload;p::basis_rebind(payload.basis,7);p::basis_pending(payload.basis,frame);
 CHECK(payload.basis.state==p::Basis::Pending);p::basis_done(payload.basis,p::basis_result(true,true,false,0,0),frame);
 CHECK(payload.basis.state==p::Basis::Ready);p::Slots slots;payload.basis.slot=slots.acquire();CHECK(payload.basis.slot<bg3::native_hit::nh_pool_rows);
 bg3::native_hit::PendingPublication publication;CHECK(publication.prepare(payload.basis.state==p::Basis::Ready)==1);publication.finish(true);
 CHECK(publication.sealed&&publication.published);CHECK(table.find(geometry)->payload.material==row.material);
 CHECK(table.lookup(geometry,7,position,frame+1,2,current)==p::Status::Qualified);
 payload.current=false;CHECK(table.lookup(geometry,7,position,frame+1,2,current)==p::Status::Stale);
 payload.current=true;CHECK(pak_uv_forget_cached_buffer(11)); // same handle receives new capture bytes
 CHECK(!payload.proof->load());CHECK(!pak_witness_get(s,a,ip,&pak_meshes[0]));
 CHECK(table.lookup(geometry,7,position,frame+1,2,current)==p::Status::Stale);
 pak_uv_destroy();owners.clear();
}
void chunk_replay(bool discard,bool invalidate=false){
 pak_uv_destroy();owners.clear();pak_budget={};recording=300;done=0;frame=300;
 CmdState s;s.recording=recording;s.ib=1;s.ib_type=VK_INDEX_TYPE_UINT32;s.vb[0]={11,0,12};s.vb[1]={12,0,4};
 DrawArgs a;a.count=3;InstPipe ip;const uint32_t indices[3]={0,1,400000};
 buffers[1].resize(sizeof indices);std::memcpy(buffers[1].data(),indices,sizeof indices);
 buffers[11].assign(400001*12,0);buffers[12].assign(400001*4,0);
 std::vector<float> expected(15,0);mesh_paks::Row row;row.index_count=3;row.compact=true;
 row.geometry_digest=bg3::shader_cache::sha256_hex(expected.data(),expected.size()*4);pak_meshes={row};
 PakUvEntry entry;entry.stage=1;entry.count=3;entry.stride=88;entry.witness=&pak_meshes[0];entry.result.state=mesh_paks::uv_join::State::Ready;
 const auto key=pak_uv_key(s,a,ip);pak_uv_cache.emplace(key,std::move(entry));uint64_t xfb{},offset{},bytes{};
 CHECK(pak_budget.take(frame,mesh_paks::uv_join::Budget::kFrameBytes,0,0));
 const auto deferred_before=pak_budget_deferred.load();
 CHECK(!pak_uv_capture(9,&s,a,ip,88,&xfb,&offset,&bytes));
 CHECK(pak_uv_cache.at(key).stage==1&&pak_uv_cache.at(key).host.b==0);
 CHECK(pak_budget_deferred==deferred_before+1);++frame; // same candidate retries without a stall
 auto retire=[&](bool success){auto& owner=owners[s.recording];CHECK(owner.reserve());owner.finish(success,frame);
  done=frame+2;recording=++s.recording;++frame;pak_uv_poll();};
 CHECK(!pak_uv_capture(9,&s,a,ip,88,&xfb,&offset,&bytes));CHECK(pak_uv_cache.at(key).stage==2);
 pak_uv_record(9,&s);retire(true);CHECK(pak_uv_cache.at(key).stage==3);
 CHECK(!pak_uv_capture(9,&s,a,ip,88,&xfb,&offset,&bytes));CHECK(pak_uv_cache.at(key).stage==4);
 CHECK(pak_uv_cache.at(key).chunk==mesh_paks::uv_join::Budget::kFrameBytes);
 CHECK(pak_uv_cache.at(key).host.bytes>pak_uv_cache.at(key).chunk);
 pak_uv_record(9,&s);retire(true);CHECK(pak_uv_cache.at(key).stage==3);
 CHECK(!pak_witness_get(s,a,ip,&pak_meshes[0])); // first chunk cannot qualify
 CHECK(pak_uv_cache.at(key).copied==mesh_paks::uv_join::Budget::kFrameBytes);
 CHECK(!pak_uv_capture(9,&s,a,ip,88,&xfb,&offset,&bytes));pak_uv_record(9,&s);
 const auto token=pak_uv_cache.at(key).valid;
 if(invalidate){CHECK(pak_uv_forget_cached_buffer(12));CHECK(pak_uv_cache.at(key).invalid);CHECK(!token->load());}
 retire(!discard);
 if(discard||invalidate){CHECK(!pak_uv_cache.contains(key));CHECK(!pak_witness_get(s,a,ip,&pak_meshes[0]));}
 else{CHECK(pak_witness_get(s,a,ip,&pak_meshes[0]));CHECK(pak_uv_cache.at(key).host.b==0);}
 pak_uv_destroy();owners.clear();
}
}
