// Export the launcher's actual layered settings, without duplicating release policy.
#include "../../../common/settings.h"
#include <fstream>
#include <iostream>
#include <iterator>
#include <map>
std::string read(const char* path) {
  std::ifstream f(path);
  if (!f) throw std::runtime_error(path);
  return {std::istreambuf_iterator<char>(f), {}};
}
int main(int argc, char** argv) {
  if (argc != 2) return 2;
  using namespace bg3::settings;
  auto m = code_defaults();
  apply_layer(m, parse_ini(read("bg3rtx.ini")));
  apply_layer(m, parse_ini(read("data/release.ini")));
  apply_layer(m, parse_ini(std::string("preset=") + argv[1]));
  resolve_derived(m); // Readiness compares against the renderer's resolved keys.
  for (const auto& kv : std::map<std::string, std::string>(m.begin(), m.end()))
    std::cout << kv.first << '=' << kv.second << '\n';
}
