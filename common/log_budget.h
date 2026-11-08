// Per-period line budget for a high-rate log line (layer/devfault.cpp "[fault] range of ours").
// The first `lines` events of each `period` frames log individually; the rest are counted per tag and summarised in
// one line when the period ends (at the next event after it). Tags are copied.
#pragma once
#include <cstdint>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

namespace bg3::log_budget {

class Budget {
 public:
  Budget(uint32_t lines, uint32_t period) : lines_(lines), period_(period ? period : 1) {}
  // True: log this event individually. `summary` receives the previous period's summary when one is due ("" else).
  bool admit(const char* tag, uint64_t frame, std::string* summary) {
    if (summary) summary->clear();
    if (frame >= start_ + period_ || frame < start_) {
      if (summary) *summary = take_summary(frame);
      else take_summary(frame);
    }
    if (used_ < lines_) { ++used_; return true; }
    for (auto& c : counts_)
      if (c.first == tag) { ++c.second; return false; }
    counts_.emplace_back(tag, 1);
    return false;
  }
  // The summary of the suppressed events so far ("" when none); starts a new period at `frame`.
  std::string take_summary(uint64_t frame) {
    std::string s;
    uint64_t total = 0;
    for (const auto& c : counts_) total += c.second;
    if (total) {
      s = std::to_string(total) + " more in frames " + std::to_string(start_) + "-" + std::to_string(frame) + ":";
      for (size_t i = 0; i < counts_.size(); ++i)
        s += std::string(i ? ", " : " ") + counts_[i].first + " x" + std::to_string(counts_[i].second);
    }
    counts_.clear();
    used_ = 0;
    start_ = frame;
    return s;
  }

 private:
  uint32_t lines_, period_;
  uint32_t used_ = 0;
  uint64_t start_ = 0;
  std::vector<std::pair<std::string, uint64_t>> counts_;
};

}  // namespace bg3::log_budget
