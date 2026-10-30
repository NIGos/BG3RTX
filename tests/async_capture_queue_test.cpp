#include "../common/async_capture_queue.h"
#include <latch>
#include <vector>
#include <cstdio>
int main() {
  std::latch entered(1), release(1);
  std::vector<int> written;
  AsyncCaptureQueue<int> q(1,[&](int&& x) {
    if (x==1) { entered.count_down(); release.wait(); }
    written.push_back(x);
  });
  bool ok=q.submit(1); entered.wait();
  ok=ok && q.submit(2) && !q.submit(3); // bounded while the writer is stalled
  release.count_down(); q.close();
  ok=ok && written==std::vector<int>({1,2}) && !q.submit(4);
  if (!ok) std::puts("capture queue ordering/drain/backpressure failed");
  return ok ? 0 : 1;
}
