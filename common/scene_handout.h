// Scene handout increment 3 (docs/SCENE_HANDOUT_DESIGN.md 3.1): the slot state machine. Pure C++, no Vulkan, no
// locks: layer/rtscene.cpp owns one Machine under g_handout_mu (lock order g_mu -> g_handout_mu) and supplies the
// facts it cannot know (which slot is retired, whether a build record was submitted or sits earlier in a submit).
// tests/scene_handout_test.cpp drives every interleaving of capture begin, lighting record, capture end (commit or
// failed build) and submit through it.
//
// One reservation at a time (the scene the next capture end commits):
//   reserve (G-buffer thread, capture begin) -> slot R picked from the retired ones, P = the current valid scene;
//   hand_out (lighting thread, first RT query) -> R's set is bound before R is built; a user is a recording id;
//   commit (G-buffer thread, capture end) -> the build goes into R, or fails;
//   resolve (submit thread, a batch holding a user) -> in order: R committed and its build recording already submitted
//   or earlier in this batch; otherwise the fallback: the caller copies set P into set R before the submit (every
//   binding, the stamp with P's TLAS address and camera included), and R is never committed for that capture.
// R and P stay pinned (never retired) from the reservation until the resolution; the caller then stamps both with the
// resolving frame (SceneSlot::last_use), so they retire only after that submit's GPU work (present fence + 2).
#pragma once
#include <cstdint>

namespace bg3::scene_handout {

enum class Fallback : uint32_t { kNone = 0, kNotCommitted, kBuildFailed, kOrder, kDropped, kQuery, kCamera, kCount };
inline const char* fallback_name(Fallback f) {
  switch (f) {
    case Fallback::kNotCommitted: return "not committed";
    case Fallback::kBuildFailed: return "build failed";
    case Fallback::kOrder: return "order";
    case Fallback::kDropped: return "dropped";
    case Fallback::kQuery: return "hit material query";
    case Fallback::kCamera: return "hit material camera";
    default: return "none";
  }
}

struct Counters {
  uint64_t reserved = 0, handed = 0, in_order = 0, unused = 0, committed_first = 0, refused_busy = 0;
  uint64_t fallback[uint32_t(Fallback::kCount)] = {};
  uint64_t fallbacks() const { uint64_t n = 0; for (uint32_t i = 1; i < uint32_t(Fallback::kCount); ++i) n += fallback[i]; return n; }
  // Pairing mismatches: the handed-out scene was not the one the lighting's own submit carried.
  uint64_t mismatches() const { return fallback[uint32_t(Fallback::kNotCommitted)] + fallback[uint32_t(Fallback::kOrder)]; }
};

// What resolve() asks the caller to do for the reservation it resolved.
struct Resolution {
  bool resolved = false;      // a user of the open handout was in the batch (or it expired)
  bool in_order = false;      // nothing to do: R holds this capture's scene and its build runs first
  Fallback reason = Fallback::kNone;
  int slot = -1, prev = -1;   // R, P (fallback: copy set P -> R, downgrade P's traversal address)
  bool poison = false;        // R was committed (kOrder): it must not stay the current scene
  uint64_t frame = 0;         // the frame the handout was made in
};

// Increment 5 (ABI 31): a refusal of the OMM accelerated TLAS downgrades the slot's traversal address (byte 24) only
// when the caller's shaders trace it; a reference lookup (water) never does.
inline bool downgrade_on_refusal(bool traversal, bool fast, bool slot_accelerated) {
  return traversal && !fast && slot_accelerated;
}

// Increment 5: the water compose pass of a frame whose lighting stash is a handout (dummy TLAS + set R).
enum class WaterPick : uint32_t { kHandout = 0, kCommitted, kNewer, kNone };
// this_frame: the pass composes this frame's lighting; registered: the layer let its command buffer join the open
// handout; committed: the current committed scene is the stash's own frame (the handout resolved, never a wait);
// newer: a later frame's build is already committed and is this frame's (built >= now), so the newer pair is taken as
// pick_scene does for an ordinary stash (rt_scene_set does not wait then). Before, that case left the water
// uncomposed for one frame ("no scene", final-s8-ultra-high 2026-11-21: 1-3 per 60 frames, an on/off flip each).
inline WaterPick water_handout_pick(bool this_frame, bool registered, bool committed, bool newer = false) {
  if (this_frame && registered) return WaterPick::kHandout;
  if (committed) return WaterPick::kCommitted;
  if (newer && !this_frame) return WaterPick::kNewer;
  return WaterPick::kNone;
}
// The newer-scene condition of water_handout_pick: the committed build b is later than the stash's frame and not older
// than this frame (a scene of an older frame would make rt_scene_set wait on the game's recording thread).
inline bool water_handout_newer(bool have_committed, uint64_t b, uint64_t stash_frame, uint64_t now) {
  return have_committed && b > stash_frame && b >= now;
}

struct Machine {
  static constexpr uint32_t kMaxUsers = 8;
  enum class State : uint32_t { kIdle = 0, kReserved, kCommitted, kFailed };
  State state = State::kIdle;
  int slot = -1, prev = -1;
  uint64_t reserve_frame = 0, hand_frame = 0, rec = 0;
  bool handed = false, committing = false;
  uint64_t users[kMaxUsers] = {};
  uint64_t handles[kMaxUsers] = {};   // the users' command buffers (opaque: OMM tickets are registered on them)
  uint32_t user_count = 0;
  // Increment 4: the commit chose R's OMM accelerated TLAS (stamp byte 24). Every user recording must hold R's micro-map
  // tickets (registered while it records); a user that cannot get them downgrades byte 24 to the reference TLAS.
  bool accelerated = false;
  uint64_t committed_frame = 0;   // frame of the last scene commit (any slot): later lighting needs no handout
  Counters n;

  bool open() const { return state != State::kIdle; }
  bool pinned(int s) const { return open() && (s == slot || s == prev); }

  // Capture begin. `pick` is a retired slot the caller picked (or -1), `current` the scene the RT passes would trace
  // today (P, -1 if none is valid). An open reservation is kept (a second capture begin of the same capture, or a
  // capture that never ended: its handout resolves at the submit). Returns R or -1.
  int reserve(uint64_t frame, int pick, int current) {
    if (open()) return slot;
    if (pick < 0 || current < 0 || pick == current) return -1;
    state = State::kReserved;
    slot = pick; prev = current;
    reserve_frame = frame; hand_frame = 0; rec = 0;
    handed = committing = accelerated = query_expected = query_ok = false; user_count = 0;
    ++n.reserved;
    return slot;
  }

  // Lighting thread, first RT query of `frame`, recording `user`. Returns R, or -1: no reservation (the capture has
  // not begun: the caller waits as before) or this frame's scene is already committed (no wait needed).
  // `expect` >= 0: a further command buffer of the same frame registers for the slot it already bound (it joins only
  // that handout, never a newer reservation). A handout of an older frame that is still unresolved is never joined
  // by a later frame's lighting (that frame takes the ordinary path).
  int hand_out(uint64_t frame, uint64_t user, int expect = -1, uint64_t handle = 0) {
    if (expect >= 0) {
      if (!open() || !handed || slot != expect || hand_frame != frame) return -1;
      return use(user, handle) ? slot : -1;
    }
    if (handed && hand_frame != frame) return -1;
    if (committed_frame >= frame && !handed) { ++n.committed_first; return -1; }
    // A reservation whose capture began more than 2 frames ago and never ended (menus, a pass that stopped): never
    // hand it out; dropped unused.
    if (state == State::kReserved && !handed && !committing && reserve_frame + 2 < frame) { ++n.unused; close(); return -1; }
    if (state != State::kReserved && state != State::kCommitted && state != State::kFailed) return -1;
    if (state == State::kCommitted && !handed) return -1;   // committed before any handout: the ordinary path
    if (!handed) { handed = true; hand_frame = frame; ++n.handed; }
    use(user, handle);
    return slot;
  }

  // Another recording binds the handed-out set (core passes in other command buffers of the frame).
  bool use(uint64_t user, uint64_t handle = 0) {
    if (!handed || !open()) return false;
    for (uint32_t i = 0; i < user_count; ++i) if (users[i] == user) return true;
    if (user_count == kMaxUsers) { ++n.refused_busy; return false; }
    handles[user_count] = handle;
    users[user_count++] = user;
    return true;
  }
  // Commit with the OMM accelerated TLAS: the users so far (their command buffers get the tickets now; later users get
  // them in hand_out's caller, which sees `accelerated`). Only for the slot being committed.
  uint32_t accelerate(int s, uint64_t* user_out, uint64_t* handle_out) {
    if (!committing || s != slot) return 0;
    accelerated = true;
    for (uint32_t i = 0; i < user_count; ++i) { user_out[i] = users[i]; handle_out[i] = handles[i]; }
    return user_count;
  }

  // Capture end: the slot this build must write (R while reserved and unresolved), or -1 (pick as before).
  int commit_slot() {
    if (state != State::kReserved) return -1;
    committing = true;
    return slot;
  }
  // The build of the commit_slot() slot: `ok` with its build record, or failed.
  void commit_done(int s, bool ok, uint64_t record, uint64_t frame) {
    if (!committing || s != slot) return;
    committing = false;
    if (ok) { state = State::kCommitted; rec = record; committed_frame = frame; }
    else state = State::kFailed;
    // Not handed out: nothing was bound early, the reservation is simply used (or dropped).
    if (!handed) { if (!ok) ++n.unused; close(); }
  }
  // A commit of another slot (the reservation was not used for it) still tells later lighting the scene exists.
  void note_commit(uint64_t frame) { committed_frame = frame; }

  // Increment 4b (native hit materials): the commit also built the slot's hit-material query (or failed to); its
  // resolution then needs it (`accept`). A handed-out slot without a query, or whose users bound another camera than the
  // query's, resolves to P like a scene that did not commit (the frame pairing checks of the query stay unchanged).
  bool query_expected = false, query_ok = false;
  void commit_query(int s, bool ok) { if (committing && s == slot) { query_expected = true; query_ok = ok; } }

  // Submit of a batch. `user_in_batch(user)` -> index of that recording in the batch or -1; `record_before(rec, index)`
  // -> the build record was submitted before or sits in the batch before index. `now` for expiry: a handout whose
  // users were never submitted within `expire` frames resolves as dropped (the recordings were reset unsubmitted).
  template <class UserIndex, class RecordBefore>
  Resolution resolve(uint64_t now, uint32_t expire, UserIndex user_in_batch, RecordBefore record_before) {
    return resolve(now, expire, user_in_batch, record_before, [](int) { return Fallback::kNone; });
  }
  // `accept(slot)`: kNone, or why an in-order handout must still fall back (kCamera: a user's camera binding).
  template <class UserIndex, class RecordBefore, class Accept>
  Resolution resolve(uint64_t now, uint32_t expire, UserIndex user_in_batch, RecordBefore record_before, Accept accept) {
    Resolution r;
    if (!open() || !handed || committing) return r;   // committing: the caller holds g_mu, so never at a submit
    int first = -1;
    for (uint32_t i = 0; i < user_count; ++i) {
      const int at = user_in_batch(users[i]);
      if (at >= 0 && (first < 0 || at < first)) first = at;
    }
    const bool expired = first < 0 && now > hand_frame + expire;
    if (first < 0 && !expired) return r;
    r.resolved = true; r.slot = slot; r.prev = prev; r.frame = hand_frame;
    if (expired) r.reason = Fallback::kDropped;
    else if (state == State::kCommitted && record_before(rec, first)) {
      if (query_expected && !query_ok) { r.reason = Fallback::kQuery; r.poison = true; }
      else if (const Fallback f = accept(slot); f != Fallback::kNone) { r.reason = f; r.poison = true; }
      else r.in_order = true;
    }
    else if (state == State::kCommitted) { r.reason = Fallback::kOrder; r.poison = true; }
    else if (state == State::kFailed) r.reason = Fallback::kBuildFailed;
    else r.reason = Fallback::kNotCommitted;
    if (r.in_order) ++n.in_order; else ++n.fallback[uint32_t(r.reason)];
    close();
    return r;
  }

  void close() {
    state = State::kIdle; slot = prev = -1; handed = committing = accelerated = query_expected = query_ok = false; user_count = 0; rec = 0;
  }
};

}  // namespace bg3::scene_handout
