## Tracker forgets a destroyed pool's sets before the driver call (branch claude/fix-tracker-pool, pure fix, 2026-11-23)
- Cause (hunt2_diff S1): `layer/tracker.cpp` DestroyDescriptorPool (claude/final-layer) erased the pool's sets after
  the driver destroy. A pool handle reused at once by another thread's CreateDescriptorPool + AllocateDescriptorSets put
  its new set in the stale list, and the late erase dropped that set's tracked descriptors.
- Fix: the erase now runs before `vkDestroyDescriptorPool`, as DestroyBuffer, DestroyImage and DestroyImageView already
  do since r2-layer. There is no key: this is the same behaviour without the race.
- Test: `descriptor_epoch_test` pool_reuse covers two orders. Forgetting before the re-created pool's allocation keeps
  the new set. The old order shows the hazard: the new set is forgotten.
- No live test is needed. The window is a few microseconds, so no in-game check is possible.
