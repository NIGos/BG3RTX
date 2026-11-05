#include "../tools/launcher/dlss5_runtime_policy.h"
#include <cstdio>
int main() {
  using launcher::dlss5::select;
  using launcher::dlss5::candidate_paths;
  unsigned failed{};
  auto check = [&](bool ok, const char* text) { std::printf("%s %s\n", ok ? "PASS" : "FAIL", text); failed += !ok; };
  check(candidate_paths(L"",L"").empty(), "no paths: discovery has no staged fallback");
  check(candidate_paths(L"C:/BG3",L"")==std::vector<std::wstring>{L"C:/BG3\\bin\\nvngx_dlssnr.dll"}, "game-only source resolves its bin runtime");
  check(candidate_paths(L"",L"D:/user/nvngx_dlssnr.dll")==std::vector<std::wstring>{L"D:/user/nvngx_dlssnr.dll"}, "user-only source remains explicit");
  const auto paths=candidate_paths(L"C:/BG3/",L"D:/user/nvngx_dlssnr.dll");
  check(paths==std::vector<std::wstring>{L"C:/BG3\\bin\\nvngx_dlssnr.dll",L"D:/user/nvngx_dlssnr.dll"}, "both sources preserve game-bin precedence and trim trailing slash");
  const auto choice=select({{true,true},{true,true}});
  check(choice&&paths[*choice]==L"C:/BG3\\bin\\nvngx_dlssnr.dll", "signed tie actually selects game-bin source path");
  check(!select({}), "missing runtimes do not select a candidate");
  check(!select({{false,true},{false,false}}), "signature cannot make an incompatible binary usable");
  check(select({{true,false}}) == 0, "compatible unsigned model is accepted under user policy");
  check(select({{true,false},{true,true}}) == 1, "signed user candidate beats unsigned game-bin");
  check(select({{true,true},{true,false}}) == 0, "signed game-bin candidate beats unsigned user selection");
  check(select({{true,true},{true,true}}) == 0, "game-bin wins when both compatible candidates are signed");
  check(select({{true,false},{true,false}}) == 0, "game-bin wins when both compatible candidates are unsigned");
  check(select({{false,true},{true,false},{true,true}}) == 2, "invalid signed file cannot mask signed fallback");
  check(select({{false,false},{true,false}}) == 1, "unsigned fallback remains available when first file is invalid");
  return failed ? 1 : 0;
}
