// Dev check of hairconv::validate_full against the Python reference (tools/hair_groom/render.py via hcap.reference).
// Input: <name>.vin written by a dumper of the Python reference groom (u32 V, T, W, H, P, S; positions f32[V*3],
// uv f32[V*2], tris u32[T*3], mask u8[H*W*4], points f32[P*3], radii f32[P], offsets u32[S+1], root f32[P],
// head f64[6]). Prints one JSON line per file: iou / coverage / radiance per view and the time per thread count.
// usage: hairconv_validate_check <file.vin>... [--threads 1,4] [--repeat N]
#include "../hairconv.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

namespace {

template <class T>
bool take(std::ifstream& f, std::vector<T>& v, size_t n) {
  v.resize(n);
  return bool(f.read(reinterpret_cast<char*>(v.data()), std::streamsize(n * sizeof(T))));
}

bool load(const std::string& path, hairconv::Mesh& m, hairconv::Mask& k, hairconv::Groom& g, hairconv::Head& h) {
  std::ifstream f(path, std::ios::binary);
  uint32_t hd[6];
  if (!f.read(reinterpret_cast<char*>(hd), sizeof hd)) return false;
  const uint32_t V = hd[0], T = hd[1], W = hd[2], H = hd[3], P = hd[4], S = hd[5];
  std::vector<double> head;
  k.w = int(W);
  k.h = int(H);
  if (!take(f, m.pos, size_t(V) * 3) || !take(f, m.uv, size_t(V) * 2) || !take(f, m.tris, size_t(T) * 3) ||
      !take(f, k.rgba, size_t(W) * H * 4) || !take(f, g.points, size_t(P) * 3) || !take(f, g.radii, P) ||
      !take(f, g.offsets, size_t(S) + 1) || !take(f, g.root, P) || !take(f, head, 6))
    return false;
  for (int i = 0; i < 3; ++i) {
    h.centre[i] = head[i];
    h.radii[i] = head[3 + i];
  }
  return true;
}

void print_list(const char* key, const std::vector<double>& v) {
  std::printf("\"%s\": [", key);
  for (size_t i = 0; i < v.size(); ++i) std::printf("%s%.17g", i ? ", " : "", v[i]);
  std::printf("], ");
}

}  // namespace

int main(int argc, char** argv) {
  std::vector<std::string> files;
  std::vector<int> threads = {1, 4};
  int repeat = 3;
  for (int i = 1; i < argc; ++i) {
    if (!std::strcmp(argv[i], "--threads") && i + 1 < argc) {
      threads.clear();
      for (const char* s = argv[++i]; *s;) {
        threads.push_back(std::atoi(s));
        while (*s && *s != ',') ++s;
        if (*s) ++s;
      }
    } else if (!std::strcmp(argv[i], "--repeat") && i + 1 < argc) {
      repeat = std::max(1, std::atoi(argv[++i]));
    } else {
      files.push_back(argv[i]);
    }
  }
  int failures = 0;
  for (const std::string& path : files) {
    hairconv::Mesh m;
    hairconv::Mask k;
    hairconv::Groom g;
    hairconv::Head h;
    if (!load(path, m, k, g, h)) {
      std::fprintf(stderr, "%s: cannot read\n", path.c_str());
      ++failures;
      continue;
    }
    hairconv::Validation v;
    std::printf("{\"file\": \"%s\", ", path.c_str());
    for (int t : threads) {
      double best = 1e30;
      for (int r = 0; r < repeat; ++r) {
        const auto t0 = std::chrono::steady_clock::now();
        hairconv::Validation got = hairconv::validate_full(m, k, g, h, t);
        best = std::min(best, std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count());
        if (v.iou.empty()) v = got;
        if (got.iou != v.iou || got.coverage != v.coverage || got.radiance != v.radiance) {
          std::fprintf(stderr, "%s: result depends on threads/run\n", path.c_str());
          ++failures;
        }
      }
      std::printf("\"seconds_%d\": %.4f, ", t, best);
    }
    print_list("iou", v.iou);
    print_list("coverage", v.coverage);
    print_list("radiance", v.radiance);
    std::printf("\"ok\": 1}\n");
    std::fflush(stdout);
  }
  return failures ? 1 : 0;
}
