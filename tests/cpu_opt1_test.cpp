#include "../common/cpu_build_keys.h"
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <random>
#include <unordered_set>

static void check(bool ok) { if (!ok) std::abort(); }
int main() {
  bg3::cpu_opt::BuildKeys keys;
  std::mt19937_64 rng(42);
  std::vector<uint64_t> input{0, UINT64_MAX, 0, UINT64_MAX};
  for (int i = 0; i < 12000; ++i) input.push_back(i % 3 ? rng() : uint64_t(i % 4096));
  std::unordered_set<uint64_t> reference;
  for (auto key : input) {
    check(keys.contains(key) == (reference.count(key) != 0));
    check(keys.insert(key) == reference.insert(key).second);
  }
  for (int i = 0; i < 20000; ++i) {
    const auto key = rng(); check(keys.contains(key) == (reference.count(key) != 0));
  }
  const auto capacity = keys.capacity();
  // Failed/empty builds, key-off interludes, rerecords and smaller scenes must
  // never retain membership. Repeated/held draw order remains the caller's.
  for (int round = 0; round < 12; ++round) {
    keys.clear(); reference.clear();
    check(!keys.contains(0) && !keys.contains(UINT64_MAX));
    for (size_t i = 0; i < input.size() / size_t(round + 1); ++i)
      check(keys.insert(input[i]) == reference.insert(input[i]).second);
    check(keys.capacity() == capacity); // zero table growth after warmup
  }
  // Simulate the dynamic loop: held entries bypass duplicate tracking and
  // failed allocation removes a slot, but must not forget the seen key.
  keys.clear(); reference.clear();
  for (size_t i = 0; i < input.size(); ++i) {
    if (i % 7 == 0) continue;
    check(keys.insert(input[i]) == reference.insert(input[i]).second);
  }
  // Bench the real full-frame lifecycle (new std set vs retained flat set),
  // including reset and membership probes. Timings are evidence, not a gate.
  input.resize(5000);
  auto bench = [&](bool optimized) {
    uint64_t receipt = 0;
    const auto start = std::chrono::steady_clock::now();
    for (int frame = 0; frame < 400; ++frame) {
      if (optimized) {
        keys.clear();
        for (auto key : input) receipt += keys.insert(key);
        for (auto key : input) receipt += keys.contains(key);
      } else {
        std::unordered_set<uint64_t> set;
        for (auto key : input) receipt += set.insert(key).second;
        for (auto key : input) receipt += set.count(key);
      }
    }
    const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now()-start).count()/400;
    return std::pair{ms, receipt};
  };
  const auto off = bench(false), on = bench(true);
  check(off.second == on.second);
  std::printf("cpu_opt1 identical decisions; 5000 keys/build: OFF %.4f ON %.4f ms/build; retained %zu slots\n",
              off.first, on.first, keys.capacity());
}
