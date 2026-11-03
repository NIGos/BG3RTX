// Exercise the production enqueue/copy/retire/cache path with a tiny fake device.
#include "../common/native_hit_join.h"
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
};
std::map<uint64_t,std::vector<uint8_t>> buffers;
std::map<uint64_t,bg3::native_hit::RecordingOwner> owners;
std::vector<mesh_paks::Row> pak_meshes;
std::mutex mutex;
uint64_t next_buffer=100,done=0,recording=7;
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
  for(unsigned i=0;i<count;++i)std::memcpy(buffers[dst].data()+copies[i].dstOffset,
    buffers[src].data()+copies[i].srcOffset,copies[i].size);
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
}
