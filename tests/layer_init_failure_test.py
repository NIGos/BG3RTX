"""Failure injection against the actual RR initialization function; no Vulkan device."""
from pathlib import Path
import os
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


def function(file, signature):
    text = (ROOT / file).read_text(encoding='utf-8')
    start = text.index(signature)
    brace = text.index('{', start)
    depth = 1
    end = brace + 1
    while depth:
        depth += (text[end] == '{') - (text[end] == '}')
        end += 1
    return text[start:end]


class LayerInitFailure(unittest.TestCase):
    def test_ngx_shutdown_hooks_are_transactional(self):
        compiler = shutil.which('cl.exe')
        self.assertIsNotNone(compiler, 'run through scripts/build.ps1 -Test (MSVC environment)')
        text = (ROOT / 'layer/ngx.cpp').read_text(encoding='utf-8')
        start = text.index('    bool shutdown_safe')
        end = text.index('    if(!shutdown_safe)', start)
        source = NGX_SHUTDOWN_FIXTURE + '\n' + function('layer/ngx.cpp', 'static bool hook_terminal(')
        source += '\nbool install() {\n' + text[start:end] + '\nreturn shutdown_safe;\n}\n' + r'''
int main() {
  for (missing = 0; missing < 4; ++missing) for (alias = 0; alias < 2; ++alias)
  for (int fail : {0, 1, 2}) {
    fail_protect = fail; protects = 0; protection = 0x20; a[0] = b[0] = 0;
    g_nr_shutdown = {}; g_nr_shutdown1 = {};
    int count = !(missing & 1) + !(missing & 2);
    bool expected = !fail || fail > count;
    bool ok = install();
    if (ok != expected) return 1;
    if (!ok && (a[0] || b[0] || g_nr_shutdown.active || g_nr_shutdown1.active)) return 2;
    if (ok && (g_nr_shutdown.active != !(missing & 1) ||
               g_nr_shutdown1.active != !(missing & 2))) return 3;
    if (ok && ((g_nr_shutdown.active && !a[0]) ||
               (g_nr_shutdown1.active && !(alias ? a[0] : b[0])))) return 4;
    if (protection != 0x20) return 5;
  }
}
'''
        with tempfile.TemporaryDirectory(prefix='ngx-shutdown-') as temp:
            path = Path(temp)
            (path / 'test.cpp').write_text(source, encoding='utf-8')
            result = subprocess.run([compiler, '/nologo', '/EHsc', '/std:c++20', '/MT',
                                     'test.cpp', '/Fe:test.exe'], cwd=path, capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            result = subprocess.run([str(path / 'test.exe')], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_streamline_teardown_clears_deferred_init(self):
        compiler = shutil.which('cl.exe')
        self.assertIsNotNone(compiler, 'run through scripts/build.ps1 -Test (MSVC environment)')
        source = SL_TEARDOWN_FIXTURE + '\n' + function('layer/streamline.cpp', 'void sl_device_destroy(') + r'''
int main() {
  DeviceData owner, other;
  g_dd = &owner; g_pending = std::make_unique<int>(7);
  g_sl_init = true; g_sl_instance = 42; g_have_of_queue = true;
  g_req.queues = 3; g_active = true;
  sl_device_destroy(&other);
  if (!g_pending || !g_sl_init || !g_active) return 1;
  sl_device_destroy(&owner);
  // A replacement device must not enter sl_device_pre with a cleared SDK table.
  if (g_sl_init || g_dd || g_pending || g_active || g_sl_instance ||
      g_req.queues || g_have_of_queue) return 2;
  if (shutdowns != 1) return 3;
  sl_device_destroy(&owner);
  return shutdowns == 1 ? 0 : 4;
}
'''
        with tempfile.TemporaryDirectory(prefix='sl-teardown-') as temp:
            path = Path(temp)
            (path / 'test.cpp').write_text(source, encoding='utf-8')
            result = subprocess.run([compiler, '/nologo', '/EHsc', '/std:c++20', '/MT',
                                     'test.cpp', '/Fe:test.exe'], cwd=path, capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            result = subprocess.run([str(path / 'test.exe')], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_rr_layout_faults_and_retry(self):
        compiler = shutil.which('cl.exe')
        self.assertIsNotNone(compiler, 'run through scripts/build.ps1 -Test (MSVC environment)')
        sdk = os.environ['VULKAN_SDK']
        sl = (ROOT / 'layer/streamline.cpp').read_text(encoding='utf-8')
        start = sl.index('    sl::Result r = g_sl.DLSSGSetOptions(0, o);')
        end = sl.index('\n  }\n\n  if (g_token', start)
        fg_apply = 'void apply(int want, uint32_t flags, uint64_t frame) { Options o{};\n' + sl[start:end] + '\n}'
        source = (FIXTURE + '\n' + function('layer/rrpost.cpp', 'bool ensure_layout(DeviceData* dd)') + '\n'
                  + function('layer/hotreload.cpp', 'bool alloc_image(') + '\n'
                  + function('layer/ngx.cpp', 'static bool ensure_layouts(DeviceData* dd)') + '\n'
                  + function('layer/geocapture.cpp', 'bool ubo_pipeline()') + '\n' + fg_apply + '\n' + MAIN)
        with tempfile.TemporaryDirectory(prefix='layer-init-') as temp:
            path = Path(temp)
            (path / 'test.cpp').write_text(source, encoding='utf-8')
            result = subprocess.run([compiler, '/nologo', '/EHsc', '/std:c++20', '/MT',
                                     '/I' + str(Path(sdk) / 'Include'), 'test.cpp', '/Fe:test.exe'],
                                    cwd=path, capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            result = subprocess.run([str(path / 'test.exe')], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertIn('18 injected failures and successful retries', result.stdout)

    def test_core_fault_publication_is_atomic(self):
        text = (ROOT / 'layer/hotreload.cpp').read_text(encoding='utf-8')
        self.assertIn('static std::atomic<bool> g_faulted{false};', text)
        self.assertIn('if (g_faulted.exchange(true)) return;', text)

    def test_allocations_unwind_and_recordings_stop_before_submit(self):
        for file in ('bvhsplit', 'rtscene', 'water', 'emissive'):
            code = function('layer/' + file + '.cpp', 'bool make_buf(')
            self.assertRegex(code, r'if \(dd->vk.BindBufferMemory[^\n]+!= VK_SUCCESS\) \{\s*dd->vk.DestroyBuffer[^;]+;\s*dd->vk.FreeMemory[^;]+;\s*out = Buf\{\};\s*return false;')
        for file, signature in [('water', 'void water_after_submit('), ('emissive', 'void em_after_submit(')]:
            code = function('layer/' + file + '.cpp', signature)
            for phase in ('begin', 'end'):
                self.assertIn('if (' + phase + '_result != VK_SUCCESS) return;', code)
                self.assertLess(code.index('if (' + phase + '_result != VK_SUCCESS)'), code.rindex('QueueSubmit('))
        code = function('layer/rtscene.cpp', 'bool slot_retired(')
        self.assertNotIn('240', code)
        self.assertIn('return false;', code)
        code = function('layer/geocapture.cpp', 'bool ubo_pipeline()')
        for item in ('DestroyPipeline(', 'DestroyPipelineLayout(', 'DestroyDescriptorSetLayout('):
            self.assertIn(item, code)
        self.assertIn('if (r != VK_SUCCESS) return failed();', code)
        for file, signature in [('rrpost', 'bool img_ensure('), ('hotreload', 'bool alloc_image(')]:
            # Both view creation failures must return before publishing usable dimensions/true.
            text = (ROOT / ('layer/' + file + '.cpp')).read_text(encoding='utf-8')
            self.assertRegex(text, r'if \(dd->vk.CreateImageView[^\n]+!= VK_SUCCESS\) \{\s*dd->vk.DestroyImage[^;]+;\s*dd->vk.FreeMemory')


NGX_SHUTDOWN_FIXTURE = r'''
#include <cstdint>
#include <cstring>
#include <initializer_list>
using DWORD = unsigned long;
constexpr DWORD PAGE_EXECUTE_READWRITE = 0x40;
struct InlineHook { uint8_t* target{}; void* trampoline{}; bool active{}; };
InlineHook g_nr_shutdown, g_nr_shutdown1;
uint8_t a[14]{}, b[14]{};
int protects, fail_protect, missing, alias;
DWORD protection = 0x20; // Both targets share a page, as real exports may do.
bool VirtualProtect(void*, int, DWORD value, DWORD* old) {
  if (value == PAGE_EXECUTE_READWRITE && ++protects == fail_protect) return false;
  *old = protection; protection = value; return true;
}
void* GetCurrentProcess() { return nullptr; }
void FlushInstructionCache(void*, void*, int) {}
template<class... T> void log(const char*, T...) {}
uint8_t* follow_jumps(uint8_t* p) { return p; }
void write_abs_jump(uint8_t* p, const void*) { std::memset(p, 1, 14); }
void nr_shutdown() {}
void nr_shutdown1() {}
void* proc(const char* name) {
  bool second = std::strstr(name, "Shutdown1");
  if (missing & (second ? 2 : 1)) return nullptr;
  return second && !alias ? b : a;
}
'''

SL_TEARDOWN_FIXTURE = r'''
#include <memory>
#include <mutex>
#include <vector>
#define VK_NULL_HANDLE 0
namespace sl { enum class Result { eOk }; }
int shutdowns;
sl::Result shutdown() { ++shutdowns; return sl::Result::eOk; }
struct { sl::Result (*Shutdown)() = shutdown; } g_sl;
struct {} g_px;
struct Dispatch {
  template<class... T> void DestroyImageView(T...) {}
  template<class... T> void DestroyImage(T...) {}
  template<class... T> void FreeMemory(T...) {}
  template<class... T> void DestroyQueryPool(T...) {}
  void DeviceWaitIdle(int) {}
};
struct DeviceData { int device = 1; Dispatch vk; };
DeviceData* g_dd;
bool g_active, g_sl_init, g_have_of_queue;
int g_sl_instance, g_depth_only_view, g_mv_copy_queries;
struct { int queues; } g_req;
struct Image { int image, view; } g_preserved_mvec, g_preserved_depth;
struct Copy { Image image, depth; int memory, depth_memory; };
std::vector<Copy> g_mv_copies(1);
std::vector<int> g_mv_copy_ms;
std::unique_ptr<int> g_pending;
std::mutex g_pending_mu;
const char* res_str(sl::Result) { return "ok"; }
template<class... T> void log(const char*, T...) {}
'''

FIXTURE = r'''
#define VK_NO_PROTOTYPES
#include <vulkan/vulkan.h>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <set>
#include <vector>
namespace sl { enum class Result { eOk, eFailure }; }
struct Options { unsigned numFramesToGenerate=1; };
static sl::Result sdk_result;
struct Sdk { sl::Result DLSSGSetOptions(int, const Options&) { return sdk_result; } } g_sl;
static int g_fg_applied;
static uint32_t g_flags_applied;
static bool g_fg_running;
static uint64_t g_fg_on_frame, g_fg_enables;
static const char* res_str(sl::Result) { return "stub"; }
template<class... T> void log(const char*, T...) {}
static int step, fail_step;
static uintptr_t serial = 1;
static std::set<uintptr_t> live;
static void check(bool value) { if (!value) { std::printf("FAIL step %d injection %d live %zu\n", step, fail_step, live.size()); std::exit(1); } }
template<class T> VkResult create(T* out) {
  if (++step == fail_step) return VK_ERROR_OUT_OF_DEVICE_MEMORY;
  *out = reinterpret_cast<T>(serial++); live.insert(uintptr_t(*out)); return VK_SUCCESS;
}
template<class T> void destroy(T value) { check(live.erase(uintptr_t(value)) == 1); }
#define CREATE(Name, Info, Handle) VkResult Name(VkDevice, const Info*, const VkAllocationCallbacks*, Handle* out) { return create(out); }
#define DESTROY(Name, Handle) void Name(VkDevice, Handle value, const VkAllocationCallbacks*) { destroy(value); }
struct Dispatch {
  CREATE(CreateDescriptorSetLayout, VkDescriptorSetLayoutCreateInfo, VkDescriptorSetLayout)
  CREATE(CreatePipelineLayout, VkPipelineLayoutCreateInfo, VkPipelineLayout)
  CREATE(CreateDescriptorPool, VkDescriptorPoolCreateInfo, VkDescriptorPool)
  CREATE(CreateSampler, VkSamplerCreateInfo, VkSampler)
  CREATE(CreateImage, VkImageCreateInfo, VkImage)
  CREATE(AllocateMemory, VkMemoryAllocateInfo, VkDeviceMemory)
  CREATE(CreateImageView, VkImageViewCreateInfo, VkImageView)
  CREATE(CreateShaderModule, VkShaderModuleCreateInfo, VkShaderModule)
  DESTROY(DestroyShaderModule, VkShaderModule)
  DESTROY(DestroyPipeline, VkPipeline)
  VkResult CreateComputePipelines(VkDevice, VkPipelineCache, uint32_t, const VkComputePipelineCreateInfo*, const VkAllocationCallbacks*, VkPipeline* out) { return create(out); }
  bool CmdPushDescriptorSet = true;
  DESTROY(DestroyImage, VkImage)
  DESTROY(DestroyImageView, VkImageView)
  void GetImageMemoryRequirements(VkDevice, VkImage, VkMemoryRequirements* req) { req->size=4096; req->memoryTypeBits=1; }
  VkResult BindImageMemory(VkDevice, VkImage, VkDeviceMemory, VkDeviceSize) {
    return ++step==fail_step ? VK_ERROR_OUT_OF_DEVICE_MEMORY : VK_SUCCESS;
  }
  DESTROY(DestroyDescriptorSetLayout, VkDescriptorSetLayout)
  DESTROY(DestroyPipelineLayout, VkPipelineLayout)
  DESTROY(DestroyDescriptorPool, VkDescriptorPool)
  DESTROY(DestroySampler, VkSampler)
  DESTROY(DestroyBuffer, VkBuffer)
  DESTROY(FreeMemory, VkDeviceMemory)
  VkResult AllocateDescriptorSets(VkDevice, const VkDescriptorSetAllocateInfo* info, VkDescriptorSet* out) {
    if (++step == fail_step) return VK_ERROR_OUT_OF_DEVICE_MEMORY;
    for (uint32_t i=0; i<info->descriptorSetCount; ++i) out[i] = reinterpret_cast<VkDescriptorSet>(serial++);
    return VK_SUCCESS;
  }
  PFN_vkCreateQueryPool CreateQueryPool = nullptr;
};
struct Instance { void GetPhysicalDeviceProperties(VkPhysicalDevice, VkPhysicalDeviceProperties* p) { p->limits.timestampPeriod=1; } };
struct DeviceData { VkDevice device{}; VkPhysicalDevice physical{}; Instance* inst{}; Dispatch vk; };
struct Bg3Image { VkImage image{}; VkImageView view{}; VkDeviceMemory memory{}; };
static DeviceData device;
static DeviceData* g_dd=&device;
static VkDescriptorSetLayout g_ubo_dsl{};
static VkPipelineLayout g_ubo_pl{};
static VkPipeline g_ubo_pipe{};
struct Bg3ShaderBlob { size_t size_bytes=4; const uint32_t* code=nullptr; };
bool shader_get(const char*, const char*, const char*, Bg3ShaderBlob*) { return true; }
DeviceData* main_device() { return &device; }
uint32_t find_memory_type(DeviceData*, uint32_t, VkMemoryPropertyFlags) { return 0; }
struct Img { VkImage image{}; };
static Img g_dummy2d, g_dummy3d;
static VkBuffer g_dummy_buf{};
static VkDeviceMemory g_dummy_buf_mem{};
static void* g_dummy_buf_map{};
static VkDescriptorSetLayout g_dsl{};
static VkPipelineLayout g_pl{};
static VkDescriptorPool g_pool{};
static VkSampler g_sampler{};
static VkQueryPool g_qp{};
static float g_ts_period{};
static constexpr uint32_t kRing=256, kTs=8;
static VkDescriptorSet g_sets[kRing]{};
struct Push { int value[24]; };
void img_destroy(Img& img) { if (img.image) destroy(img.image); img={}; }
bool img_ensure(DeviceData*, Img& img, VkFormat, uint32_t, uint32_t, const char*, VkImageType=VK_IMAGE_TYPE_2D) { return create(&img.image)==VK_SUCCESS; }
bool alloc_host_buffer(size_t, VkBufferUsageFlags, VkBuffer* b, VkDeviceMemory* m, void** map) {
  if (create(b)!=VK_SUCCESS) return false;
  --step; // The buffer+memory operation is one injected initialization stage.
  check(create(m)==VK_SUCCESS); *map=reinterpret_cast<void*>(1); return true;
}
'''
MAIN = r'''
int main() {
  DeviceData dd;
  for (int injection=1; injection<=8; ++injection) {
    step=0; fail_step=injection;
    check(!ensure_layout(&dd));
    check(live.empty());
    check(!g_dsl && !g_pl && !g_pool && !g_sampler && !g_dummy_buf && !g_dummy_buf_mem && !g_dummy_buf_map);
    for (auto set:g_sets) check(!set);
    step=0; fail_step=0;
    check(ensure_layout(&dd));
    int completed=step;
    check(ensure_layout(&dd)); check(step==completed);
    img_destroy(g_dummy2d); img_destroy(g_dummy3d);
    destroy(g_dummy_buf); destroy(g_dummy_buf_mem); destroy(g_sampler); destroy(g_pool); destroy(g_pl); destroy(g_dsl);
    g_dummy_buf={}; g_dummy_buf_mem={}; g_dummy_buf_map={}; g_sampler={}; g_pool={}; g_pl={}; g_dsl={};
    check(live.empty());
  }
  for (int injection=1; injection<=4; ++injection) {
    Bg3Image img;
    step=0; fail_step=injection;
    check(!alloc_image(32, 32, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_USAGE_SAMPLED_BIT, &img));
    check(!img.image && !img.view && !img.memory && live.empty());
    step=0; fail_step=0;
    check(alloc_image(32, 32, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_USAGE_SAMPLED_BIT, &img));
    destroy(img.view); destroy(img.image); destroy(img.memory);
    check(live.empty());
  }
  for (int injection=1; injection<=2; ++injection) {
    step=0; fail_step=injection;
    check(!ensure_layouts(&dd)); check(!g_dsl && !g_pl && live.empty());
    step=0; fail_step=0;
    check(ensure_layouts(&dd));
    int completed=step; check(ensure_layouts(&dd)); check(step==completed);
    destroy(g_pl); destroy(g_dsl); g_pl={}; g_dsl={};
    check(live.empty());
  }
  for (int injection=1; injection<=4; ++injection) {
    step=0; fail_step=injection;
    check(!ubo_pipeline());
    check(!g_ubo_dsl && !g_ubo_pl && !g_ubo_pipe && live.empty());
    step=0; fail_step=0;
    check(ubo_pipeline());
    int completed=step; check(ubo_pipeline()); check(step==completed);
    destroy(g_ubo_pipe); destroy(g_ubo_pl); destroy(g_ubo_dsl);
    g_ubo_pipe={}; g_ubo_pl={}; g_ubo_dsl={}; check(live.empty());
  }
  g_fg_applied=2; g_flags_applied=7; g_fg_running=true; g_fg_on_frame=40; g_fg_enables=3;
  sdk_result=sl::Result::eFailure;
  apply(0, 9, 100);
  check(g_fg_applied==2 && g_flags_applied==7 && g_fg_running && g_fg_on_frame==40 && g_fg_enables==3);
  sdk_result=sl::Result::eOk;
  apply(0, 9, 101);
  check(g_fg_applied==0 && g_flags_applied==9 && !g_fg_running && g_fg_enables==3);
  sdk_result=sl::Result::eFailure;
  apply(2, 11, 102);
  check(g_fg_applied==0 && g_flags_applied==9 && !g_fg_running && g_fg_on_frame==40 && g_fg_enables==3);
  sdk_result=sl::Result::eOk;
  apply(2, 11, 103);
  check(g_fg_applied==2 && g_flags_applied==11 && g_fg_running && g_fg_on_frame==103 && g_fg_enables==4);
  apply(2, 12, 104);
  check(g_flags_applied==12 && g_fg_running && g_fg_on_frame==103 && g_fg_enables==4);
  std::puts("18 injected failures and successful retries; FG failure/success transitions");
}
'''

if __name__ == '__main__':
    unittest.main()
