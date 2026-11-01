// layer/shader_cache.cpp: the keys equal tools/shader_identity.py's (tests/shader_cache_key_golden.txt,
// written and checked by tests/shader_cache_key_test.py), a tree read from disk keys like the same
// files in memory, and the store: miss, hit, corrupt entries (size, magic, checksum, SPIR-V magic)
// are misses and deleted, non-SPIR-V is refused, pruning keeps the most recent entries.
// Usage: shader_cache_test <repository root>
#include "../layer/shader_cache.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace sc = bg3::shader_cache;

static int g_fail = 0, g_checks = 0;
#define CHECK(c)                                                  \
  do {                                                            \
    ++g_checks;                                                   \
    if (!(c)) {                                                   \
      std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c);    \
      ++g_fail;                                                   \
    }                                                             \
  } while (0)

static std::string unhex(const std::string& h) {
  std::string out;
  for (size_t i = 0; i + 1 < h.size(); i += 2) out += char(std::stoi(h.substr(i, 2), nullptr, 16));
  return out;
}

static void write_bytes(const std::string& path, const std::string& data) {
  std::ofstream f(path, std::ios::binary | std::ios::trunc);
  f.write(data.data(), std::streamsize(data.size()));
}

static std::string read_bytes(const std::string& path) {
  std::ifstream f(path, std::ios::binary);
  return std::string(std::istreambuf_iterator<char>(f), {});
}

static bool exists(const std::string& p) { return GetFileAttributesA(p.c_str()) != INVALID_FILE_ATTRIBUTES; }

int main(int argc, char** argv) {
  const std::string root = argc > 1 ? argv[1] : ".";
  // 1. Golden keys.
  std::ifstream g(root + "/tests/shader_cache_key_golden.txt", std::ios::binary);
  CHECK(g.good());
  std::string line, tool;
  sc::Tree mem;
  std::vector<std::pair<std::string, std::string>> files;
  struct Key {
    std::string request, entry, profile, key;
  };
  std::vector<Key> keys;
  while (std::getline(g, line)) {
    if (!line.empty() && line.back() == '\r') line.pop_back();
    if (line.empty() || line[0] == '#') continue;
    std::istringstream s(line);
    std::string kind;
    s >> kind;
    if (kind == "tool") s >> tool;
    if (kind == "file") {
      std::string path, hex;
      s >> path >> hex;
      files.emplace_back(path, unhex(hex));
      mem.add(path, unhex(hex));
    }
    if (kind == "key") {
      Key k;
      s >> k.request >> k.entry >> k.profile >> k.key;
      keys.push_back(k);
    }
  }
  CHECK(!tool.empty() && files.size() >= 10 && keys.size() >= 8);
  for (const Key& k : keys) {
    const std::string got = sc::request_key(mem, tool, k.request, k.entry, k.profile);
    const bool ok = got == (k.key == "-" ? std::string() : k.key);
    if (!ok) std::printf("key of %s:%s %s: C++ %s, Python %s\n", k.request.c_str(), k.entry.c_str(), k.profile.c_str(),
                         got.c_str(), k.key.c_str());
    CHECK(ok);
  }
  CHECK(sc::posix_normpath("shaders/gi/../../common/x.h") == "common/x.h");
  CHECK(sc::posix_normpath("shaders/../../up.h") == "../up.h");
  CHECK(sc::posix_normpath("//a/./b/") == "//a/b");
  CHECK(sc::posix_dirname("shaders/a.hlsl") == "shaders" && sc::posix_dirname("a") == "");

  // Scratch folder under %TEMP%.
  char tmp[MAX_PATH];
  GetTempPathA(MAX_PATH, tmp);
  const std::string base = std::string(tmp) + "bg3rtx_shader_cache_test_" + std::to_string(GetCurrentProcessId());
  std::vector<std::wstring> written;
  // 2. A tree read from disk keys like the same files in memory (sharc/ from its own folder).
  {
    const std::string disk = base + "\\tree", sharc = base + "\\sharc_include";
    for (const auto& [path, data] : files) {
      std::string p = path.compare(0, 6, "sharc/") == 0 ? sharc + "\\" + path.substr(6) : disk + "\\" + path;
      for (char& c : p) c = c == '/' ? '\\' : c;
      CHECK(sc::ensure_dir(p.substr(0, p.find_last_of('\\'))));
      // Non-ASCII names: through the wide API.
      std::wstring w(MultiByteToWideChar(CP_UTF8, 0, p.c_str(), -1, nullptr, 0), L'\0');
      MultiByteToWideChar(CP_UTF8, 0, p.c_str(), -1, w.data(), int(w.size()));
      std::ofstream f(w.c_str(), std::ios::binary | std::ios::trunc);
      f.write(data.data(), std::streamsize(data.size()));
      written.push_back(w);
    }
    auto t = sc::Tree::from_disk(disk, sharc);
    CHECK(t->size() == files.size());
    for (const Key& k : keys)
      CHECK(sc::request_key(*t, tool, k.request, k.entry, k.profile) == sc::request_key(mem, tool, k.request, k.entry, k.profile));
    // An include change invalidates its includers only.
    sc::Tree changed;
    for (const auto& [path, data] : files) changed.add(path, path == "common/w.h" ? std::string("// edited\n") : data);
    CHECK(sc::request_key(changed, tool, "b.hlsl", "main", "cs_6_0") != sc::request_key(mem, tool, "b.hlsl", "main", "cs_6_0"));
    CHECK(sc::request_key(changed, tool, "a.hlsl", "main", "cs_6_6") == sc::request_key(mem, tool, "a.hlsl", "main", "cs_6_6"));
    CHECK(sc::request_key(mem, tool + "2", "a.hlsl", "main", "cs_6_6") != sc::request_key(mem, tool, "a.hlsl", "main", "cs_6_6"));
  }

  // 3. The store.
  const std::string dir = base + "\\cache\\nested";
  CHECK(sc::ensure_dir(dir));
  const std::string key(64, 'a'), key2(64, 'b');
  std::vector<uint32_t> code = {0x07230203u, 0x00010600u, 0u, 42u, 0u, 7u, 8u, 9u};
  std::vector<uint32_t> out;
  CHECK(!sc::load(dir, key, out));                        // miss
  CHECK(sc::store(dir, key, code));
  CHECK(sc::load(dir, key, out) && out == code);          // hit
  CHECK(!sc::store(dir, key2, {1u, 2u, 3u, 4u, 5u}));     // not SPIR-V: refused
  CHECK(!sc::store(dir, key2, {0x07230203u}));            // too short
  const std::string file = dir + "\\" + key + ".spv";
  const std::string good = read_bytes(file);
  auto corrupt_is_miss = [&](std::string bad) {
    write_bytes(file, bad);
    std::vector<uint32_t> o;
    const bool miss = !sc::load(dir, key, o);
    return miss && !exists(file);   // deleted
  };
  CHECK(corrupt_is_miss(good.substr(0, good.size() - 4)));                 // truncated
  CHECK(corrupt_is_miss(good + std::string(4, '\0')));                     // trailing bytes
  { std::string b = good; b[good.size() - 1] ^= 1; CHECK(corrupt_is_miss(b)); }   // payload (checksum)
  { std::string b = good; b[0] ^= 1; CHECK(corrupt_is_miss(b)); }                  // header magic
  { std::string b = good; b[16] ^= 1; CHECK(corrupt_is_miss(b)); }                 // SPIR-V magic
  CHECK(corrupt_is_miss(std::string(3, 'x')));                             // tiny
  CHECK(sc::store(dir, key, code) && sc::load(dir, key, out) && out == code);   // recompiled entry
  CHECK(!exists(file + ".tmp"));

  // Pruning: 6 entries of ~1 KB with increasing times; cap 4 KB keeps the newest (<= 3 KB).
  std::vector<uint32_t> big(256, 0u);
  big[0] = 0x07230203u;
  std::vector<std::string> names;
  for (int i = 0; i < 6; ++i) {
    std::string k(64, char('0' + i));
    big[1] = uint32_t(i);
    CHECK(sc::store(dir, k, big));
    names.push_back(dir + "\\" + k + ".spv");
    HANDLE h = CreateFileA(names.back().c_str(), FILE_WRITE_ATTRIBUTES, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    FILETIME ft{DWORD(1000000 + i * 1000), 0x01d00000};
    SetFileTime(h, nullptr, nullptr, &ft);
    CloseHandle(h);
  }
  DeleteFileA(file.c_str());
  sc::PruneStats st = sc::prune(dir, 4096);
  CHECK(st.removed == 4 && st.files == 2);
  CHECK(!exists(names[0]) && !exists(names[3]) && exists(names[4]) && exists(names[5]));
  st = sc::prune(dir, 1 << 20);
  CHECK(st.removed == 0 && st.files == 2);

  // Leave nothing behind (only the files this test made).
  for (const std::string& n : names) DeleteFileA(n.c_str());
  for (const std::wstring& w : written) DeleteFileW(w.c_str());
  std::printf("shader_cache_test: %d checks, %d failed (scratch %s)\n", g_checks, g_fail, base.c_str());
  return g_fail ? 1 : 0;
}
