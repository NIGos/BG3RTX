"""Execute NR teardown with fake NGX/Vulkan; no device or runtime is loaded."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

from layer_init_failure_test import function


class Dlss5Teardown(unittest.TestCase):
    def test_features_release_before_bound_resources(self):
        compiler = shutil.which('cl.exe')
        self.assertIsNotNone(compiler, 'run scripts/build.ps1 -Test')
        source = FIXTURE + '\n' + function('layer/dlss5.cpp', 'struct Feature {') + ';\n'
        source += SLOT + '\n' + function('layer/dlss5.cpp', 'void slot_destroy(')
        source += '\n' + function('layer/dlss5.cpp', 'void dlss5_device_destroy(') + MAIN
        with tempfile.TemporaryDirectory(prefix='nr-teardown-') as temp:
            path = Path(temp)
            (path / 'test.cpp').write_text(source, encoding='utf-8')
            result = subprocess.run([compiler, '/nologo', '/EHsc', '/std:c++20', '/MT',
                                     'test.cpp', '/Fe:test.exe'], cwd=path, capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            result = subprocess.run([str(path / 'test.exe')], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)


FIXTURE = r'''
#include <array>
#include <atomic>
#include <memory>
#include <mutex>
#include <set>
using NVSDK_NGX_Handle=int; using NVSDK_NGX_Parameter=int;
namespace dlss5_frame { struct FeatureKey {}; }
int live_images=15, releases=0, destroyed_parameters=0;
bool drained=false, invalid=false;
struct Ngx {
  void (*release)(int*)=[](int*) { ++releases; invalid |= !drained || live_images!=15; };
  void (*destroy)(int*)=[](int*) { ++destroyed_parameters; invalid |= live_images!=15; };
} ngx;
struct Vk {
  int DeviceWaitIdle(int) { drained=true; return 0; }
  void DestroyQueryPool(int,int,void*) {}
  void DestroyDescriptorPool(int,int,void*) {}
};
struct DeviceData { Vk vk; int device=1; };
DeviceData* device{};
std::mutex mutex;
std::set<int> enabled_devices{1,2};
std::atomic<bool> retired_owner{};
bool history=true,capabilities_checked=true,need_reset=false;
void log(const char*,...) {}
struct Image { bool live=true; };
struct Uniform {};
void uniform_destroy(Uniform&) {}
void image_destroy(Image& im) { if(im.live) { --live_images; im.live=false; } }
'''

SLOT = r'''
struct Slot {
  int query{},pool{}; Uniform constants[3];
  Image original,proxy,neural,motion,depth;
  std::shared_ptr<Feature> feature;
};
std::array<Slot,3> slots;
std::shared_ptr<Feature> feature;
std::shared_ptr<int> pipelines;
'''

MAIN = r'''
int main() {
  DeviceData owner, foreign; foreign.device=2; device=&owner;
  int handles[2]{}, parameters[2]{};
  feature=std::make_shared<Feature>();
  feature->handle=&handles[0]; feature->parameters=&parameters[0];
  slots[0].feature=feature; slots[2].feature=feature;
  // A failed CreateFeature can retain its own parameters/handle only in a slot.
  slots[1].feature=std::make_shared<Feature>();
  slots[1].feature->handle=&handles[1]; slots[1].feature->parameters=&parameters[1];
  dlss5_device_destroy(&foreign);
  if(drained || releases || live_images!=15 || !feature || device!=&owner) return 1;
  dlss5_device_destroy(&owner);
  if(invalid) return 2;
  if(releases!=2 || destroyed_parameters!=2 || live_images || feature || device ||
     history || capabilities_checked || !need_reset || !retired_owner) return 3;
  dlss5_device_destroy(&owner);
  return releases==2 && destroyed_parameters==2 ? 0 : 4;
}
'''

if __name__ == '__main__':
    unittest.main()
