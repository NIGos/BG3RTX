#pragma once

#include <array>
#include <cstdint>
#include <limits>
#include <optional>
#include <algorithm>
#include <vector>

namespace bg3::dlss5 {

using Luid = std::array<std::uint8_t, 8>;

struct Distribution { double median, p90, minimum, maximum; };
inline std::optional<Distribution> summarize(std::vector<double> samples) {
  if (samples.empty()) return std::nullopt;
  std::sort(samples.begin(), samples.end());
  const auto n = samples.size();
  return Distribution{(samples[(n - 1) / 2] + samples[n / 2]) * .5,
                      samples[(9 * n + 9) / 10 - 1], samples.front(), samples.back()};
}

// An invalid Vulkan deviceLUID must never select even a byte-identical DXGI LUID.
inline bool same_luid(const Luid& vulkan, bool valid, const Luid& dxgi) noexcept {
  return valid && vulkan == dxgi;
}

struct Ticket {
  std::uint64_t ready;
  std::uint64_t done;
};

// One fixture slot: A signals ready, D3D12 signals done, B consumes done. The slot
// cannot be reused until the caller has observed B complete. This is not a GPU
// wait: complete() is called only after that observation by the fixture owner.
class FenceBook {
 public:
  explicit FenceBook(std::uint64_t completed = 0) noexcept : completed_(completed) {}

  std::optional<Ticket> begin() noexcept {
    if (inflight_ || completed_ > std::numeric_limits<std::uint64_t>::max() - 2)
      return std::nullopt;
    reserved_ = {completed_ + 1, completed_ + 2};
    inflight_ = true;
    return reserved_;
  }

  bool complete(std::uint64_t value) noexcept {
    // This fixture owns the whole fence. An unexpected higher value means
    // another signal escaped our protocol; accepting it could reserve a later
    // signal below the actual timeline counter. Fail closed instead.
    if (!inflight_ || value != reserved_.done) return false;
    completed_ = reserved_.done;
    inflight_ = false;
    return true;
  }

  std::uint64_t completed() const noexcept { return completed_; }

 private:
  std::uint64_t completed_{};
  Ticket reserved_{};
  bool inflight_{};
};

}  // namespace bg3::dlss5
