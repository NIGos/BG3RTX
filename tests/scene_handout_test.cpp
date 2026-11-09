// Scene handout increment 3: the slot state machine (common/scene_handout.h) under every interleaving of the four
// actors the layer sees: capture begin and end (G-buffer thread), the lighting's first RT query, and the submits.
// A small model of layer/rtscene.cpp around it (slot retirement by GPU-completed frame, set contents, the fallback
// copy) checks the safety rules, not only the machine's bookkeeping:
//   S1 when a submitted batch executes a lighting user of set R, set R describes a scene whose build already
//      executed (its own build earlier in the batch or in an earlier batch) or a copy of P, which is still live;
//   S2 a slot bound by a pending recording is never picked for another build or rewritten after its first submit;
//   S3 a fallback never leaves R as the current scene; a later capture end never writes R once it was resolved;
//   S4 with the keys off nothing is reserved and every open handout still resolves (toggling at runtime).
#include "../common/scene_handout.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

using namespace bg3::scene_handout;
static int g_fail = 0;
static bool g_skip_copy = false;   // mutation control: without the fallback copy S1 must fail
#define CHECK(c) do { if (!(c)) { std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); ++g_fail; } } while (0)

namespace {
constexpr int kSlots = 8;
struct Slot { bool built = false; uint64_t last_use = 0, build_frame = 0; int content = -1; bool build_executed = false; };

// The layer around the machine (layer/rtscene.cpp): slots, the current scene, recordings and their submits.
struct Layer {
  Machine m;
  Slot s[kSlots];
  int cur = -1;
  uint64_t frame = 10, gpu_done = 9;
  bool keys_on = true;
  // recordings: id -> slot bound (set) or -1; build records: rec -> slot built
  struct Rec { uint64_t id; int set_slot = -1; bool build = false; int build_slot = -1; bool submitted = false; bool handed = false; };
  std::vector<Rec> recs;
  uint64_t next_id = 1;
  int reserved_for_capture = -1;   // the slot the open capture will build (R or a fresh pick)
  std::string log;

  bool retired(int i) const { return !m.pinned(i) && i != cur && gpu_done >= s[i].last_use + 2; }
  int pick() {
    for (int i = 0; i < kSlots; ++i) if (retired(i)) return i;
    return -1;
  }
  uint64_t new_rec() { recs.push_back({next_id}); return next_id++; }
  Rec* rec(uint64_t id) { for (auto& r : recs) if (r.id == id) return &r; return nullptr; }

  void capture_begin() {
    if (!keys_on || cur < 0) return;
    const int p = pick();
    m.reserve(frame, p, cur);
  }
  // Capture end: the build goes into R (reserved) or a fresh pick; returns the G-buffer recording.
  uint64_t capture_end(bool ok) {
    const uint64_t g = new_rec();
    int slot = m.commit_slot();
    const bool reserved = slot >= 0;
    if (slot < 0) slot = pick();
    if (slot < 0) return g;
    CHECK(retired(slot) || (reserved && m.pinned(slot)));   // S2: never a slot still bound by pending work
    Rec* r = rec(g);
    r->build = true; r->build_slot = slot;
    if (ok) {
      s[slot].built = true; s[slot].build_frame = frame; s[slot].content = slot; s[slot].build_executed = false;
      s[slot].last_use = frame;
      cur = slot;
      if (reserved) m.commit_done(slot, true, g, frame); else m.note_commit(frame);
    } else if (reserved) {
      m.commit_done(slot, false, 0, frame);
    }
    return g;
  }
  // The lighting's first query: a handout or the ordinary path (wait or committed). Returns the recording.
  uint64_t lighting(bool* handed = nullptr) {
    const uint64_t l = new_rec();
    const int r = keys_on ? m.hand_out(frame, l) : -1;
    if (handed) *handed = r >= 0;
    rec(l)->set_slot = r >= 0 ? r : cur;
    rec(l)->handed = r >= 0;
    if (r >= 0) s[r].last_use = std::max(s[r].last_use, frame);
    else if (cur >= 0) s[cur].last_use = std::max(s[cur].last_use, frame);
    return l;
  }
  // A submit of recordings in order; resolution first (as layer.cpp calls it before the driver), then execution.
  void submit(std::vector<uint64_t> batch) {
    auto index = [&](uint64_t user) { for (size_t i = 0; i < batch.size(); ++i) if (batch[i] == user) return int(i); return -1; };
    auto before = [&](uint64_t record, int at) {
      const Rec* r = rec(record);
      if (r && r->submitted) return true;
      for (int i = 0; i < at; ++i) if (batch[size_t(i)] == record) return true;
      return false;
    };
    const Resolution res = m.resolve(frame, 8, index, before);
    if (res.resolved && !res.in_order && !g_skip_copy) {
      CHECK(s[res.prev].built);   // S1: P is a built scene
      s[res.slot].content = s[res.prev].content;      // copy set P -> R
      if (res.poison && cur == res.slot) cur = res.prev;   // S3
      s[res.prev].last_use = std::max(s[res.prev].last_use, frame);
    }
    if (res.resolved) s[res.slot].last_use = std::max(s[res.slot].last_use, frame);
    for (uint64_t id : batch) {   // execution in batch order
      Rec* r = rec(id);
      r->submitted = true;
      if (r->build && r->build_slot >= 0 && s[r->build_slot].content == r->build_slot) s[r->build_slot].build_executed = true;
      // The ordinary path (no handout) relies on the game submitting the G-buffer first, as today: S1 is the
      // handout's own guarantee, checked for the recordings that bound a set before its build.
      if (r->set_slot >= 0 && r->handed) {
        const Slot& bound = s[r->set_slot];
        const int scene = bound.content;
        // S1: the traced scene's build already executed (its own slot) or it is a copy of an executed scene.
        CHECK(scene >= 0 && s[scene].build_executed);
      }
    }
  }
  void present() { ++frame; gpu_done = frame - 1; }
};

Layer warm() {   // one ordinary frame first: a valid current scene P
  Layer L;
  L.keys_on = false;
  const uint64_t g = L.capture_end(true);
  const uint64_t l = L.lighting();
  L.submit({g, l});
  L.present();
  L.keys_on = true;
  return L;
}
}  // namespace

static void scripted() {
  {  // 1 in order: capture begin, lighting, capture end ok, one batch G-buffer first
    Layer L = warm();
    const int P = L.cur;
    L.capture_begin();
    bool handed = false;
    const uint64_t l = L.lighting(&handed);
    CHECK(handed);
    const int R = L.rec(l)->set_slot;
    CHECK(R != P && L.m.pinned(R) && L.m.pinned(P) && !L.retired(R) && !L.retired(P));
    const uint64_t g = L.capture_end(true);
    L.submit({g, l});
    CHECK(L.m.n.in_order == 1 && L.m.n.fallbacks() == 0 && L.cur == R && !L.m.open());
    CHECK(L.s[R].content == R);
  }
  {  // 2 lighting submitted before the capture ends: fallback not committed; the late build picks another slot
    Layer L = warm();
    const int P = L.cur;
    L.capture_begin();
    const uint64_t l = L.lighting();
    const int R = L.rec(l)->set_slot;
    L.submit({l});
    CHECK(L.m.n.fallback[uint32_t(Fallback::kNotCommitted)] == 1 && L.s[R].content == P);
    const uint64_t g = L.capture_end(true);
    CHECK(L.rec(g)->build_slot != R);   // S3: R is not written again while the lighting batch is pending
    L.submit({g});
  }
  {  // 3 build failed after the handout: fallback build failed, P stays current
    Layer L = warm();
    const int P = L.cur;
    L.capture_begin();
    const uint64_t l = L.lighting();
    const uint64_t g = L.capture_end(false);
    L.submit({g, l});
    CHECK(L.m.n.fallback[uint32_t(Fallback::kBuildFailed)] == 1 && L.cur == P);
  }
  {  // 4 committed but the lighting batch comes first: fallback order, R poisoned (P current again)
    Layer L = warm();
    const int P = L.cur;
    L.capture_begin();
    const uint64_t l = L.lighting();
    const uint64_t g = L.capture_end(true);
    L.submit({l, g});
    CHECK(L.m.n.fallback[uint32_t(Fallback::kOrder)] == 1 && L.cur == P && L.m.n.mismatches() == 1);
  }
  {  // 5 the capture ends before the lighting asks: no handout (committed first), no wait either
    Layer L = warm();
    L.capture_begin();
    const uint64_t g = L.capture_end(true);
    bool handed = true;
    const uint64_t l = L.lighting(&handed);
    CHECK(!handed && L.m.n.committed_first == 1 && L.rec(l)->set_slot == L.cur);
    L.submit({g, l});
  }
  {  // 6 the lighting asks before any capture begins: no reservation (the caller waits as before)
    Layer L = warm();
    bool handed = true;
    L.lighting(&handed);
    CHECK(!handed && L.m.n.handed == 0);
  }
  {  // 7 a handout never submitted (recording reset): dropped after 8 frames, R and P released
    Layer L = warm();
    L.capture_begin();
    L.lighting();
    for (int i = 0; i < 9; ++i) L.present();
    L.submit({});
    CHECK(L.m.n.fallback[uint32_t(Fallback::kDropped)] == 1 && !L.m.open());
  }
  {  // 8 keys toggled off with a handout open: still resolved; nothing new reserved while off
    Layer L = warm();
    L.capture_begin();
    const uint64_t l = L.lighting();
    L.keys_on = false;
    const uint64_t g = L.capture_end(true);
    L.submit({g, l});
    CHECK(L.m.n.in_order == 1);
    L.present();
    L.capture_begin();
    CHECK(!L.m.open());
  }
  {  // 9 a second capture begin keeps the reservation; a second user (water, other buffer) shares it
    Layer L = warm();
    L.capture_begin();
    const int R = L.m.slot;
    L.capture_begin();
    CHECK(L.m.slot == R && L.m.n.reserved == 1);
    const uint64_t l = L.lighting();
    const uint64_t w = L.new_rec();
    CHECK(L.m.use(w));
    L.rec(w)->set_slot = R;
    L.rec(w)->handed = true;
    const uint64_t g = L.capture_end(true);
    L.submit({g, l});
    L.submit({w});   // later batch: R already resolved in order and built
    CHECK(L.m.n.in_order == 1);
  }
  {  // 10 retirement: R and P are live from the handout until the lighting submit's GPU work is done
    Layer L = warm();
    const int P = L.cur;
    L.capture_begin();
    const uint64_t l = L.lighting();
    const int R = L.rec(l)->set_slot;
    const uint64_t g = L.capture_end(true);
    L.present(); L.present(); L.present();   // GPU far ahead of the handout frame: still pinned (not resolved)
    CHECK(!L.retired(R) && !L.retired(P));
    L.submit({g, l});
    const uint64_t at = L.frame;
    CHECK(!L.retired(R));
    L.present(); L.present();
    CHECK(L.gpu_done < at + 2 || L.retired(P) || P == L.cur);
    L.present();
    CHECK(L.gpu_done >= at + 2 && L.retired(P));
  }
  {  // 12 a later frame's lighting never joins an older, still unresolved handout (it takes the ordinary path)
    Layer L = warm();
    L.capture_begin();
    const uint64_t l = L.lighting();
    const int R = L.rec(l)->set_slot;
    L.present();
    bool handed = true;
    L.lighting(&handed);
    CHECK(!handed && L.m.slot == R && L.m.user_count == 1);
  }
  {  // 13 further command buffers: join the frame's handout by slot; never a newer reservation after it resolved
    Layer L = warm();
    L.capture_begin();
    const uint64_t l = L.lighting();
    const int R = L.rec(l)->set_slot;
    const uint64_t post = L.new_rec();
    CHECK(L.m.hand_out(L.frame, post, R) == R && L.m.user_count == 2);
    CHECK(L.m.hand_out(L.frame, L.new_rec(), (R + 1) % kSlots) == -1);   // another slot: refused
    const uint64_t g = L.capture_end(true);
    L.submit({g, l});
    L.capture_begin();   // the next capture's reservation, same frame number
    CHECK(L.m.hand_out(L.frame, L.new_rec(), R) == -1);   // the resolved handout is not joined
    CHECK(L.m.hand_out(L.frame, L.new_rec(), L.m.slot) == -1);   // nor the new reservation (not handed out)
    L.submit({post});
  }
  {  // 14 increment 4, OMM on a reserved slot: the commit's accelerate() hands back every user so far with its command
     // buffer (tickets registered on them); a user joining later sees `accelerated` (registers itself); a user whose
     // recording already ended cannot hold tickets: the layer downgrades byte 24 (modelled: traversal = reference).
    Layer L = warm();
    L.capture_begin();
    const uint64_t l = L.new_rec();
    const int R = L.m.hand_out(L.frame, l, -1, 0xA1);
    L.rec(l)->set_slot = R; L.rec(l)->handed = true;
    uint64_t users[Machine::kMaxUsers], handles[Machine::kMaxUsers];
    CHECK(L.m.accelerate(R, users, handles) == 0);   // not committing yet: refused
    const int c = L.m.commit_slot();
    CHECK(c == R);
    const uint32_t n = L.m.accelerate(R, users, handles);
    CHECK(n == 1 && users[0] == l && handles[0] == 0xA1 && L.m.accelerated);
    CHECK(L.m.accelerate((R + 1) % kSlots, users, handles) == 0);   // only the committing slot
    const uint64_t post = L.new_rec();
    CHECK(L.m.hand_out(L.frame, post, R, 0xB2) == R && L.m.accelerated);   // the joiner must register tickets itself
    L.m.commit_done(R, true, L.new_rec(), L.frame);
    CHECK(L.m.accelerated);
    L.m.close();
    CHECK(!L.m.accelerated && L.m.user_count == 0);
    L.capture_begin();
    CHECK(!L.m.accelerated);   // a new reservation starts without the previous one's acceleration
  }
  {  // 15 OMM ticket lifetime model: byte 24 may stay accelerated only if every user still records at registration
    struct User { bool recording; };
    auto keep_accelerated = [](std::vector<User> users) {
      bool all = true;
      for (const auto& u : users) all = u.recording && all;   // handout_tickets: cmd_recording(cmd) == user
      return all;
    };
    CHECK(keep_accelerated({{true}, {true}}));
    CHECK(!keep_accelerated({{true}, {false}}));   // the lighting ended before the commit: downgrade
    CHECK(keep_accelerated({}));                   // no handed-out user: the ordinary path registers per dispatch
  }
  {  // 11 stale reservation (capture began, never ended for 3 frames): not handed out
    Layer L = warm();
    L.capture_begin();
    L.present(); L.present(); L.present();
    bool handed = true;
    L.lighting(&handed);
    CHECK(!handed && L.m.n.unused == 1 && !L.m.open());
  }
}

// Every order of the four events of one frame (capture begin before its end; the G-buffer recording is submitted
// after the capture end; the lighting recording after its query), every batch split, build ok or failed, several
// frames in a row: the model's safety checks must hold in all of them.
static void interleavings() {
  enum Ev { kBegin, kQuery, kEnd, kSubmitG, kSubmitL, kSubmitBoth, kSubmitBothRev };
  int runs = 0;
  for (int ok = 0; ok < 2; ++ok)
    for (int variant = 0; variant < 3; ++variant) {   // 0 separate submits, 1 one batch G first, 2 one batch L first
      std::vector<int> ev = {kBegin, kQuery, kEnd};
      std::sort(ev.begin(), ev.end());
      do {
        auto pos = [&](int e) { return int(std::find(ev.begin(), ev.end(), e) - ev.begin()); };
        if (pos(kBegin) > pos(kEnd)) continue;
        Layer L = warm();
        for (int f = 0; f < 3; ++f) {   // three frames: retirement and reservation reuse across frames
          uint64_t g = 0, l = 0;
          bool lsub = false;
          for (int e : ev) {
            if (e == kBegin) L.capture_begin();
            if (e == kQuery) l = L.lighting();
            if (e == kEnd) g = L.capture_end(ok || f != 1);
            if (variant == 0 && e == kQuery && pos(kQuery) < pos(kEnd) && (f & 1)) { L.submit({l}); lsub = true; }
          }
          if (variant == 0) { if (!lsub) L.submit({l}); L.submit({g}); }
          if (variant == 1) L.submit({g, l});
          if (variant == 2) L.submit({l, g});
          CHECK(!L.m.open() || !L.m.handed);
          L.present();
          ++runs;
        }
      } while (std::next_permutation(ev.begin(), ev.end()));
    }
  std::printf("interleavings: %d frames checked\n", runs);
}

int main() {
  scripted();
  interleavings();
  {  // the safety check is not blind: the same interleavings without the fallback copy violate S1
    const int before = g_fail;
    g_skip_copy = true;
    interleavings();
    g_skip_copy = false;
    const int caught = g_fail - before;
    g_fail = before;
    std::printf("mutation control (no fallback copy): %d violations caught\n", caught);
    CHECK(caught > 0);
  }
  std::printf("%s: %d failure(s)\n", g_fail ? "FAIL" : "PASS", g_fail);
  return g_fail ? 1 : 0;
}
