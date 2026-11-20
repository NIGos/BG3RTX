#include "../core/frame_memo.h"

int main() {
  bg3::FrameMemo<int> m;
  int loads = 0, value = 7;
  auto load = [&] { ++loads; return value; };
  static const char kA[] = "a", kB[] = "b";
  // One load per key per frame; later reads in the frame return the first value.
  if (m.get(1, kA, load) != 7 || loads != 1) return 1;
  value = 9;
  if (m.get(1, kA, load) != 7 || loads != 1) return 2;
  // Another key loads on its own.
  if (m.get(1, kB, load) != 9 || loads != 2 || m.size() != 2) return 3;
  // A new frame reloads: the change is seen at the next frame.
  if (m.get(2, kA, load) != 9 || loads != 3 || m.size() != 1) return 4;
  // An older frame index (any change) reloads too.
  value = 4;
  if (m.get(1, kA, load) != 4 || loads != 4) return 5;
  // clear() forgets everything, also for the same frame.
  m.clear();
  value = 5;
  if (m.get(1, kA, load) != 5 || loads != 5) return 6;
  // Pointer values (the persist words) are memoised the same way.
  bg3::FrameMemo<void*> p;
  int word = 0;
  int calls = 0;
  auto lookup = [&]() -> void* { ++calls; return &word; };
  if (p.get(3, kA, lookup) != &word || p.get(3, kA, lookup) != &word || calls != 1) return 7;
  return 0;
}
