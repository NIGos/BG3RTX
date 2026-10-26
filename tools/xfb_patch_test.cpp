// Offline check of the Track D transform feedback patch (layer/spirv_xfb.h):
//   xfb_patch_test <dir with .spv dumps> <out dir> [stride: 16, or 24 with the UV output]
// Patches every vertex shader module in <dir> and writes the result to
// <out dir>/<name>.spv, for spirv-val / spirv-dis. Prints counts.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <vector>

#include "../layer/spirv_xfb.h"

int main(int argc, char** argv) {
  if (argc < 3) {
    std::fprintf(stderr, "usage: xfb_patch_test <in dir> <out dir>\n");
    return 2;
  }
  namespace fs = std::filesystem;
  fs::create_directories(argv[2]);
  int total = 0, patched = 0, vertices = 0, with_uv = 0;
  const uint32_t stride = argc > 3 ? uint32_t(std::atoi(argv[3])) : 16;
  for (const auto& e : fs::directory_iterator(argv[1])) {
    if (e.path().extension() != ".spv" || e.path().string().find(".xfb.") != std::string::npos) continue;
    std::ifstream f(e.path(), std::ios::binary);
    std::vector<char> bytes((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    if (bytes.size() < 20 || bytes.size() % 4) continue;
    ++total;
    std::vector<uint32_t> code(bytes.size() / 4), out;
    std::memcpy(code.data(), bytes.data(), bytes.size());
    bool vertex = false;
    for (size_t p = 5; p < code.size() && (code[p] >> 16);) {
      if ((code[p] & 0xffff) == 15 && code[p + 1] == 0) vertex = true;
      p += code[p] >> 16;
    }
    vertices += vertex;
    int uv = -1;
    if (!bg3::patch_vs(code.data(), code.size(), out, stride, &uv)) {
      if (vertex) std::printf("not patched: %s\n", e.path().filename().string().c_str());
      continue;
    }
    ++patched;
    with_uv += uv >= 0;
    std::ofstream o(fs::path(argv[2]) / e.path().filename(), std::ios::binary);
    o.write(reinterpret_cast<const char*>(out.data()), std::streamsize(out.size() * 4));
  }
  std::printf("%d modules, %d vertex shaders, %d patched (stride %u, %d with a UV output)\n", total, vertices, patched,
              stride, with_uv);
  return 0;
}
