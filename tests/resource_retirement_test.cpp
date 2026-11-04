#include "../layer/resource_retirement.h"
#include <cstdint>

int main() {
  using resource_retirement::complete;
  if (complete(5, 4, 10, 11, false)) return 1;  // another queue never covers this object
  if (!complete(5, 4, 10, 11, true)) return 2;
  if (complete(5, 4, 10, 10, true)) return 3;   // current frame is not yet covered
  if (!complete(5, 5, 10, 0, false)) return 4;  // a completed device-idle covers every queue
  if (complete(5, 4, UINT64_MAX, 0, true)) return 5;  // no frame wraparound
  // A stalled GPU retains the object. Moving it out for polling must still
  // retain a concurrently freed backing allocation until completion.
  using resource_retirement::can_free_memory;
  if (can_free_memory(0, true) || can_free_memory(1, false)) return 6;
  if (can_free_memory(1, true) || !can_free_memory(0, false)) return 7;
  for (uint64_t done = 0; done <= 10; ++done)
    if (complete(5, 4, 10, done, true)) return 8;
  if (!complete(5, 4, 10, 11, true)) return 9;
  if (resource_retirement::batch_size(10000) != 128 ||
      resource_retirement::batch_size(0) != 0 ||
      resource_retirement::batch_size(17) != 17) return 10;
  return 0;
}
