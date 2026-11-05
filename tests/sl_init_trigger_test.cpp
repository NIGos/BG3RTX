// common/sl_init_trigger.h: when slSetVulkanInfo runs. No Vulkan.
#include "../common/sl_init_trigger.h"
#include <cstdio>
#include <cstdlib>
using namespace bg3::sl_init;
static unsigned checks;
#define CHECK(x) do{++checks;if(!(x)){std::printf("FAIL %d: %s\n",__LINE__,#x);std::exit(1);}}while(0)
int main(){
 // Key off: the old behaviour (first vkGetDeviceQueue when early, else the swapchain).
 CHECK(init_now(0,false,Trigger::DeviceQueue));
 CHECK(!init_now(0,false,Trigger::GameNgxInit));
 CHECK(init_now(0,false,Trigger::Swapchain));
 CHECK(!init_now(1,false,Trigger::DeviceQueue));
 CHECK(!init_now(1,false,Trigger::GameNgxInit));
 CHECK(init_now(1,false,Trigger::Swapchain));
 // Key on: a vkGetDeviceQueue (maybe from a layer above, inside its vkCreateDevice) never
 // starts it; the game's NGX Init does, and the swapchain stays the fallback.
 CHECK(!init_now(0,true,Trigger::DeviceQueue));
 CHECK(init_now(0,true,Trigger::GameNgxInit));
 CHECK(init_now(0,true,Trigger::Swapchain));
 // At the swapchain (sl_init_at=1) the key changes nothing.
 CHECK(!init_now(1,true,Trigger::DeviceQueue));
 CHECK(!init_now(1,true,Trigger::GameNgxInit));
 CHECK(init_now(1,true,Trigger::Swapchain));
 std::printf("sl_init_trigger_test: %u checks passed\n",checks);
 return 0;
}
