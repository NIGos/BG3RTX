#pragma once
#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace launcher::dlss5 {
struct CandidateStatus { bool compatible{}, signed_nvidia{}; };
inline std::vector<std::wstring> candidate_paths(std::wstring game, const std::wstring& user) {
  std::vector<std::wstring> paths;
  if(!game.empty()) {
    while(!game.empty()&&(game.back()==L'\\'||game.back()==L'/'))game.pop_back();
    paths.push_back(game+L"\\bin\\nvngx_dlssnr.dll");
  }
  if(!user.empty())paths.push_back(user);
  return paths;
}
// Caller orders game-bin before the explicit user path. Staged copies are not candidates.
// A compatible NVIDIA-signed candidate takes precedence over every unsigned one.
inline std::optional<std::size_t> select(const std::vector<CandidateStatus>& candidates) {
  for (bool trusted : {true, false})
    for (std::size_t i = 0; i < candidates.size(); ++i)
      if (candidates[i].compatible && candidates[i].signed_nvidia == trusted) return i;
  return std::nullopt;
}
}
