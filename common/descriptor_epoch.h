#pragma once

// Called under the tracker's exclusive lock after a successful allocation.
// A recycled handle identifies a new set, even when its layout is unchanged.
template <class Tracker, class Set, class Layout>
void descriptor_set_allocated(Tracker& tracked, Set set, Layout layout) {
  tracked.sets.erase(set);
  tracked.dynamic_counts.erase(set);
  tracked.set_layouts[set] = layout;
}
