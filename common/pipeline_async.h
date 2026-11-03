#pragma once
// Background pipeline builds (core/gi.cpp refresh). A new variant request (native hair, OMM, a key
// toggled live) used to compile with DXC and create its VkPipeline on the render thread: up to 10 s
// of DXC plus 7 s of driver pipeline creation per variant, minutes of freeze on the first frames with
// rt_native_hair (ds1 log 2026-11-02 10:03). Builds run on a small pool of worker threads instead; the
// render thread polls each frame and installs the result when it is ready. Until then the caller skips
// the pass (a new request) or keeps the previous pipeline of the same request (a hot reload).
//
// - One build per key (request + layout). poll() starts it, reports it busy, then hands the result over
//   once (the entry is erased). A failed build is not retried for kFailCooldown polls of that key.
// - `dep` (the pipeline layout a build reads) must outlive the build: park() defers its destruction
//   until no build of it is running, and discards the unclaimed results built against it.
// - join() (core unload) stops the workers, drops the queued builds and discards unclaimed results.
// Workers persist (one DXC instance each in the layer; per-job threads would leak one per build).
// tests/pipeline_async_test.cpp.
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace bg3::pipe_async {

struct Result {
  uint64_t pipeline = 0;   // VkPipeline (0: failed)
  uint64_t generation = 0;
  bool ok = false;
  double pipeline_ms = 0;   // driver creation on the worker; preserves the pipeline timing log
};

enum class State { kStarted, kBuilding, kReady, kFailed };

inline constexpr unsigned kFailCooldown = 240;   // polls of a failed key before it is built again

class Builds {
 public:
  using Fn = std::function<Result()>;
  using Discard = std::function<void(const Result&)>;
  using Init = std::function<void()>;
  // `discard`: destroys an unclaimed pipeline (defer_destroy). `init`: run first on each worker (priority).
  explicit Builds(unsigned workers, Discard discard, Init init = {})
      : workers_(workers ? workers : 1), discard_(std::move(discard)), init_(std::move(init)) {}
  ~Builds() { join(); }
  Builds(const Builds&) = delete;
  Builds& operator=(const Builds&) = delete;

  State poll(const std::string& key, uint64_t dep, const Fn& fn, Result* out) {
    std::unique_lock<std::mutex> lock(mu_);
    drain_locked();
    auto it = jobs_.find(key);
    if (it != jobs_.end()) {
      Job& j = *it->second;
      if (!j.done) return State::kBuilding;
      const Result r = j.result;
      jobs_.erase(it);
      if (out) *out = r;
      if (!r.ok) failed_[key] = kFailCooldown;
      return r.ok ? State::kReady : State::kFailed;
    }
    auto f = failed_.find(key);
    if (f != failed_.end()) {
      if (f->second-- > 0) return State::kFailed;
      failed_.erase(f);
    }
    if (stopping_) return State::kFailed;
    auto job = std::make_shared<Job>();
    job->dep = dep;
    job->fn = fn;
    jobs_[key] = job;
    queue_.push_back(job);
    ++started_;
    while (threads_.size() < workers_) threads_.emplace_back([this] { work(); });
    cv_.notify_one();
    return State::kStarted;
  }

  // A build of `dep` is queued or running.
  bool busy(uint64_t dep) const {
    std::lock_guard<std::mutex> lock(mu_);
    return busy_locked(dep);
  }

  // Destroys `dep` (via `destroy`) now if no build reads it, else on a later poll/join once none does.
  void park(uint64_t dep, std::function<void()> destroy) {
    if (!dep) return;
    std::unique_lock<std::mutex> lock(mu_);
    parked_.push_back({dep, std::move(destroy)});
    drain_locked();
  }

  uint64_t started_total() const {
    std::lock_guard<std::mutex> lock(mu_);
    return started_;
  }

  size_t pending() const {
    std::lock_guard<std::mutex> lock(mu_);
    size_t n = 0;
    for (const auto& [k, j] : jobs_) n += j->done ? 0 : 1;
    return n;
  }

  // Tests: blocks until every queued build has finished.
  void wait_idle() {
    std::unique_lock<std::mutex> lock(mu_);
    idle_cv_.wait(lock, [this] {
      for (const auto& [k, j] : jobs_)
        if (!j->done) return false;
      return true;
    });
  }

  void join() {
    {
      std::lock_guard<std::mutex> lock(mu_);
      stopping_ = true;
      for (auto& j : queue_) j->done = true, j->result = Result{};   // dropped, never run
      queue_.clear();
    }
    cv_.notify_all();
    for (std::thread& t : threads_)
      if (t.joinable()) t.join();
    threads_.clear();
    std::lock_guard<std::mutex> lock(mu_);
    for (auto& [k, j] : jobs_)
      if (j->result.pipeline && discard_) discard_(j->result);
    jobs_.clear();
    for (auto& p : parked_)
      if (p.destroy) p.destroy();
    parked_.clear();
    failed_.clear();
    stopping_ = false;
  }

 private:
  struct Job {
    uint64_t dep = 0;
    Fn fn;
    Result result;
    bool done = false;
    bool running = false;
  };
  struct Parked {
    uint64_t dep;
    std::function<void()> destroy;
  };

  bool busy_locked(uint64_t dep) const {
    for (const auto& [k, j] : jobs_)
      if (j->dep == dep && !j->done) return true;
    return false;
  }

  // The parked layouts no build reads any more: their unclaimed results go first, then the layout.
  void drain_locked() {
    size_t w = 0;
    for (size_t i = 0; i < parked_.size(); ++i) {
      Parked& p = parked_[i];
      if (busy_locked(p.dep)) {
        if (w != i) parked_[w] = std::move(p);
        ++w;
        continue;
      }
      for (auto it = jobs_.begin(); it != jobs_.end();)
        if (it->second->dep == p.dep && it->second->done) {
          if (it->second->result.pipeline && discard_) discard_(it->second->result);
          it = jobs_.erase(it);
        } else {
          ++it;
        }
      if (p.destroy) p.destroy();
    }
    parked_.resize(w);
  }

  void work() {
    if (init_) init_();
    for (;;) {
      std::shared_ptr<Job> j;
      {
        std::unique_lock<std::mutex> lock(mu_);
        cv_.wait(lock, [this] { return stopping_ || !queue_.empty(); });
        if (queue_.empty()) return;   // stopping
        j = queue_.front();
        queue_.pop_front();
        j->running = true;
      }
      const Result r = j->fn ? j->fn() : Result{};
      {
        std::lock_guard<std::mutex> lock(mu_);
        j->result = r;
        j->done = true;
        j->running = false;
      }
      idle_cv_.notify_all();
    }
  }

  const unsigned workers_;
  Discard discard_;
  Init init_;
  mutable std::mutex mu_;
  std::condition_variable cv_, idle_cv_;
  std::unordered_map<std::string, std::shared_ptr<Job>> jobs_;
  std::deque<std::shared_ptr<Job>> queue_;
  std::unordered_map<std::string, unsigned> failed_;
  std::vector<Parked> parked_;
  std::vector<std::thread> threads_;
  uint64_t started_ = 0;
  bool stopping_ = false;
};

// Per-present status of the builds (core/gi.cpp gi_on_frame), the observables of the live gates:
// - a window of `window_frames` presents: frames with a build pending (a new request's pass skipped), builds
//   started, builds still pending at its end ("gi: background builds (N frames): ...");
// - a burst, from the frame a build started to the first frame with none pending: builds started, wall time,
//   presents and the longest present interval inside it ("gi: background builds done: ..."): the first enable of
//   native hair used to block one present for each variant (DXC up to 10.4 s + creation up to 7 s).
class Monitor {
 public:
  struct Window { unsigned frames = 0, waiting = 0, started = 0; size_t pending = 0; };
  struct Burst { unsigned builds = 0; uint64_t frames = 0; double ms = 0, longest_ms = 0; };
  enum : unsigned { kWindow = 1, kBurst = 2 };
  // One call per present: `pending` and `started_total` from Builds, `now_ms` a steady clock.
  unsigned tick(size_t pending, uint64_t started_total, double now_ms, unsigned window_frames, Window* w, Burst* b) {
    unsigned out = 0;
    const unsigned starts = unsigned(started_total - last_started_);
    last_started_ = started_total;
    const double dt = have_last_ ? now_ms - last_ms_ : 0.0;
    last_ms_ = now_ms;
    have_last_ = true;
    if (!in_burst_ && (pending || starts)) {   // started during the interval that ends here
      in_burst_ = true;
      burst_ = Burst{};
      t0_ = now_ms - dt;
    }
    if (in_burst_) {
      ++burst_.frames;
      burst_.builds += starts;
      if (dt > burst_.longest_ms) burst_.longest_ms = dt;
      if (!pending) {
        burst_.ms = now_ms - t0_;
        if (b) *b = burst_;
        in_burst_ = false;
        out |= kBurst;
      }
    }
    ++win_.frames;
    if (pending) ++win_.waiting;
    win_.started += starts;
    if (win_.frames >= (window_frames ? window_frames : 1)) {
      win_.pending = pending;
      if (w) *w = win_;
      win_ = Window{};
      out |= kWindow;
    }
    return out;
  }

 private:
  Window win_;
  Burst burst_;
  uint64_t last_started_ = 0;
  double last_ms_ = 0, t0_ = 0;
  bool have_last_ = false, in_burst_ = false;
};

}  // namespace bg3::pipe_async
