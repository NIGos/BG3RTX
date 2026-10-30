#pragma once
#include <condition_variable>
#include <deque>
#include <functional>
#include <mutex>
#include <thread>
#include <utility>

// Debug capture IO never blocks the presentation thread. close() drains accepted
// work in order; a full queue rejects new work instead of growing without bound.
template<class T> class AsyncCaptureQueue {
  const size_t capacity_;
  std::function<void(T&&)> consume_;
  std::mutex mu_;
  std::condition_variable cv_;
  std::deque<T> queue_;
  bool closed_ = false;
  std::thread worker_;
 public:
  AsyncCaptureQueue(size_t capacity, std::function<void(T&&)> consume)
      : capacity_(capacity), consume_(std::move(consume)), worker_([this] {
    for (;;) {
      std::unique_lock lock(mu_);
      cv_.wait(lock,[this] { return closed_ || !queue_.empty(); });
      if (queue_.empty()) return;
      T item=std::move(queue_.front()); queue_.pop_front();
      lock.unlock(); consume_(std::move(item));
    }
  }) {}
  bool submit(T item) {
    { std::lock_guard lock(mu_);
      if (closed_ || queue_.size() >= capacity_) return false;
      queue_.push_back(std::move(item));
    }
    cv_.notify_one(); return true;
  }
  void close() {
    { std::lock_guard lock(mu_); closed_=true; }
    cv_.notify_one();
    if (worker_.joinable()) worker_.join();
  }
  ~AsyncCaptureQueue() { close(); }
};
