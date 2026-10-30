#include <cstdint>
#include <cstdio>
using VkDeviceSize=uint64_t;
constexpr int VK_PIPELINE_BIND_POINT_COMPUTE=1;
struct Bg3Binding { uint64_t buffer=0,range=0; };
struct Snapshot { Bg3Binding x_prof; };
struct Host { int calls=0; bool query_binding(int,int,int,int,int,Bg3Binding* b){++calls;b->buffer=901;b->range=16384;return true;} template<class... T> void log(const char*,T...){} } h;
Host* host=&h;
int main(){int cmd=1;Snapshot s;Bg3Binding b;
// A stale-but-live optional binding was accepted at an earlier non-owner permutation.
s.x_prof.buffer=900;s.x_prof.range=144;
// Now the real native skin/translucent permutation exposes its correct profile buffer.
    if (!s.x_prof.buffer && host->query_binding(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, 1, 28, 0, &b) && b.buffer && b.range > 0) {
      s.x_prof = b;
      static VkDeviceSize logged_range = 0;
      static int n_logged = 0;
      if (b.range != logged_range && n_logged < 8) {
        ++n_logged;
        logged_range = b.range;
        host->log("gi: SSS profile list (set1 b28): %llu bytes (%llu profiles)", (unsigned long long)b.range,
                  (unsigned long long)(b.range / 64));
      }
    }
std::printf("Retained optional buffer=%llu range=%llu; queries of correct material source=%d\n",(unsigned long long)s.x_prof.buffer,(unsigned long long)s.x_prof.range,h.calls);
return s.x_prof.buffer==901?0:1;
}
