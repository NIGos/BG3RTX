"""Exercise the production descriptor allocator and submit walker without a GPU."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest
from recording_lifecycle_test import body

ROOT = Path(__file__).resolve().parents[1]


class PostRecording(unittest.TestCase):
    def test_allocator_and_secondary_submit(self):
        compiler = shutil.which("cl.exe")
        self.assertIsNotNone(compiler, "run scripts/build.ps1 -Test")
        source = FIXTURE + "\nVkDescriptorSet acquire_set(DeviceData* dd, VkCommandBuffer cmd) " + body("layer/rrpost.cpp", "acquire_set")
        source += "\nvoid post_submit_recordings(const std::vector<VkCommandBuffer>& cmds) " + body("layer/cmd.cpp", "post_submit_recordings")
        source += MAIN
        with tempfile.TemporaryDirectory(prefix="post-recording-") as temp:
            path = Path(temp)
            (path / "test.cpp").write_text(source)
            result = subprocess.run([compiler, "/nologo", "/EHsc", "/std:c++20", "/MT", "test.cpp", "/Fe:test.exe"], cwd=path, capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            result = subprocess.run([str(path / "test.exe")], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_core_retained_recording_completion(self):
        compiler = shutil.which("cl.exe")
        self.assertIsNotNone(compiler, "run scripts/build.ps1 -Test")
        code = (ROOT / "layer/cmd.cpp").read_text()
        start = code.index("namespace {", code.index("// Retained only for core consumers"))
        end = code.index("static void post_discard_recording", start)
        source = CORE_FIXTURE + code[start:end] + CORE_MAIN
        with tempfile.TemporaryDirectory(prefix="core-recording-") as temp:
            path = Path(temp)
            (path / "test.cpp").write_text(source)
            result = subprocess.run([compiler, "/nologo", "/EHsc", "/std:c++20", "/MT", "test.cpp", "/Fe:test.exe"], cwd=path, capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            result = subprocess.run([str(path / "test.exe")], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_real_temporal_record_submit_discard_sequence(self):
        compiler = shutil.which("cl.exe")
        self.assertIsNotNone(compiler, "run scripts/build.ps1 -Test")
        source = TEMPORAL_FIXTURE
        source += "\nvoid rrpost_temporal(VkCommandBuffer cmd, const Bg3Binding& depth, const Bg3Binding& mv, const float jitter[2], const float scale[2], bool reset, uint32_t ow, uint32_t oh) " + body("layer/rrpost.cpp", "rrpost_temporal")
        source += TEMPORAL_MAIN
        with tempfile.TemporaryDirectory(prefix="post-temporal-") as temp:
            path = Path(temp)
            (path / "test.cpp").write_text(source)
            result = subprocess.run([compiler, "/nologo", "/EHsc", "/std:c++20", "/MT", "test.cpp", "/Fe:test.exe"], cwd=path, capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            result = subprocess.run([str(path / "test.exe")], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)


FIXTURE = r'''
#include <cstdint>
#include <vector>
#include <unordered_map>
#include <set>
#include <cstdio>
#include "HEADER"
using VkDescriptorSet=uint64_t; using VkDescriptorPool=uint64_t;
using VkDescriptorSetLayout=uint64_t; using VkCommandBuffer=uint64_t;
constexpr uint64_t VK_NULL_HANDLE=0; constexpr int VK_SUCCESS=0;
constexpr int VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE=1, VK_DESCRIPTOR_TYPE_SAMPLER=2;
constexpr int VK_DESCRIPTOR_TYPE_STORAGE_IMAGE=3, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER=4;
constexpr int VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO=1, VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO=2;
struct VkDescriptorPoolSize { int type; uint32_t count; };
struct VkDescriptorPoolCreateInfo { int type; uint32_t maxSets=0, poolSizeCount=0; VkDescriptorPoolSize* pPoolSizes=nullptr; };
struct VkDescriptorSetAllocateInfo { int type; VkDescriptorPool descriptorPool=0; uint32_t descriptorSetCount=0; VkDescriptorSetLayout* pSetLayouts=nullptr; };
uint64_t next_handle=1000, current_recording=1;
struct FakeVk {
  int CreateDescriptorPool(int, const VkDescriptorPoolCreateInfo*, void*, VkDescriptorPool* p) { *p=next_handle++; return VK_SUCCESS; }
  int AllocateDescriptorSets(int, const VkDescriptorSetAllocateInfo* a, VkDescriptorSet* p) { for(uint32_t i=0;i<a->descriptorSetCount;++i)p[i]=next_handle++; return VK_SUCCESS; }
  void DestroyDescriptorPool(int, VkDescriptorPool, void*) {}
};
struct DeviceData { FakeVk vk; int device=0; };
constexpr uint32_t kRing=256;
VkDescriptorSetLayout g_dsl=1; VkDescriptorSet g_sets[kRing];
DescriptorRecordingLease g_set_owners[kRing]; uint32_t g_next_set=0;
struct SetPage { VkDescriptorPool pool=0; VkDescriptorSet sets[kRing]={}; DescriptorRecordingLease owners[kRing]; };
std::vector<SetPage> g_set_pages;
std::set<uint64_t> pending, referenced;
uint64_t cmd_recording(VkCommandBuffer) { return current_recording; }
bool life_before_destroy(uint64_t h, const char*, bool) { return !pending.contains(h); }
void life_note(VkCommandBuffer, uint64_t h) { referenced.insert(h); }
struct CmdState { uint64_t recording=0; std::vector<uint64_t> post_secondaries; };
std::unordered_map<VkCommandBuffer,CmdState> states;
CmdState* cmd_state(VkCommandBuffer cmd) { auto i=states.find(cmd);return i==states.end()?nullptr:&i->second; }
std::vector<uint64_t> rr_submits, ngx_submits;
void rrpost_submit_recording(uint64_t r) { rr_submits.push_back(r); }
void ngx_submit_recording(uint64_t r) { ngx_submits.push_back(r); }
'''.replace("HEADER", (ROOT / "layer/recording_init.h").as_posix())

MAIN = r'''
int main() {
  DeviceData dd;
  for(uint32_t i=0;i<kRing;++i)g_sets[i]=i+1;
  std::set<uint64_t> used;
  // More than 256 live executable sets: temporal/final must still allocate.
  for(int i=0;i<600;++i) {
    auto set=acquire_set(&dd,1);
    if(!set || !used.insert(set).second || !referenced.contains(set))return 1;
  }
  if(g_set_pages.size()!=2)return 2;
  for(auto& owner:g_set_owners)owner.discard(1);
  for(auto set:g_sets)pending.insert(set); // reset does not prove GPU completion
  current_recording=2;
  auto set=acquire_set(&dd,2);
  if(!set || pending.contains(set) || used.contains(set))return 3;
  pending.clear();g_next_set=0;
  if(acquire_set(&dd,2)!=g_sets[0])return 4;
  states[10]={900,{100,101}}; // copied ExecuteCommands IDs
  states[20]={102,{}};       // secondary handle was begun again
  post_submit_recordings({10});
  if(rr_submits!=std::vector<uint64_t>{100,101,900} || ngx_submits!=rr_submits)return 5;
  return 0;
}
'''

TEMPORAL_FIXTURE = r'''
#include <cstdint>
#include <mutex>
#include <cmath>
#include <cstring>
#include <initializer_list>
#include "HEADER"
using VkCommandBuffer=uint64_t;
struct DeviceData {};
DeviceData dd;
DeviceData* device_of(VkCommandBuffer) { return &dd; }
uint64_t frame=1;
uint64_t frame_index() { return frame; }
uint64_t cmd_recording(VkCommandBuffer cmd) { return cmd; }
int cfg_int(const char*,int) { return 1; }
void log(const char*,...) {}
constexpr int VK_FORMAT_R32G32B32A32_SFLOAT=1, VK_IMAGE_LAYOUT_GENERAL=1;
constexpr int P_TEMPORAL_FINAL=0, P_TEMPORAL=1;
int g_pipes[2];
bool pipeline(DeviceData*,int) { return true; }
struct Img { uint64_t view=1; uint32_t w=0,h=0; };
Img g_hist_ac[2], g_hist_eb[2], g_hist_meta[2], g_ac,g_eb;
bool img_ensure(DeviceData*,Img& img,int,uint32_t w,uint32_t h,const char*) { img.w=w;img.h=h;return true; }
void to_general(DeviceData*,VkCommandBuffer,std::initializer_list<const Img*>) {}
struct Bg3Binding { uint64_t view=1; uint32_t width=16,height=16; int layout=1; };
struct Chain { uint32_t w=16,h=16; } g_ch;
struct History { PostHistoryProducer producer; uint64_t frame=0; uint32_t ow=0,oh=0; float jitter[2]={}; };
History g_history[2];
std::mutex g_mu;
bool g_hist_valid=false,g_temporal_active=false;
uint32_t g_hist_index=0,g_hist_ow=0,g_hist_oh=0;
uint64_t g_hist_frame=0;
float g_hist_jitter[2]={},g_final_jitter[2]={};
struct Binds { uint64_t in[3]={}; int in_l[3]={}; uint64_t temporal_in[4]={}; int depth_layout=0; uint64_t temporal_ac=0,temporal_eb=0,temporal_meta=0; };
struct Push { uint32_t size[2]={},flags=0; float uvmap[2]={},uvscale[2]={}; };
uint32_t calls=0,last_flags=99;
bool run(DeviceData*,VkCommandBuffer,int,const Binds&,const Push& pc,uint32_t,uint32_t) { ++calls;last_flags=pc.flags;return true; }
void submit(uint64_t id) { for(auto& h:g_history)h.producer.submit(id); }
void discard(uint64_t id) { for(auto& h:g_history)h.producer.discard(id); }
'''.replace("HEADER", (ROOT / "layer/recording_init.h").as_posix())

TEMPORAL_MAIN = r'''
int main() {
  Bg3Binding depth,mv;float jitter[2]={},scale[2]={1,1};
  auto record=[&](uint64_t id) { rrpost_temporal(id,depth,mv,jitter,scale,false,32,32); };
  record(100); // N in a secondary
  if(calls!=1 || last_flags || !g_temporal_active)return 1;
  frame=2;record(101); // N+1 recorded before submit N
  if(calls!=2 || last_flags || !g_temporal_active)return 2;
  submit(100);
  if(!g_history[1].producer.readable() || g_history[0].producer.readable())return 3;
  submit(101);discard(100); // reset submitted secondary keeps the pixels valid
  for(frame=3;frame<100;++frame) {
    record(100+frame-1);
    if(calls!=frame || last_flags!=1 || !g_temporal_active)return 4;
    submit(100+frame-1);
  }
  record(999);discard(999); // discarded recording can never seed history
  submit(999);
  for(auto& h:g_history)if(h.producer.recording==999 && h.producer.readable())return 5;
  frame=101;record(1000);
  if(last_flags || !g_temporal_active)return 6; // gap still dispatches a fresh sample
  return 0;
}
'''



CORE_FIXTURE = r"""
#include <cstdint>
#include <vector>
#include <mutex>
#include <unordered_map>
#include <algorithm>
using VkQueue = uint64_t; using VkCommandBuffer = uint64_t;
enum { BG3_RECORDING_EXECUTABLE=1, BG3_RECORDING_SUBMITTED=2, BG3_RECORDING_COMPLETE=4 };
struct Bg3RecordingState { uint32_t flags; uint64_t completion_frame; };
uint64_t frame=10, seq=1, idle=0;
std::unordered_map<uint64_t,uint64_t> done;
bool life_recording_complete(VkQueue queue,uint64_t f,uint64_t s) { return s<=idle || f<done[queue]; }
uint64_t frame_index() { return frame; }
uint64_t life_idle_begin() { return seq; }
struct CmdState { uint64_t recording=0; std::vector<uint64_t> post_secondaries; };
std::unordered_map<VkCommandBuffer,CmdState> states;
CmdState* cmd_state(VkCommandBuffer cmd) { auto i=states.find(cmd);return i==states.end()?nullptr:&i->second; }
"""
CORE_MAIN = r"""
int main() {
  if(core_recording_state(999).flags)return 1;
  states[1]={100,{101}};
  core_retain_recording(100);core_retain_recording(100);core_retain_recording(101);
  if(core_recording_state(100).flags!=5)return 2;
  core_submit_recordings(7,{1});
  if(core_recording_state(100).flags!=3 || core_recording_state(101).flags!=3)return 3;
  if(core_recording_state(100).completion_frame!=11)return 4;
  done[8]=1000; // Another queue's progress cannot prove completion.
  if(core_recording_state(100).flags&4)return 5;
  done[7]=10;if(core_recording_state(100).flags&4)return 6;
  done[7]=11;if(core_recording_state(100).flags!=7)return 7;
  ++seq;frame=12;core_submit_recordings(7,{1}); // Executable resubmission renews the lease.
  if(core_recording_state(100).flags!=3)return 8;
  core_discard_recording(100);core_discard_recording(101);
  core_release_recording(100);
  if(core_recording_state(100).flags!=2)return 9; // Second consumer still owns the metadata.
  done[7]=13;
  if(core_recording_state(100).flags!=6)return 10;
  core_release_recording(100);core_release_recording(101);
  if(!g_core_recordings.empty())return 11;
  core_retain_recording(102);core_discard_recording(102);
  if(core_recording_state(102).flags!=4)return 12;
  core_release_recording(102);
  states[1]={103,{}};core_retain_recording(103);++seq;
  core_submit_recordings(9,{1});core_discard_recording(103);
  if(core_recording_state(103).flags!=2)return 13;
  idle=seq;if(core_recording_state(103).flags!=6)return 14;
  core_release_recording(103);
  if(!g_core_recordings.empty())return 15;
  return 0;
}
"""

if __name__ == "__main__":
    unittest.main()
