#include <windows.h>
#include "../core/completion_lease.h"
#include <array>
#include <cstdio>
#include <unordered_map>

const Bg3HostApi* host = nullptr;
namespace {
struct State { uint32_t flags = BG3_RECORDING_EXECUTABLE; uint64_t frame = 0; unsigned refs = 0; };
std::unordered_map<uint64_t, State> states;
uint64_t done = 0;
uint64_t identity(VkCommandBuffer cmd) { return uint64_t(uintptr_t(cmd)); }
void retain(uint64_t id) { ++states[id].refs; }
void release(uint64_t id) { --states.at(id).refs; }
Bg3RecordingState query(uint64_t id) {
  const auto i = states.find(id);
  if (i == states.end()) return {};
  const auto& s = i->second;
  const uint32_t complete = (!(s.flags & BG3_RECORDING_SUBMITTED) || done >= s.frame) ? BG3_RECORDING_COMPLETE : 0;
  return {s.flags | complete, s.frame};
}
void record(CoreCompletionLease& lease, uint64_t id) { lease.record(reinterpret_cast<VkCommandBuffer>(uintptr_t(id))); }
void submit(uint64_t id, uint64_t frame) { states.at(id).flags |= BG3_RECORDING_SUBMITTED; states.at(id).frame = frame; }
void discard(uint64_t id) { states.at(id).flags &= ~BG3_RECORDING_EXECUTABLE; }
#define CHECK(x) do { if (!(x)) { std::printf("FAIL line %d: %s\n", __LINE__, #x); return false; } } while (0)
bool steady(unsigned lag) {
  states.clear(); done = 0;
  std::array<CoreCompletionLease, 8> ring;
  unsigned display = 0, reads = 0;
  for (uint64_t frame = 1; frame <= 80; ++frame) {
    done = frame > lag ? frame - lag : 0;
    if (frame > lag) discard(frame - lag);
    auto& slot = ring[frame % ring.size()];
    CHECK(slot.reusable());
    if (frame > ring.size()) { CHECK(slot.readable()); ++reads; }
    slot.clear(); record(slot, frame); record(slot, frame);
    CHECK(states.at(frame).refs == 1); // repeated use in one recording acquires once
    submit(frame, frame);
    CHECK(!slot.reusable());
    // FG adds displayed frames, without advancing app-frame completion or leases.
    for (unsigned fg = 0; fg < 3; ++fg) { ++display; CHECK(!slot.reusable()); }
  }
  CHECK(display == 240 && reads == 72);
  for (auto& slot : ring) slot.clear();
  for (const auto& [id, state] : states) CHECK(state.refs == 0);
  return true;
}
bool exceptional() {
  states.clear(); done = 0;
  // Nine app updates with completion stalled must reject the ninth ring use.
  std::array<CoreCompletionLease, 8> stalled;
  for (uint64_t frame = 1; frame <= 8; ++frame) {
    auto& slot = stalled[frame % stalled.size()];
    CHECK(slot.reusable()); record(slot, frame); submit(frame, frame); discard(frame);
  }
  CHECK(!stalled[1].reusable());
  done = 1; CHECK(stalled[1].reusable() && stalled[1].readable());
  for (auto& slot : stalled) slot.clear();
  states.clear(); done = 0;
  CoreCompletionLease lease;
  record(lease, 1);
  CHECK(!lease.reusable() && !lease.readable());
  discard(1); // initialization/upload recorded then discarded, never submitted
  CHECK(lease.reusable() && !lease.readable());
  lease.clear();
  record(lease, 2); submit(2, 2); discard(2);
  done = 1;
  CHECK(!lease.reusable() && !lease.readable()); // a stall lasts beyond any frame age
  done = 2;
  CHECK(lease.reusable() && lease.readable());
  lease.clear();
  record(lease, 3); submit(3, 3); done = 3;
  CHECK(!lease.reusable() && !lease.readable()); // completed executable may resubmit
  submit(3, 7); discard(3);
  CHECK(!lease.reusable());
  done = 7;
  CHECK(lease.reusable() && lease.readable());
  // Compose slots have separate lighting and forward-pass consumers.
  record(lease, 4); submit(4, 8); discard(4);
  CHECK(!lease.reusable() && !lease.readable());
  done = 8; CHECK(lease.reusable() && lease.readable());
  lease.clear();
  CHECK(states.at(3).refs == 0 && states.at(4).refs == 0);
  return true;
}
}
int main() {
  Bg3HostApi api{};
  api.recording_index = identity; api.retain_recording = retain;
  api.release_recording = release; api.recording_state = query; host = &api;
  if (!steady(2) || !steady(3) || !exceptional()) return 1;
  puts("PASS core completion leases: 2/3 frames in flight, FG, discard, stall, resubmission and multiple consumers");
}
