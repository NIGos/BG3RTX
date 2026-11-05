#pragma once
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <limits>
#include <vector>

namespace bg3::replay {
struct AbSummary { double median, low, high; };
struct ByteDifference { size_t count = 0, first = 0; };
inline ByteDifference byte_difference(const std::vector<uint8_t>& a, const std::vector<uint8_t>& b) {
  ByteDifference result;
  const size_t common = std::min(a.size(), b.size());
  for (size_t i = 0; i < common; ++i) if (a[i] != b[i]) {
    if (!result.count) result.first = i;
    ++result.count;
  }
  if (a.size() != b.size()) {
    if (!result.count) result.first = common;
    result.count += std::max(a.size(), b.size()) - common;
  }
  return result;
}
// Distribution-free two-sided interval for the population median. Select the
// narrowest order statistics whose binomial tails each have probability <= .025.
inline AbSummary ab_summary(std::vector<double> values) {
  if (values.empty()) return {0, -std::numeric_limits<double>::infinity(), std::numeric_limits<double>::infinity()};
  std::sort(values.begin(), values.end());
  const size_t n = values.size();
  const double middle = n % 2 ? values[n / 2] : (values[n / 2 - 1] + values[n / 2]) * .5;
  size_t k = 0;
  // Start at the centre to avoid underflow in 2^-n for large sample counts.
  long double term = std::exp(std::lgamma(static_cast<long double>(n) + 1) -
      std::lgamma(static_cast<long double>(n / 2) + 1) - std::lgamma(static_cast<long double>(n - n / 2) + 1) -
      static_cast<long double>(n) * std::log(2.L));
  long double cdf = n % 2 ? .5L : .5L + term * .5L;
  for (size_t j = n / 2;; --j) {
    if (cdf <= .025L) { k = j + 1; break; }
    cdf -= term;
    if (j == 0) break;
    term *= static_cast<long double>(j) / static_cast<long double>(n - j + 1);
  }
  return {middle, k ? values[k - 1] : -std::numeric_limits<double>::infinity(),
                  k ? values[n - k] : std::numeric_limits<double>::infinity()};
}
inline bool ab_self_test() {
  std::vector<double> v;
  bool okay = true;
  for (unsigned pair = 0; pair < 40; ++pair) {
    double t[4];
    for (unsigned i = 0; i < 4; ++i) {
      const bool b = ((i == 1 || i == 2) != ((pair & 1) != 0));
      t[i] = 10 + (b ? -2.0 : 0.0) + (pair * 4 + i) * .125;
    }
    const double outer = (t[0] + t[3]) * .5, inner = (t[1] + t[2]) * .5;
    const double delta = pair & 1 ? outer - inner : inner - outer;
    okay &= delta == -2;
    v.push_back(delta);
  }
  const auto same = byte_difference({0, 1, 2}, {0, 1, 2});
  const auto bits = byte_difference({0, 1, 2, 3}, {0, 9, 2, 4});
  const auto size = byte_difference({0, 1}, {0, 1, 2});
  okay &= same.count == 0 && bits.count == 2 && bits.first == 1 && size.count == 1 && size.first == 2;
  const auto constant = ab_summary(v);
  okay &= constant.median == -2 && constant.low == -2 && constant.high == -2;
  for (unsigned i = 0; i < 40; ++i) v[i] = i;
  const auto ordered = ab_summary(v), small = ab_summary({1, 2});
  okay &= ordered.median == 19.5 && ordered.low == 13 && ordered.high == 26;
  okay &= std::isinf(small.low) && small.low < 0 && std::isinf(small.high) && small.high > 0;
  std::printf("%s paired statistics: ABBA/BAAB linear-drift cancellation, exact 40-sample median CI, small-sample bounds, byte mismatch controls\n", okay ? "PASS" : "FAIL");
  return okay;
}
} // namespace bg3::replay
