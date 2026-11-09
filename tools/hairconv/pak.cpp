// LSPK v18 packages, LZ4 and the LSF / LSX resources (pak.h). Port of tools/assets/lspk.py, compress.py, lsf.py and
// lsx.py; the formats follow LSLib (Norbyte, MIT; docs/release/THIRD_PARTY.md).
#include "pak.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <numeric>
#include <unordered_map>

namespace fs = std::filesystem;

namespace hairconv::pak {

namespace {
template <class T> T rd(const uint8_t* p) { T v; std::memcpy(&v, p, sizeof v); return v; }
bool fail(std::string* e, const std::string& why) { if (e) *e = why; return false; }
std::string narrow(const std::wstring& w) {
  if (w.empty()) return {};
  const int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), int(w.size()), nullptr, 0, nullptr, nullptr);
  std::string s(size_t(n), '\0');
  WideCharToMultiByte(CP_UTF8, 0, w.data(), int(w.size()), s.data(), n, nullptr, nullptr);
  return s;
}

// One LZ4 block appended to out (matches may reach into what out already holds: linked frame blocks).
bool lz4_into(const uint8_t* src, size_t n, std::vector<uint8_t>& out, size_t limit) {
  size_t i = 0;
  while (i < n) {
    const uint8_t token = src[i++];
    size_t lit = token >> 4;
    if (lit == 15)
      for (;;) {
        if (i >= n) return false;
        const uint8_t b = src[i++];
        lit += b;
        if (b != 255) break;
      }
    if (lit) {
      if (i + lit > n || out.size() + lit > limit) return false;
      out.insert(out.end(), src + i, src + i + lit);
      i += lit;
    }
    if (i >= n) break;   // the last sequence carries literals only
    if (i + 2 > n) return false;
    const size_t off = size_t(src[i]) | size_t(src[i + 1]) << 8;
    i += 2;
    size_t ml = token & 15;
    if (ml == 15)
      for (;;) {
        if (i >= n) return false;
        const uint8_t b = src[i++];
        ml += b;
        if (b != 255) break;
      }
    ml += 4;
    if (off == 0 || off > out.size() || out.size() + ml > limit) return false;
    const size_t start = out.size() - off;
    for (size_t k = 0; k < ml; ++k) out.push_back(out[start + k]);   // overlapping copies repeat the last `off` bytes
  }
  return true;
}
}  // namespace

bool lz4_block(const uint8_t* src, size_t n, std::vector<uint8_t>& out, size_t usize) {
  out.clear();
  out.reserve(usize);
  return lz4_into(src, n, out, usize) && out.size() == usize;
}

bool lz4_frame(const uint8_t* src, size_t n, std::vector<uint8_t>& out, size_t usize) {
  out.clear();
  out.reserve(usize);
  size_t i = 0;
  while (i < n) {
    if (i + 4 > n) return false;
    const uint32_t magic = rd<uint32_t>(src + i);
    i += 4;
    if ((magic & 0xFFFFFFF0u) == 0x184D2A50u) {   // skippable frame
      if (i + 4 > n) return false;
      i += 4 + size_t(rd<uint32_t>(src + i));
      if (i > n) return false;
      continue;
    }
    if (magic != 0x184D2204u || i + 2 > n) return false;
    const uint8_t flg = src[i];
    i += 2;   // FLG, BD
    if ((flg >> 6) != 1) return false;
    const bool block_checksum = flg & 0x10, independent = flg & 0x20;
    if (flg & 0x08) i += 8;   // content size
    if (flg & 0x01) i += 4;   // dictionary id
    i += 1;                   // header checksum
    for (;;) {
      if (i + 4 > n) return false;
      uint32_t bsize = rd<uint32_t>(src + i);
      i += 4;
      if (!bsize) break;
      const bool raw = bsize & 0x80000000u;
      bsize &= 0x7FFFFFFFu;
      if (i + bsize + (block_checksum ? 4 : 0) > n) return false;
      if (raw) {
        if (out.size() + bsize > usize) return false;
        out.insert(out.end(), src + i, src + i + bsize);
      } else if (independent) {
        std::vector<uint8_t> blk;
        if (!lz4_into(src + i, bsize, blk, usize - out.size())) return false;
        out.insert(out.end(), blk.begin(), blk.end());
      } else if (!lz4_into(src + i, bsize, out, usize)) {
        return false;
      }
      i += bsize + (block_checksum ? 4 : 0);
    }
    if (flg & 0x04) i += 4;   // content checksum
    if (i > n) return false;
  }
  return out.size() == usize;
}

bool decompress(int method, const uint8_t* src, size_t n, size_t usize, std::vector<uint8_t>& out, bool frame,
                std::string* error) {
  switch (method) {
    case kNone:
      out.assign(src, src + n);
      return out.size() == usize || fail(error, "stored entry size mismatch");
    case kLz4:
      if (frame || (n >= 4 && rd<uint32_t>(src) == 0x184D2204u))
        return lz4_frame(src, n, out, usize) || fail(error, "LZ4 frame: malformed or wrong size");
      return lz4_block(src, n, out, usize) || fail(error, "LZ4 block: malformed or wrong size");
    case kZstd:
      return fail(error, "zstd-compressed entry: not supported (BG3 uses LZ4; no zstd decoder is vendored)");
    case kZlib:
      return fail(error, "zlib-compressed entry: not supported (BG3 uses LZ4)");
    default:
      return fail(error, "unknown compression method " + std::to_string(method));
  }
}

// ---------------------------------------------------------------- packages
std::wstring Package::part_path(int part) const {
  if (part == 0) return path;
  const fs::path p(path);
  return (p.parent_path() / (p.stem().wstring() + L"_" + std::to_wstring(part) + p.extension().wstring())).wstring();
}

namespace {
// Patch and hotfix packages load after the base game; HotFix10 after HotFix9 (lspk.py _patch_rank).
std::pair<int, std::vector<long long>> patch_rank(const std::string& file) {
  std::string low = file;
  for (char& c : low) c = char(tolower(static_cast<unsigned char>(c)));
  if (low.rfind("patch", 0) != 0) return {0, {}};
  std::vector<long long> digits;
  long long cur = -1;
  for (char c : low) {
    if (c >= '0' && c <= '9') cur = (cur < 0 ? 0 : cur * 10) + (c - '0');
    else if (cur >= 0) { digits.push_back(cur); cur = -1; }
  }
  if (cur >= 0) digits.push_back(cur);
  return {1, digits};
}
std::string lower(std::string s) {
  for (char& c : s) c = char(tolower(static_cast<unsigned char>(c)));
  return s;
}
}  // namespace

bool Set::open(const std::wstring& data_dir, std::string* error) {
  packages_.clear(); names_.clear(); entries_.clear();
  std::error_code ec;
  std::vector<fs::path> files;
  for (auto& e : fs::directory_iterator(data_dir, ec))
    if (e.is_regular_file(ec) && lower(e.path().extension().string()) == ".pak") files.push_back(e.path());
  if (ec || files.empty()) return fail(error, "no .pak files in " + narrow(data_dir));
  for (const fs::path& p : files) {
    // A numbered part of another package (Textures_1.pak beside Textures.pak) is read through its part 0.
    const std::wstring stem = p.stem().wstring();
    const size_t us = stem.rfind(L'_');
    if (us != std::wstring::npos && us + 1 < stem.size() && us > 0 &&
        std::all_of(stem.begin() + us + 1, stem.end(), [](wchar_t c) { return c >= L'0' && c <= L'9'; }) &&
        fs::exists(p.parent_path() / (stem.substr(0, us) + L".pak"), ec))
      continue;
    Package pk;
    pk.path = p.wstring();
    pk.file = narrow(p.filename().wstring());
    FILE* f = nullptr;
    if (_wfopen_s(&f, pk.path.c_str(), L"rb") != 0 || !f) return fail(error, pk.file + ": cannot open");
    uint8_t hdr[40];
    const bool ok = fread(hdr, 1, sizeof hdr, f) == sizeof hdr;
    fclose(f);
    if (!ok || std::memcmp(hdr, "LSPK", 4) != 0) return fail(error, "rejected package " + pk.file + ": not an LSPK package");
    pk.version = rd<uint32_t>(hdr + 4);
    if (pk.version != 18) return fail(error, "rejected package " + pk.file + ": LSPK v" + std::to_string(pk.version) + " (v18 only)");
    pk.list_offset = rd<uint64_t>(hdr + 8);
    pk.list_size = rd<uint32_t>(hdr + 16);
    pk.flags = hdr[20];
    pk.priority = hdr[21];
    pk.parts = rd<uint16_t>(hdr + 38);
    pk.bytes = uint64_t(fs::file_size(p, ec));
    pk.mtime = uint64_t(fs::last_write_time(p, ec).time_since_epoch().count());
    packages_.push_back(pk);
  }
  std::stable_sort(packages_.begin(), packages_.end(), [](const Package& a, const Package& b) {
    if (a.priority != b.priority) return a.priority < b.priority;
    const auto ra = patch_rank(a.file), rb = patch_rank(b.file);
    if (ra != rb) return ra < rb;
    return lower(a.file) < lower(b.file);
  });
  // Later packages override earlier ones.
  std::unordered_map<std::string, uint32_t> at;
  std::vector<std::string> names;
  std::vector<Entry> entries;
  for (size_t pi = 0; pi < packages_.size(); ++pi) {
    const Package& pk = packages_[pi];
    FILE* f = nullptr;
    if (_wfopen_s(&f, pk.path.c_str(), L"rb") != 0 || !f) return fail(error, pk.file + ": cannot open");
    _fseeki64(f, (long long)pk.list_offset, SEEK_SET);
    uint8_t head[8];
    std::vector<uint8_t> packed;
    bool ok = fread(head, 1, 8, f) == 8;
    const uint32_t count = ok ? rd<uint32_t>(head) : 0, csize = ok ? rd<uint32_t>(head + 4) : 0;
    if (ok) { packed.resize(csize); ok = fread(packed.data(), 1, csize, f) == csize; }
    fclose(f);
    std::vector<uint8_t> list;
    constexpr size_t kEntry = 272;
    if (!ok || !lz4_block(packed.data(), packed.size(), list, size_t(count) * kEntry))
      return fail(error, pk.file + ": unreadable file list");
    for (uint32_t i = 0; i < count; ++i) {
      const uint8_t* e = &list[size_t(i) * kEntry];
      size_t len = 0;
      while (len < 256 && e[len]) ++len;
      std::string name(reinterpret_cast<const char*>(e), len);
      std::replace(name.begin(), name.end(), '\\', '/');
      if (keep_ && !keep_(name)) continue;
      Entry en;
      en.offset = uint64_t(rd<uint32_t>(e + 256)) | uint64_t(rd<uint16_t>(e + 260)) << 32;
      en.part = e[262];
      en.flags = e[263];
      en.disk = rd<uint32_t>(e + 264);
      en.size = rd<uint32_t>(e + 268);
      en.package = uint16_t(pi);
      const auto it = at.find(name);
      if (it == at.end()) { at.emplace(name, uint32_t(names.size())); names.push_back(std::move(name)); entries.push_back(en); }
      else entries[it->second] = en;
    }
  }
  std::vector<uint32_t> order(names.size());
  std::iota(order.begin(), order.end(), 0u);
  std::sort(order.begin(), order.end(), [&](uint32_t a, uint32_t b) { return names[a] < names[b]; });
  names_.reserve(names.size());
  entries_.reserve(names.size());
  for (uint32_t i : order) { names_.push_back(std::move(names[i])); entries_.push_back(entries[i]); }
  return true;
}

const Entry* Set::find(const std::string& name) const {
  const auto it = std::lower_bound(names_.begin(), names_.end(), name);
  return it != names_.end() && *it == name ? &entries_[size_t(it - names_.begin())] : nullptr;
}

bool Set::read(const std::string& name, std::vector<uint8_t>& out, std::string* error) const {
  const Entry* e = find(name);
  if (!e) return fail(error, name + ": not in the game packages");
  if (!read(*e, out, error)) { if (error) *error = name + ": " + *error; return false; }
  return true;
}

bool Set::read(const Entry& e, std::vector<uint8_t>& out, std::string* error) const {
  const Package& pk = packages_[e.package];
  FILE* f = nullptr;
  if (_wfopen_s(&f, pk.part_path(e.part).c_str(), L"rb") != 0 || !f) return fail(error, pk.file + ": cannot open part " + std::to_string(e.part));
  std::vector<uint8_t> raw(e.disk);
  _fseeki64(f, (long long)e.offset, SEEK_SET);
  const bool ok = fread(raw.data(), 1, raw.size(), f) == raw.size();
  fclose(f);
  if (!ok) return fail(error, pk.file + ": truncated read");
  if (e.method() == kNone || e.size == 0) { out = std::move(raw); return true; }
  return decompress(e.method(), raw.data(), raw.size(), e.size, out, false, error);
}

std::string Set::fingerprint() const {
  std::string s;
  for (const Package& p : packages_)
    s += p.file + ":" + std::to_string(p.bytes) + ":" + std::to_string(p.mtime) + ";";
  return s;
}

// ---------------------------------------------------------------- LSF
const Attr* Node::get(const char* key) const {
  const Attr* found = nullptr;
  for (const auto& [k, v] : attrs)
    if (k == key) found = &v;   // a repeated name: the last one wins (Python dict assignment)
  return found;
}

std::string Node::str(const char* key) const {
  const Attr* a = get(key);
  if (!a) return {};
  return a->text.empty() && a->type != 20 && a->type != 21 && a->type != 22 && a->type != 23 && a->type != 29 && a->type != 30 &&
                 a->type != 31
             ? std::to_string(a->num)
             : a->text;
}

int Resource::child(int n, const char* name) const {
  for (int c : nodes[n].children)
    if (nodes[c].name == name) return c;
  return -1;
}

namespace {
bool is_string_type(uint32_t t) { return t == 20 || t == 21 || t == 22 || t == 23 || t == 29 || t == 30; }
size_t int_size(uint32_t t) {
  switch (t) {
    case 1: case 19: case 27: return 1;
    case 2: case 3: return 2;
    case 4: case 5: return 4;
    case 24: case 26: case 32: return 8;
    default: return 0;
  }
}
bool signed_type(uint32_t t) { return t == 2 || t == 4 || t == 26 || t == 27 || t == 32; }

Attr decode(uint32_t t, const uint8_t* d, size_t n) {
  Attr a;
  a.type = t;
  if (is_string_type(t)) {
    size_t len = 0;
    while (len < n && d[len]) ++len;
    a.text.assign(reinterpret_cast<const char*>(d), len);
  } else if (t == 31 && n == 16) {
    // A .NET Guid with the last eight bytes swapped in pairs (resource.py guid_from_lsf).
    const uint8_t b[16] = {d[3], d[2], d[1], d[0], d[5], d[4], d[7], d[6], d[9], d[8], d[11], d[10], d[13], d[12], d[15], d[14]};
    char s[40];
    snprintf(s, sizeof s, "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x", b[0], b[1], b[2], b[3], b[4],
             b[5], b[6], b[7], b[8], b[9], b[10], b[11], b[12], b[13], b[14], b[15]);
    a.text = s;
  } else if (const size_t sz = int_size(t); sz && sz == n) {
    uint64_t v = 0;
    std::memcpy(&v, d, sz);
    if (signed_type(t) && sz < 8 && (v >> (8 * sz - 1)) & 1) v |= ~uint64_t(0) << (8 * sz);
    a.num = int64_t(v);
  }
  return a;
}
}  // namespace

bool parse_lsf(const uint8_t* data, size_t size, Resource& out, std::string* error) {
  out = Resource{};
  if (size < 8 || std::memcmp(data, "LSOF", 4) != 0) return fail(error, "not an LSF file");
  const uint32_t ver = rd<uint32_t>(data + 4);
  if (ver < 2 || ver > 7) return fail(error, "LSF v" + std::to_string(ver) + " is not supported");
  size_t p = 8 + (ver >= 5 ? 8 : 4);
  const bool keys = ver >= 6;
  const int nf = keys ? 10 : 8;
  if (p + 4 * nf + 8 > size) return fail(error, "truncated LSF header");
  uint32_t md[10];
  for (int i = 0; i < nf; ++i) md[i] = rd<uint32_t>(data + p + 4 * i);
  p += 4 * nf;
  const int method = data[p] & 0x0F;
  const uint32_t fmt = rd<uint32_t>(data + p + 4);
  p += 8;
  if (fmt > 2) return fail(error, "unsupported LSF format " + std::to_string(fmt));
  // (uncompressed, on disk) pairs: strings, [keys], nodes, attributes, values; sections in order strings, nodes,
  // attributes, values, [keys].
  const int s = 0, nd = keys ? 4 : 2, at = nd + 2, va = at + 2;
  auto take = [&](int k, bool frame, std::vector<uint8_t>& sec) {
    const uint32_t usize = md[k], dsize = md[k + 1];
    const size_t n = dsize ? dsize : usize;
    if (p + n > size) return fail(error, "LSF section exceeds the file");
    const uint8_t* src = data + p;
    p += n;
    if (!dsize) { sec.assign(src, src + n); return true; }
    return decompress(method, src, n, usize, sec, frame, error);
  };
  std::vector<uint8_t> strings, nodes, attrs, vals;
  if (!take(s, false, strings) || !take(nd, true, nodes) || !take(at, true, attrs) || !take(va, true, vals)) return false;
  // Names: u32 buckets, per bucket u16 count, per string u16 length + UTF-8.
  std::vector<std::vector<std::string>> names;
  {
    size_t q = 0;
    if (strings.size() < 4) return fail(error, "truncated LSF names");
    const uint32_t nb = rd<uint32_t>(&strings[0]);
    q = 4;
    names.resize(nb);
    for (uint32_t b = 0; b < nb; ++b) {
      if (q + 2 > strings.size()) return fail(error, "truncated LSF names");
      const uint16_t cnt = rd<uint16_t>(&strings[q]);
      q += 2;
      for (uint16_t i = 0; i < cnt; ++i) {
        if (q + 2 > strings.size()) return fail(error, "truncated LSF names");
        const uint16_t len = rd<uint16_t>(&strings[q]);
        q += 2;
        if (q + len > strings.size()) return fail(error, "truncated LSF name string");
        names[b].emplace_back(reinterpret_cast<const char*>(&strings[q]), len);
        q += len;
      }
    }
  }
  bool bad_name = false;
  auto nm = [&](uint32_t h) -> const std::string& {
    static const std::string empty;
    const uint32_t b = h >> 16, i = h & 0xFFFF;
    if (b >= names.size() || i >= names[b].size()) { bad_name = true; return empty; }
    return names[b][i];
  };
  const size_t stride = fmt == 1 ? 16 : 12;
  if (nodes.size() % stride || attrs.size() % stride) return fail(error, "incomplete LSF node or attribute record");
  const size_t nn = nodes.size() / stride, na = attrs.size() / stride;
  out.nodes.resize(nn);
  auto value = [&](uint32_t tl, uint32_t off, Attr& a) {
    const uint32_t t = tl & 0x3F, ln = tl >> 6;
    if (size_t(off) + ln > vals.size()) return false;
    a = decode(t, vals.data() + off, ln);
    return true;
  };
  if (fmt == 1) {
    std::vector<int32_t> first(nn);
    for (size_t i = 0; i < nn; ++i) {
      const uint8_t* r = &nodes[i * 16];
      out.nodes[i].name = nm(rd<uint32_t>(r));
      out.nodes[i].parent = rd<int32_t>(r + 4);
      first[i] = rd<int32_t>(r + 12);
    }
    for (size_t i = 0; i < nn; ++i) {
      int32_t a = first[i];
      size_t guard = 0;
      while (a != -1) {
        if (a < 0 || size_t(a) >= na || ++guard > na) return fail(error, "invalid or cyclic LSF attribute link");
        const uint8_t* r = &attrs[size_t(a) * 16];
        Attr v;
        if (!value(rd<uint32_t>(r + 4), rd<uint32_t>(r + 12), v)) return fail(error, "LSF value exceeds its section");
        out.nodes[i].attrs.emplace_back(nm(rd<uint32_t>(r)), std::move(v));
        a = rd<int32_t>(r + 8);
      }
    }
  } else {
    for (size_t i = 0; i < nn; ++i) {
      const uint8_t* r = &nodes[i * 12];
      out.nodes[i].name = nm(rd<uint32_t>(r));
      out.nodes[i].parent = rd<int32_t>(r + 8);
    }
    uint32_t off = 0;
    for (size_t i = 0; i < na; ++i) {
      const uint8_t* r = &attrs[i * 12];
      const uint32_t tl = rd<uint32_t>(r + 4);
      const int32_t owner = rd<int32_t>(r + 8);
      if (owner < 0 || size_t(owner) >= nn) return fail(error, "invalid LSF attribute owner");
      Attr v;
      if (!value(tl, off, v)) return fail(error, "LSF value exceeds its section");
      out.nodes[size_t(owner)].attrs.emplace_back(nm(rd<uint32_t>(r)), std::move(v));
      off += tl >> 6;
    }
  }
  if (bad_name) return fail(error, "invalid LSF name handle");
  for (size_t i = 0; i < nn; ++i) {
    const int par = out.nodes[i].parent;
    if (par >= 0 && size_t(par) >= nn) return fail(error, "invalid LSF parent link");
    if (par >= 0) out.nodes[size_t(par)].children.push_back(int(i));
    else out.roots.push_back(int(i));
  }
  // Cycle check: every node must reach a root (lsf.py does the same walk).
  std::vector<uint8_t> state(nn, 0);   // 0 unknown, 1 on the current chain, 2 reaches a root
  std::vector<size_t> chain;
  for (size_t i = 0; i < nn; ++i) {
    chain.clear();
    for (size_t c = i;;) {
      if (state[c] == 2) break;
      if (state[c] == 1) return fail(error, "cyclic LSF parent link");
      state[c] = 1;
      chain.push_back(c);
      if (out.nodes[c].parent < 0) break;
      c = size_t(out.nodes[c].parent);
    }
    for (size_t k : chain) state[k] = 2;
  }
  return true;
}

// ---------------------------------------------------------------- LSX
namespace {
std::string xml_attr(const std::string& tag, const char* key) {
  const std::string k = std::string(" ") + key + "=\"";
  const size_t a = tag.find(k);
  if (a == std::string::npos) return {};
  const size_t b = tag.find('"', a + k.size());
  std::string v = tag.substr(a + k.size(), b == std::string::npos ? std::string::npos : b - a - k.size());
  // The entities Larian's writer uses.
  std::string o;
  for (size_t i = 0; i < v.size(); ++i) {
    if (v[i] == '&') {
      static const std::pair<const char*, char> ents[] = {{"&amp;", '&'}, {"&lt;", '<'}, {"&gt;", '>'}, {"&quot;", '"'}, {"&apos;", '\''}};
      bool hit = false;
      for (auto& [e, c] : ents)
        if (v.compare(i, strlen(e), e) == 0) { o += c; i += strlen(e) - 1; hit = true; break; }
      if (!hit) o += v[i];
    } else {
      o += v[i];
    }
  }
  return o;
}
}  // namespace

bool parse_lsx(const uint8_t* data, size_t n, Resource& out, std::string* error) {
  out = Resource{};
  const std::string text(reinterpret_cast<const char*>(data), n);
  std::vector<int> stack;
  size_t i = 0;
  while ((i = text.find('<', i)) != std::string::npos) {
    const size_t e = text.find('>', i);
    if (e == std::string::npos) return fail(error, "truncated LSX tag");
    const std::string tag = text.substr(i, e - i + 1);
    i = e + 1;
    const bool self_close = tag.size() >= 2 && tag[tag.size() - 2] == '/';
    if (tag.compare(0, 6, "<node ") == 0 || tag.compare(0, 6, "<node>") == 0 || tag.compare(0, 6, "<node/") == 0) {
      Node nd;
      nd.name = xml_attr(tag, "id");
      nd.parent = stack.empty() ? -1 : stack.back();
      const int idx = int(out.nodes.size());
      out.nodes.push_back(std::move(nd));
      if (out.nodes[idx].parent >= 0) out.nodes[size_t(out.nodes[idx].parent)].children.push_back(idx);
      else out.roots.push_back(idx);
      if (!self_close) stack.push_back(idx);
    } else if (tag.compare(0, 7, "</node>") == 0) {
      if (stack.empty()) return fail(error, "unbalanced LSX </node>");
      stack.pop_back();
    } else if (tag.compare(0, 11, "<attribute ") == 0 && !stack.empty()) {
      Attr a;
      a.type = 20;
      a.text = xml_attr(tag, "value");
      out.nodes[size_t(stack.back())].attrs.emplace_back(xml_attr(tag, "id"), std::move(a));
    }
  }
  return true;
}

}  // namespace hairconv::pak
