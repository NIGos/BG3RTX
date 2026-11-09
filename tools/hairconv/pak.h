#pragma once
// Read-only access to the game's .pak packages (LSPK v18) and the LSF / LSX resources inside them, for
// bg3rtx_hairconv --all (docs/RTX_HAIR_ROADMAP.md 2f). A C++ port of the repo's tools/assets/lspk.py, compress.py and
// lsf.py, whose layouts follow LSLib (Norbyte, MIT): Resources/LSF/LSFReader.cs, PackageFormat.cs. Credit in
// docs/release/THIRD_PARTY.md. Files are only ever opened for reading.
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace hairconv::pak {

// LSPK / LSF compression method: the low nibble of the entry or section flags.
enum Method { kNone = 0, kZlib = 1, kLz4 = 2, kZstd = 3 };

// LZ4 block / frame decoders (the LZ4 format; no external library). False on malformed input or a size mismatch.
bool lz4_block(const uint8_t* src, size_t n, std::vector<uint8_t>& out, size_t usize);
bool lz4_frame(const uint8_t* src, size_t n, std::vector<uint8_t>& out, size_t usize);
// `frame`: an LSF v2+ chunked section (LZ4 frame); a frame magic is detected either way. zstd and zlib are refused
// with a clear error: BG3's packages use LZ4 only (measured on Patch 8: 0 zstd entries among .lsf, .GR2 and .DDS) and
// no zstd source is vendored in this repo.
bool decompress(int method, const uint8_t* src, size_t n, size_t usize, std::vector<uint8_t>& out, bool frame,
                std::string* error);

struct Entry {
  uint64_t offset = 0;
  uint32_t disk = 0, size = 0;
  uint8_t part = 0, flags = 0;
  uint16_t package = 0;          // index into Set::packages()
  int method() const { return flags & 0x0F; }
};

struct Package {
  std::wstring path;
  std::string file;              // base name, e.g. "Shared.pak"
  uint32_t version = 0;
  uint8_t flags = 0, priority = 0;
  uint16_t parts = 1;
  uint64_t list_offset = 0;
  uint32_t list_size = 0;
  uint64_t bytes = 0, mtime = 0; // part 0's size and write time (the catalogue cache's fingerprint)
  std::wstring part_path(int part) const;
};

// The game's packages as one file system: the package loaded last wins (header priority, then patches and hotfixes
// after the base game, then name; tools/assets/lspk.py PackageSet). Numbered parts (Textures_1.pak) are not opened.
class Set {
 public:
  bool open(const std::wstring& data_dir, std::string* error);
  // Index only names for which keep(name) is true (all when null): saves memory on 570 k entries.
  void set_filter(bool (*keep)(const std::string&)) { keep_ = keep; }
  const Entry* find(const std::string& name) const;
  bool contains(const std::string& name) const { return find(name) != nullptr; }
  bool read(const std::string& name, std::vector<uint8_t>& out, std::string* error) const;
  bool read(const Entry& e, std::vector<uint8_t>& out, std::string* error) const;
  const std::vector<Package>& packages() const { return packages_; }
  // Every indexed name, sorted (byte order: the Python reader's sorted() on these ASCII paths).
  const std::vector<std::string>& names() const { return names_; }
  std::string fingerprint() const;   // package names, sizes and write times

 private:
  bool (*keep_)(const std::string&) = nullptr;
  std::vector<Package> packages_;
  std::vector<std::string> names_;
  std::vector<Entry> entries_;       // parallel to names_
};

// ---- LSF (binary Larian resource) and LSX (XML) ----
struct Attr {
  uint32_t type = 0;
  std::string text;   // strings, and guids formatted as LSLib does for BG3
  int64_t num = 0;    // integer and bool types
};
struct Node {
  std::string name;
  int parent = -1;
  std::vector<std::pair<std::string, Attr>> attrs;
  std::vector<int> children;
  const Attr* get(const char* key) const;
  std::string str(const char* key) const;   // text, or the number as text; "" when absent
};
struct Resource {
  std::vector<Node> nodes;
  std::vector<int> roots;   // the regions (VisualBank, MaterialBank, ...)
  // Preorder walk of n's subtree (the Python Node.walk order).
  template <class F> void walk(int n, F&& f) const {
    std::vector<int> stack{n};
    while (!stack.empty()) {
      const int i = stack.back();
      stack.pop_back();
      f(i);
      const auto& c = nodes[i].children;
      for (auto it = c.rbegin(); it != c.rend(); ++it) stack.push_back(*it);
    }
  }
  int child(int n, const char* name) const;
};
bool parse_lsf(const uint8_t* data, size_t n, Resource& out, std::string* error);
// LSX: only <node id> and <attribute id value> are kept (the visual references reach.py reads).
bool parse_lsx(const uint8_t* data, size_t n, Resource& out, std::string* error);

}  // namespace hairconv::pak
